#pragma once
// ma4 Client - iOS style "Settings app" shell: a sidebar + navigation stack window with grouped inset lists,
// module detail pages, popover menus and a colour sheet. The shell is driven by a plain data model that the
// host (the game's ClickGUI screen, or the standalone harness) rebuilds every frame.

#include "IosReplay.h"
#include "IosText.h"
#include "IosUi.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ios {

    // ------------------------------------------------------------------------------------------ data model
    struct SettingRow {
        enum class Kind {
            Toggle,
            Slider,
            Choice,
            Key,
            Color,
            Text,
            Action
        };

        Kind kind = Kind::Toggle;
        uint64_t id = 0;
        std::wstring title;
        std::wstring subtitle;

        // Toggle
        bool* boolean = nullptr;
        // Slider
        float* number = nullptr;
        float lo = 0.f, hi = 1.f, step = 0.f;
        // Choice
        int* choice = nullptr;
        std::vector<std::wstring> options;
        // Key
        int* keycode = nullptr;
        std::wstring keyText;
        // Color
        Col color = { 1.f, 1.f, 1.f, 1.f };
        bool rgb = false;
        bool hasRgbToggle = true;
        std::function<void(Col, bool)> setColor;
        // Text
        std::wstring* str = nullptr;
        int maxChars = 64;

        /// Invoked after the user edited the value (host runs Setting::update / userUpdate).
        std::function<void()> changed;
        /// Action rows
        std::function<void()> action;
        /// Key rows: reset to "none" (right click)
        std::function<void()> clear;
    };

    struct SettingsSection {
        std::wstring header;
        std::wstring footer;
        std::vector<SettingRow> rows;
    };

    struct ModuleRow {
        enum class Trailing {
            Toggle,
            Open,
            HoldKey,
            Install,
            Installed
        };
        uint64_t id = 0;
        std::string key;
        std::wstring name;
        std::wstring desc;
        std::wstring author;
        bool inGame = true;  // listed in the "Game" section
        bool inHud = false;  // listed in the "HUD" section
        bool market = false; // plugin market entry (installable script)
        Trailing trailing = Trailing::Toggle;
        bool enabled = false;
        std::wstring keyText;
        std::function<void(bool)> setEnabled;
        std::function<void()> open;
        std::function<void()> install;
    };

    struct ModuleDetail {
        std::wstring name;
        std::wstring desc;
        std::string key;
        bool hasToggle = true;
        bool enabled = false;
        std::function<void(bool)> setEnabled;
        std::vector<SettingsSection> sections;
        std::function<void()> reset;
        // HUD preview (optional)
        bool hasPreview = false;
        Vec2 previewSize = { 0.f, 0.f };
        std::function<void(Rect const& area)> drawPreview;
    };

    struct Strings {
        std::wstring appName = L"ma4";
        std::wstring search = L"Search";
        std::wstring searchSettings = L"Search Settings";
        std::wstring all = L"All";
        std::wstring game = L"Game";
        std::wstring hud = L"HUD";
        std::wstring plugins = L"Plugins";
        std::wstring settings = L"Settings";
        std::wstring hudEditor = L"HUD Editor";
        std::wstring replay = L"Replay";
        std::wstring modulesHeader = L"MODULES";
        std::wstring clientHeader = L"CLIENT";
        std::wstring modulesBack = L"Modules";
        std::wstring reset = L"Reset to Defaults";
        std::wstring install = L"Get";
        std::wstring installed = L"Installed";
        std::wstring open = L"Open";
        std::wstring done = L"Done";
        std::wstring color = L"Color";
        std::wstring rgb = L"RGB";
        std::wstring opacity = L"Opacity";
        std::wstring hex = L"Hex";
        std::wstring noResults = L"No Results";
        std::wstring pressKey = L"Press a key";
        std::wstring none = L"None";
        std::wstring byAuthor = L"by %s";
        std::wstring hudEditorHint = L"Drag HUD elements to reposition them";
        std::wstring replayHint = L"Record and watch your gameplay";
        std::wstring closeHint = L"Close";
        // replay
        std::wstring recorder = L"Replay Recorder";
        std::wstring recordReady = L"Ready to record";
        std::wstring recordingNow = L"Recording";
        std::wstring startRecording = L"Record";
        std::wstring stopRecording = L"Stop";
        std::wstring addMarker = L"Add Marker";
        std::wstring recordings = L"RECORDINGS";
        std::wstring noRecordings = L"No recordings yet";
        std::wstring noRecordingsHint = L"Start a recording, play, and it will show up here.";
        std::wstring markersCount = L"%d markers";
        std::wstring deleteReplay = L"Delete Recording";
        std::wstring follow = L"Follow";
        std::wstring addKeyframe = L"Add Keyframe";
        std::wstring clearKeyframes = L"Clear";
        std::wstring cameraPath = L"Camera Path";
        std::wstring cameraPathHint = L"Add two or more keyframes to enable";
        std::wstring mapToggle = L"Map";
    };

    enum class Section {
        All,
        Game,
        Hud,
        Plugins,
        Settings,
        Replay
    };

    struct Frame {
        Strings str;
        std::wstring version;
        std::vector<ModuleRow> modules;
        std::vector<SettingsSection> clientSettings;
        std::function<bool(uint64_t id, ModuleDetail& out)> detail;
        std::function<void()> closeWindow;
        std::function<void()> openHudEditor;
        ReplayHost replay; // set `available` to show the Replay section
        ID2D1Bitmap* logo = nullptr;
    };

    // ------------------------------------------------------------------------------------------ shell
    class Shell {
    public:
        Shell();

        static float computeScale(Rect const& screen, float userScale = 1.f);
        static Rect computeWindow(Rect const& screen, float scale);

        void setTextEditFactory(std::function<std::shared_ptr<TextEdit>(int)> f) { makeEdit_ = std::move(f); }

        /// Called when the screen is opened.
        void reset(bool instant);
        /// Called when the screen is closed (drops focus, popovers, sheets).
        void onClosed();
        /// Draws and drives the window. Returns false once it has completely faded out.
        bool frame(Ui& ui, Rect const& screen, Frame& f, bool active);

        /// Key pressed while the window is open. Returns true if the key was consumed by an overlay
        /// (popover, colour sheet, text focus, key capture). When false the host should close the window on Esc.
        bool onEscape();
        bool textFocused() const;
        bool capturingKey() const { return capturingRow_ != 0; }
        void feedKey(int vk) { capturedKey_ = vk; }

        void jumpToModule(uint64_t id);
        [[nodiscard]] Section section() const { return section_; }
        [[nodiscard]] bool wantsDetail() const { return detailId_ != 0; }
        [[nodiscard]] uint64_t detailId() const { return detailId_; }
        [[nodiscard]] float openAmount() const { return openAmount_; }
        [[nodiscard]] Rect windowRect() const { return win_; }
        [[nodiscard]] std::wstring searchText() const { return search_ ? search_->getText() : L""; }
        [[nodiscard]] std::shared_ptr<TextEdit> searchEdit();

        // test hooks
        void selectSection(Section s);
        void pushDetail(uint64_t id);
        void pushReplay(uint64_t id);
        /// Drops Direct2D bitmaps (terrain map). Call from the renderer cleanup event.
        void releaseDeviceResources() { viewer_.terrainBitmaps.clear(); }
        void viewerSeek(float t, bool playing) {
            viewer_.time = t;
            viewer_.playing = playing;
        }
        void openColorSheet(SettingRow const& row);
        void openPopover(SettingRow const& row, Rect const& anchor);

    private:
        struct Scroll {
            float pos = 0.f, target = 0.f, max = 0.f;
            bool moving = false;
        };
        struct Popover {
            bool open = false;
            uint64_t row = 0;
            Rect anchor;
            std::vector<std::wstring> options;
            int selected = 0;
            std::function<void(int)> pick;
            float scroll = 0.f;
        };
        struct ColorSheet {
            bool open = false;
            uint64_t row = 0;
            std::wstring title;
            float h = 0.f, s = 1.f, v = 1.f, a = 1.f;
            bool rgb = false;
            bool hasRgb = true;
            bool editing[3] = { false, false, false };
            std::function<void(Col, bool)> apply;
            std::shared_ptr<TextEdit> hex;
            // pull-down-to-dismiss (sheet detent behaviour)
            bool dragging = false;
            float dragStartY = 0.f;
            float dragOff = 0.f;
        };

        // layout helpers (all pixel values)
        Rect windowRect(Ui& ui, Rect const& screen) const;
        void drawSidebar(Ui& ui, Frame& f, Rect const& win, float sidebarW);
        void drawContent(Ui& ui, Frame& f, Rect const& content);
        void drawRootPage(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive);
        void drawDetailPage(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive);
        void drawNavBar(Ui& ui, Frame& f, Rect const& content, std::wstring const& title, float scroll,
                        bool showBack, std::wstring const& backTitle);
        float drawSection(Ui& ui, SettingsSection& sec, float x, float y, float w, Rect const& view, bool interactive,
                          std::wstring const& filter);
        float rowHeight(Ui& ui, SettingRow& r, float w);
        void drawRow(Ui& ui, SettingRow& r, Rect const& rc, bool interactive);
        float drawModuleList(Ui& ui, Frame& f, std::vector<ModuleRow*> const& rows, float x, float y, float w,
                             Rect const& view, bool interactive);
        float drawReplayRoot(Ui& ui, Frame& f, float x, float y, float w, Rect const& view, bool interactive);
        void drawReplayViewer(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive);
        void drawPopover(Ui& ui, Rect const& win);
        void drawColorSheet(Ui& ui, Frame& f, Rect const& win);
        void applyScroll(Ui& ui, Scroll& sc, Rect const& view, bool interactive);

        std::shared_ptr<TextEdit> editFor(uint64_t id, int maxChars = 64);
        void blurAll();
        bool filterMatches(std::wstring const& filter, SettingRow const& r) const;

        enum class DetailKind {
            Module,
            Replay
        };
        struct Viewer {
            struct Key {
                float t = 0.f, cx = 0.f, cz = 0.f, zoom = 6.f;
            };
            std::shared_ptr<ReplayClip> clip;
            uint64_t id = 0;
            float time = 0.f;
            bool playing = false;
            int speed = 1;
            bool follow = true;
            bool cameraPath = false;
            bool fitted = false;
            bool showTerrain = true;
            bool terrainIndexed = false;
            std::unordered_map<int64_t, size_t> terrainIndex;                 // chunk key -> clip.terrain index
            std::unordered_map<int64_t, ComPtr<ID2D1Bitmap>> terrainBitmaps; // lazily built 16x16 chunk images
            float camX = 0.f, camZ = 0.f, zoom = 6.f; // zoom = points per block
            Vec2 dragLast = {};
            std::vector<Key> keys;
        };
        DetailKind detailKind_ = DetailKind::Module;
        Viewer viewer_;

        std::function<std::shared_ptr<TextEdit>(int)> makeEdit_;
        std::shared_ptr<TextEdit> search_;
        std::map<uint64_t, std::shared_ptr<TextEdit>> edits_;

        Section section_ = Section::All;
        uint64_t detailId_ = 0;
        bool detailOpen_ = false;
        Scroll rootScroll_[6];
        Scroll detailScroll_;
        std::wstring lastSearch_;

        Popover pop_;
        ColorSheet sheet_;
        uint64_t capturingRow_ = 0;
        int capturedKey_ = 0;
        bool claimedFocus_ = false;
        bool closing_ = false;
        float openAmount_ = 0.f;
        bool openedOnce_ = false;
        Rect win_;
        int openRestart_ = 0; // 0 none, 1 animate from closed, 2 instant
        bool instantPush_ = false;
        bool navEdge_ = true; // scroll edge fade under the floating nav buttons (off on the full-bleed replay map)
    };

} // namespace ios
