cmake_minimum_required(VERSION 3.22)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    if (CMAKE_HOST_UNIX)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>$<$<CONFIG:RelWithDebInfo,Nightly>:ProgramDatabase>")
    else ()
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:EditAndContinue>$<$<CONFIG:RelWithDebInfo,Nightly>:ProgramDatabase>")
    endif ()
endif ()

project(ma4)

if (MSVC AND CMAKE_HOST_UNIX AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE INTERNAL "")
    set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_LIBRARIES OFF)
    set(CMAKE_C_COMPILE_OPTIONS_USE_PCH
            -Yuz:<PCH_HEADER> -Fpz:<PCH_FILE> -FIz:<PCH_HEADER>)
    set(CMAKE_C_COMPILE_OPTIONS_CREATE_PCH
            -Ycz:<PCH_HEADER> -Fpz:<PCH_FILE> -FIz:<PCH_HEADER>)
    set(CMAKE_CXX_COMPILE_OPTIONS_USE_PCH
            -Yuz:<PCH_HEADER> -Fpz:<PCH_FILE> -FIz:<PCH_HEADER>)
    set(CMAKE_CXX_COMPILE_OPTIONS_CREATE_PCH
            -Ycz:<PCH_HEADER> -Fpz:<PCH_FILE> -FIz:<PCH_HEADER>)
endif ()

set(MA4_BUILD_TIMESTAMP "" CACHE STRING
        "Optional fixed ma4 build timestamp in YYYY-MM-DD_HH-mm-ssZ UTC format")
if (MA4_BUILD_TIMESTAMP AND NOT MA4_BUILD_TIMESTAMP MATCHES
        "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]_[0-9][0-9]-[0-9][0-9]-[0-9][0-9]Z$")
    message(FATAL_ERROR
            "MA4_BUILD_TIMESTAMP must use YYYY-MM-DD_HH-mm-ssZ UTC+0 format; got '${MA4_BUILD_TIMESTAMP}'")
endif ()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel;Nightly" CACHE STRING "" FORCE)

# Nightly keeps RelWithDebInfo-style compilation so we get usable symbols, but
# links like a shipped binary so the DLL doesn't become a My 600-Lb. Life contestant
set(CMAKE_C_FLAGS_NIGHTLY "${CMAKE_C_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_NIGHTLY "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_NIGHTLY "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_NIGHTLY "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

if (NOT MSVC)
    message(FATAL_ERROR "Compiler unsupported, must use MSVC")
endif()

# MSVC runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Add all sources in src/* and deps/*
file(GLOB_RECURSE ma4_SRC CONFIGURE_DEPENDS
        "src/*.h"
        "src/*.cpp"
        "deps/*.h"
        "deps/*.cpp"
)

# Assets
function(ADD_RESOURCES out_var)
    set(result)
    foreach(in_f ${ARGN})
        file(RELATIVE_PATH src_f ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${in_f})
        set(out_f "${PROJECT_BINARY_DIR}/${in_f}.o")

        add_custom_command(
                OUTPUT ${out_f}
                COMMAND ld.exe -r -b binary -o ${out_f} ${src_f}
                DEPENDS ${in_f}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Building object ${out_f}"
                VERBATIM
        )

        list(APPEND result ${out_f})
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Add all files in assets/ as resources
file(GLOB_RECURSE RESOURCE_FILES
        RELATIVE ${CMAKE_CURRENT_LIST_DIR}
        CONFIGURE_DEPENDS
        "assets/*"
)

add_resources(EMBEDDED_RESOURCES ${RESOURCE_FILES})

set(MA4_BUILD_TIMESTAMP_SOURCE
        "${PROJECT_BINARY_DIR}/generated/ma4BuildTimestamp.cpp")
set_source_files_properties("${MA4_BUILD_TIMESTAMP_SOURCE}" PROPERTIES GENERATED TRUE)

add_custom_target(ma4BuildTimestampGenerator
        COMMAND ${CMAKE_COMMAND}
        "-DMA4_BUILD_TIMESTAMP=${MA4_BUILD_TIMESTAMP}"
        "-DOUTPUT_FILE=${MA4_BUILD_TIMESTAMP_SOURCE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildTimestamp.cmake"
        BYPRODUCTS "${MA4_BUILD_TIMESTAMP_SOURCE}"
        COMMENT "Refreshing ma4 UTC build timestamp"
        VERBATIM
)

add_library(ma4 SHARED
        ${ma4_SRC}
        ${EMBEDDED_RESOURCES}
        "${MA4_BUILD_TIMESTAMP_SOURCE}"
)
add_dependencies(ma4 ma4BuildTimestampGenerator)

# Final artifact names. Keep these in CMake so the DLL CodeView record and
# linker-generated PDB name agree, **do not rename the DLL/PDB after linking!!**
set_target_properties(ma4 PROPERTIES
        OUTPUT_NAME_RELEASE "ma4"
        PDB_NAME_RELEASE "ma4"
        OUTPUT_NAME_RELWITHDEBINFO "ma4"
        PDB_NAME_RELWITHDEBINFO "ma4"
        OUTPUT_NAME_DEBUG "ma4Debug"
        PDB_NAME_DEBUG "ma4Debug"
        OUTPUT_NAME_NIGHTLY "ma4Nightly"
        PDB_NAME_NIGHTLY "ma4Nightly"
)

# C++ standard
set_property(TARGET ma4 PROPERTY CXX_STANDARD 23)
set_property(TARGET ma4 PROPERTY CXX_STANDARD_REQUIRED ON)

# Compile definitions
target_compile_definitions(ma4 PRIVATE
        _UNICODE
        UNICODE
        _HAS_STATIC_RTTI=0

        $<$<CONFIG:Debug>:MA4_DEBUG>
        $<$<CONFIG:Debug>:MA4_CRASH_REPORTING>
        $<$<CONFIG:Release>:MA4_RELEASE>
        $<$<CONFIG:Release>:MA4_CRASH_REPORTING>
        $<$<CONFIG:RelWithDebInfo>:MA4_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:MA4_CRASH_REPORTING>
        $<$<CONFIG:MinSizeRel>:MA4_RELEASE>
        $<$<CONFIG:Nightly>:MA4_NIGHTLY>
        $<$<CONFIG:Nightly>:MA4_CRASH_REPORTING>
)

# Compile options
target_compile_options(ma4 PRIVATE
        /EHsc
        /GR-
        /W4
)

target_link_options(ma4 PRIVATE
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>

        $<$<CONFIG:Nightly>:/DEBUG:FULL>
        $<$<CONFIG:Nightly>:/INCREMENTAL:NO>
        $<$<CONFIG:Nightly>:/OPT:REF>
        $<$<CONFIG:Nightly>:/OPT:NOICF>
)

target_include_directories(ma4 PRIVATE
        src
        deps
)

# Link libraries
target_link_libraries(ma4 PRIVATE
        # Native networking, hashing, and URI launching
        Winhttp.lib
        Bcrypt.lib
        Shell32.lib
        Shlwapi.lib

        # Windows 10 fallback file picker
        Comdlg32.lib

        # Needed for getting app version
        Version.lib

        # Needed for DirectX hooking and Direct2D rendering
        d3d11
        d3d12
        dxguid
        dxgi

        # UI rendering
        d2d1

        # Text rendering
        dwrite

        # ImageNtHeader for mnemosyne
        Dbghelp

        # PlaySound for scripting
        winmm
)

add_subdirectory(src)
add_subdirectory(deps)
