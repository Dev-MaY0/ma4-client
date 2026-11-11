#include "Injector.h"

#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace inject {

    namespace {
        constexpr wchar_t kGameExe[] = L"Minecraft.Windows.exe";
        constexpr wchar_t kGameAppId[] = L"Microsoft.MinecraftUWP_8wekyb3d8bbwe!App";

        bool iequals(std::wstring const& a, wchar_t const* b) {
            return _wcsicmp(a.c_str(), b) == 0;
        }

        std::wstring lastError(wchar_t const* what, DWORD code) {
            wchar_t* msg = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                           reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
            std::wstring out = std::wstring(what) + L" (" + std::to_wstring(code) + L")";
            if (msg) {
                std::wstring m = msg;
                LocalFree(msg);
                while (!m.empty() && (m.back() == L'\n' || m.back() == L'\r' || m.back() == L' ')) m.pop_back();
                if (!m.empty()) out += L": " + m;
            }
            return out;
        }

        struct Handle {
            HANDLE h = nullptr;
            Handle() = default;
            explicit Handle(HANDLE v)
                : h(v) {}
            ~Handle() {
                if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
            }
            Handle(Handle const&) = delete;
            Handle& operator=(Handle const&) = delete;
            [[nodiscard]] bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
        };

        std::wstring exeDirectory() {
            wchar_t buf[MAX_PATH * 2] = {};
            DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
            std::wstring p(buf, n);
            size_t slash = p.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : p.substr(0, slash);
        }
    } // namespace

    std::wstring fileNameOf(std::wstring const& path) {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1);
    }

    std::wstring dataDirectory() {
        PWSTR raw = nullptr;
        std::wstring dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
            dir = std::wstring(raw) + L"\\ma4";
        }
        if (raw) CoTaskMemFree(raw);
        if (dir.empty()) dir = L".";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    DWORD findMinecraft() {
        Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap.valid()) return 0;
        PROCESSENTRY32W pe {};
        pe.dwSize = sizeof(pe);
        DWORD found = 0;
        if (Process32FirstW(snap.h, &pe)) {
            do {
                if (iequals(pe.szExeFile, kGameExe)) {
                    found = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap.h, &pe));
        }
        return found;
    }

    namespace {
        // calls fn(moduleName) for every module of the process until it returns true
        template<class Fn>
        bool anyModule(DWORD pid, Fn&& fn) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
                if (!snap.valid()) {
                    // ERROR_BAD_LENGTH happens while the module list is changing: just ask again
                    if (GetLastError() == ERROR_BAD_LENGTH) continue;
                    return false;
                }
                MODULEENTRY32W me {};
                me.dwSize = sizeof(me);
                if (Module32FirstW(snap.h, &me)) {
                    do {
                        if (fn(std::wstring(me.szModule))) return true;
                    } while (Module32NextW(snap.h, &me));
                }
                return false;
            }
            return false;
        }
    } // namespace

    bool isModuleLoaded(DWORD pid, std::wstring const& fileName) {
        if (!pid || fileName.empty()) return false;
        return anyModule(pid, [&](std::wstring const& m) { return _wcsicmp(m.c_str(), fileName.c_str()) == 0; });
    }

    bool isRendererReady(DWORD pid) {
        if (!pid) return false;
        bool d3d = false, dxgi = false;
        anyModule(pid, [&](std::wstring const& m) {
            if (iequals(m, L"d3d11.dll") || iequals(m, L"d3d12.dll")) d3d = true;
            if (iequals(m, L"dxgi.dll")) dxgi = true;
            return d3d && dxgi;
        });
        return d3d && dxgi;
    }

    bool launchMinecraft(DWORD& pid, std::wstring& error) {
        pid = 0;
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool uninit = SUCCEEDED(init);

        bool ok = false;
        IApplicationActivationManager* mgr = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr));
        if (SUCCEEDED(hr) && mgr) {
            DWORD newPid = 0;
            hr = mgr->ActivateApplication(kGameAppId, nullptr, AO_NOERRORUI, &newPid);
            if (SUCCEEDED(hr)) {
                pid = newPid;
                ok = true;
            }
            mgr->Release();
        }
        if (!ok) {
            // older shells: the minecraft: protocol opens the same app
            HINSTANCE r = ShellExecuteW(nullptr, L"open", L"minecraft:", nullptr, nullptr, SW_SHOWNORMAL);
            ok = reinterpret_cast<INT_PTR>(r) > 32;
            if (!ok) error = lastError(L"Could not start Minecraft", static_cast<DWORD>(hr));
        }
        if (uninit) CoUninitialize();
        return ok;
    }

    bool grantAppPackageAccess(std::wstring const& path, std::wstring& error) {
        PSID allPackages = nullptr;
        PSID allRestricted = nullptr;
        if (!ConvertStringSidToSidW(L"S-1-15-2-1", &allPackages) || !ConvertStringSidToSidW(L"S-1-15-2-2", &allRestricted)) {
            error = lastError(L"ConvertStringSidToSid", GetLastError());
            if (allPackages) LocalFree(allPackages);
            return false;
        }

        PACL oldDacl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        DWORD rc = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr, &sd);
        bool ok = false;
        if (rc != ERROR_SUCCESS) {
            error = lastError(L"GetNamedSecurityInfo", rc);
        } else {
            EXPLICIT_ACCESSW ea[2] = {};
            PSID sids[2] = { allPackages, allRestricted };
            for (int i = 0; i < 2; ++i) {
                ea[i].grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
                ea[i].grfAccessMode = SET_ACCESS;
                ea[i].grfInheritance = NO_INHERITANCE;
                ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
                ea[i].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
                ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(sids[i]);
            }
            PACL newDacl = nullptr;
            rc = SetEntriesInAclW(2, ea, oldDacl, &newDacl);
            if (rc != ERROR_SUCCESS) {
                error = lastError(L"SetEntriesInAcl", rc);
            } else {
                rc = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                           newDacl, nullptr);
                if (rc != ERROR_SUCCESS) {
                    error = lastError(L"SetNamedSecurityInfo", rc);
                } else {
                    ok = true;
                }
                LocalFree(newDacl);
            }
        }
        if (sd) LocalFree(sd);
        LocalFree(allPackages);
        LocalFree(allRestricted);
        return ok;
    }

    bool injectDll(DWORD pid, std::wstring const& dllPath, std::wstring& error) {
        if (!pid) {
            error = L"Minecraft is not running";
            return false;
        }
        const std::wstring fileName = fileNameOf(dllPath);
        if (isModuleLoaded(pid, fileName)) return true;

        Handle proc(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                FALSE, pid));
        if (!proc.valid()) {
            error = lastError(L"OpenProcess", GetLastError());
            return false;
        }

        // kernel32 is mapped at the same address in every process of a boot, so our LoadLibraryW is theirs too
        FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        if (!load) {
            error = lastError(L"GetProcAddress(LoadLibraryW)", GetLastError());
            return false;
        }

        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(proc.h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote) {
            error = lastError(L"VirtualAllocEx", GetLastError());
            return false;
        }
        bool ok = false;
        if (!WriteProcessMemory(proc.h, remote, dllPath.c_str(), bytes, nullptr)) {
            error = lastError(L"WriteProcessMemory", GetLastError());
        } else {
            Handle thread(CreateRemoteThread(proc.h, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(load), remote, 0, nullptr));
            if (!thread.valid()) {
                error = lastError(L"CreateRemoteThread", GetLastError());
            } else {
                DWORD wait = WaitForSingleObject(thread.h, 20000);
                DWORD exitCode = 0;
                GetExitCodeThread(thread.h, &exitCode);
                if (wait != WAIT_OBJECT_0) {
                    error = L"The game did not answer in time";
                } else if (exitCode == 0) {
                    error = L"The game refused to load the DLL";
                } else {
                    ok = true;
                }
            }
        }
        VirtualFreeEx(proc.h, remote, 0, MEM_RELEASE);
        if (ok) {
            // DllMain runs on the thread that loaded it: give the module list a moment, then confirm
            for (int i = 0; i < 20 && !isModuleLoaded(pid, fileName); ++i) Sleep(100);
            ok = isModuleLoaded(pid, fileName);
            if (!ok) error = L"The DLL unloaded again right after loading";
        }
        return ok;
    }

    std::wstring resolveDll(std::wstring const& configured) {
        std::error_code ec;
        auto exists = [&](std::filesystem::path const& p) { return std::filesystem::is_regular_file(p, ec); };

        if (!configured.empty() && exists(configured)) return configured;

        const std::filesystem::path dir = exeDirectory();
        const std::filesystem::path candidates[] = {
            dir / L"ma4.dll",
            dir.parent_path() / L"ma4.dll",
            std::filesystem::path(dataDirectory()) / L"ma4.dll",
        };
        for (auto const& c : candidates) {
            if (exists(c)) return std::filesystem::absolute(c, ec).wstring();
        }
        return L"";
    }

} // namespace inject
