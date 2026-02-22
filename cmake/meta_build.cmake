set(FIREBIRD_QT_VERSION "6.10.1" CACHE STRING "Pinned Qt version used by bootstrap/docker builders")
set(FIREBIRD_BUILD_ROOT "${CMAKE_SOURCE_DIR}/.build" CACHE PATH "Root build directory for all platform outputs")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_firebird_default_targets "desktop;android;ios;web;webqt")
else()
    set(_firebird_default_targets "desktop;android;web;webqt")
endif()
set(FIREBIRD_TARGETS "${_firebird_default_targets}" CACHE STRING "Semicolon/comma separated targets: desktop;android;ios;web;webqt")
set(FIREBIRD_MAKE_JOBS "8" CACHE STRING "Parallel jobs passed to make-like tools when supported")
option(FIREBIRD_ENABLE_CI_FALLBACK "Use GitHub Actions fallback when a local build path is unavailable" OFF)
set(FIREBIRD_CI_REF "master" CACHE STRING "Git ref used when dispatching CI fallback workflows")
set(FIREBIRD_CI_REPO "" CACHE STRING "Optional gh repo override (owner/repo)")
set(FIREBIRD_DOCKER_LINUX_IMAGE "firebird-builder-linux:qt6.10.1" CACHE STRING "Docker image for Linux desktop cross-builds")
set(FIREBIRD_DOCKER_WINDOWS_IMAGE "firebird-builder-windows:qt6.10.1" CACHE STRING "Docker image for Windows desktop cross-builds")

string(REPLACE "," ";" FIREBIRD_TARGETS "${FIREBIRD_TARGETS}")

set(_valid_targets desktop android ios web webqt)
set(_selected_targets "")
foreach(_target IN LISTS FIREBIRD_TARGETS)
    string(STRIP "${_target}" _target_trimmed)
    if(_target_trimmed STREQUAL "")
        continue()
    endif()
    list(FIND _valid_targets "${_target_trimmed}" _target_index)
    if(_target_index EQUAL -1)
        message(FATAL_ERROR "Unsupported FIREBIRD_TARGETS entry: '${_target_trimmed}'. Valid values: desktop;android;ios;web;webqt")
    endif()
    list(APPEND _selected_targets "${_target_trimmed}")
endforeach()

if(NOT _selected_targets)
    message(FATAL_ERROR "FIREBIRD_TARGETS resolved to an empty set.")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_host_desktop_platform macos)
    set(_desktop_platforms macos linux windows)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_host_desktop_platform linux)
    set(_desktop_platforms linux windows)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_host_desktop_platform windows)
    set(_desktop_platforms windows linux)
else()
    set(_host_desktop_platform unknown)
    set(_desktop_platforms)
endif()

find_program(_ninja_executable NAMES ninja)
if(_ninja_executable)
    set(_subbuild_generator Ninja)
else()
    set(_subbuild_generator "")
endif()

find_program(_qmake_executable NAMES qmake qmake6)
find_program(_system_qmake_executable NAMES qmake6 qmake PATHS /opt/homebrew/bin /usr/local/bin /usr/bin)
if(_system_qmake_executable)
    set(_host_qmake_executable "${_system_qmake_executable}")
else()
    set(_host_qmake_executable "${_qmake_executable}")
endif()
find_program(_make_executable NAMES make mingw32-make nmake)
find_program(_xcodebuild_executable NAMES xcodebuild)
find_program(_emcc_executable NAMES emcc)
set(_emsdk_root "$ENV{EMSDK}")
if(NOT _emsdk_root AND EXISTS "${CMAKE_SOURCE_DIR}/.build/emsdk/.emscripten")
    set(_emsdk_root "${CMAKE_SOURCE_DIR}/.build/emsdk")
endif()
if(_emsdk_root AND EXISTS "${_emsdk_root}/upstream/emscripten/cache")
    set(_webqt_em_cache "${_emsdk_root}/upstream/emscripten/cache")
else()
    set(_webqt_em_cache "${FIREBIRD_BUILD_ROOT}/emscripten-cache")
endif()
set(_emscripten_toolchain_file "")
if(_emsdk_root AND EXISTS "${_emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    set(_emscripten_toolchain_file "${_emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
endif()
if(NOT _emscripten_toolchain_file AND _emcc_executable)
    get_filename_component(_emcc_realpath "${_emcc_executable}" REALPATH)
    get_filename_component(_emcc_dir "${_emcc_realpath}" DIRECTORY)
    set(_emscripten_toolchain_candidates
        "${_emcc_dir}/cmake/Modules/Platform/Emscripten.cmake"
        "${_emcc_dir}/../libexec/cmake/Modules/Platform/Emscripten.cmake"
        "${_emcc_dir}/../upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    )
    foreach(_candidate IN LISTS _emscripten_toolchain_candidates)
        if(EXISTS "${_candidate}")
            set(_emscripten_toolchain_file "${_candidate}")
            break()
        endif()
    endforeach()
endif()
set(_docker_executable "")
foreach(_docker_candidate "/opt/homebrew/bin/docker" "/usr/local/bin/docker" "/usr/bin/docker")
    if(EXISTS "${_docker_candidate}")
        set(_docker_executable "${_docker_candidate}")
        break()
    endif()
endforeach()
if(NOT _docker_executable)
    find_program(_docker_executable NAMES docker)
endif()

set(_local_qt_root "${CMAKE_SOURCE_DIR}/.build/qt")
set(_qmake_android_executable "")
set(_qmake_ios_executable "")
set(_qt_web_cmake_executable "")
set(_qt_web_host_path "")
set(_host_qmake_xspec "")
set(_host_qmake_xspec_lower "")
if(_qmake_executable)
    execute_process(
        COMMAND ${_qmake_executable} -query QMAKE_XSPEC
        OUTPUT_VARIABLE _host_qmake_xspec
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(TOLOWER "${_host_qmake_xspec}" _host_qmake_xspec_lower)
endif()
if(EXISTS "${_local_qt_root}")
    file(GLOB _qmake_android_candidates "${_local_qt_root}/*/android_arm64_v8a/bin/qmake6")
    if(NOT _qmake_android_candidates)
        file(GLOB _qmake_android_candidates "${_local_qt_root}/*/android_arm64_v8a/bin/qmake")
    endif()
    list(LENGTH _qmake_android_candidates _qmake_android_count)
    if(_qmake_android_count GREATER 0)
        list(SORT _qmake_android_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _qmake_android_candidates 0 _qmake_android_executable)
    endif()

    file(GLOB _qmake_ios_candidates "${_local_qt_root}/*/ios/bin/qmake6")
    if(NOT _qmake_ios_candidates)
        file(GLOB _qmake_ios_candidates "${_local_qt_root}/*/ios/bin/qmake")
    endif()
    list(LENGTH _qmake_ios_candidates _qmake_ios_count)
    if(_qmake_ios_count GREATER 0)
        list(SORT _qmake_ios_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _qmake_ios_candidates 0 _qmake_ios_executable)
    endif()

    file(GLOB _qt_web_cmake_candidates "${_local_qt_root}/*/wasm*/bin/qt-cmake")
    if(NOT _qt_web_cmake_candidates)
        file(GLOB _qt_web_cmake_candidates "${_local_qt_root}/*/wasm*/bin/qt-cmake.bat")
    endif()
    list(LENGTH _qt_web_cmake_candidates _qt_web_cmake_count)
    if(_qt_web_cmake_count GREATER 0)
        list(SORT _qt_web_cmake_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _qt_web_cmake_candidates 0 _qt_web_cmake_executable)
    endif()
endif()
if(NOT _qmake_android_executable AND _qmake_executable AND _host_qmake_xspec_lower MATCHES "android")
    set(_qmake_android_executable "${_qmake_executable}")
endif()
if(NOT _qmake_ios_executable AND _qmake_executable AND _host_qmake_xspec_lower MATCHES "ios")
    set(_qmake_ios_executable "${_qmake_executable}")
endif()
if(_qt_web_cmake_executable)
    get_filename_component(_qt_web_bin_dir "${_qt_web_cmake_executable}" DIRECTORY)
    get_filename_component(_qt_web_kit_dir "${_qt_web_bin_dir}" DIRECTORY)
    get_filename_component(_qt_web_version_root "${_qt_web_kit_dir}" DIRECTORY)
    foreach(_qt_host_candidate IN ITEMS macos gcc_64 clang_64 win64_msvc2022_64 win64_mingw mingw_64)
        if(EXISTS "${_qt_web_version_root}/${_qt_host_candidate}")
            set(_qt_web_host_path "${_qt_web_version_root}/${_qt_host_candidate}")
            break()
        endif()
    endforeach()
endif()

function(_firebird_validate_local_qt_host_tools target_qmake out_ok out_reason)
    set(${out_ok} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)

    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        return()
    endif()
    if(NOT target_qmake)
        return()
    endif()

    get_filename_component(_target_qmake_dir "${target_qmake}" DIRECTORY)
    get_filename_component(_target_kit_dir "${_target_qmake_dir}" DIRECTORY)
    get_filename_component(_target_qt_root "${_target_kit_dir}" DIRECTORY)
    set(_local_host_libexec "${_target_qt_root}/macos/libexec")
    if(NOT EXISTS "${_local_host_libexec}")
        return()
    endif()

    foreach(_tool IN ITEMS rcc uic moc)
        set(_local_tool "${_local_host_libexec}/${_tool}")
        if(NOT EXISTS "${_local_tool}")
            set(${out_ok} FALSE PARENT_SCOPE)
            set(${out_reason} "Missing Qt host tool: ${_local_tool}" PARENT_SCOPE)
            return()
        endif()
        execute_process(
            COMMAND ${_local_tool} -v
            RESULT_VARIABLE _tool_rc
            OUTPUT_VARIABLE _tool_out
            ERROR_VARIABLE _tool_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _tool_rc EQUAL 0)
            set(${out_ok} FALSE PARENT_SCOPE)
            set(${out_reason} "Qt host tool failed to execute: ${_local_tool}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    string(REGEX MATCH "/qt/([0-9]+\\.[0-9]+\\.[0-9]+)/" _qt_version_match "${_target_qt_root}/")
    set(_qt_expected_version "${CMAKE_MATCH_1}")
    if(_qt_expected_version)
        execute_process(
            COMMAND ${_local_host_libexec}/moc -v
            OUTPUT_VARIABLE _moc_out
            ERROR_VARIABLE _moc_err
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)" _moc_version_match "${_moc_out} ${_moc_err}")
        set(_moc_version "${CMAKE_MATCH_1}")
        if(_moc_version AND NOT _moc_version STREQUAL _qt_expected_version)
            set(${out_ok} FALSE PARENT_SCOPE)
            set(${out_reason} "Qt host moc version mismatch (expected ${_qt_expected_version}, got ${_moc_version})" PARENT_SCOPE)
            return()
        endif()
    endif()

endfunction()

function(_firebird_ndk_is_valid ndk_root out_var)
    if(NOT ndk_root OR NOT EXISTS "${ndk_root}")
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()
    file(GLOB _ndk_clang_candidates "${ndk_root}/toolchains/llvm/prebuilt/*/bin/clang")
    if(_ndk_clang_candidates)
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

set(_android_ndk_root "$ENV{ANDROID_NDK_ROOT}")
if(NOT _android_ndk_root)
    set(_android_ndk_root "$ENV{ANDROID_NDK_HOME}")
endif()
_firebird_ndk_is_valid("${_android_ndk_root}" _android_ndk_ok)
if(NOT _android_ndk_ok)
    set(_android_ndk_root "")
endif()
if(NOT _android_ndk_root)
    set(_android_sdk_roots
        "$ENV{ANDROID_SDK_ROOT}"
        "$ENV{ANDROID_HOME}"
        "$ENV{HOME}/Library/Android/sdk"
        "$ENV{HOME}/Android/Sdk"
        "/opt/homebrew/share/android-commandlinetools"
        "/usr/local/share/android-commandlinetools"
    )
    foreach(_sdk_root IN LISTS _android_sdk_roots)
        if(NOT _sdk_root)
            continue()
        endif()
        if(EXISTS "${_sdk_root}/ndk")
            file(GLOB _ndk_candidates LIST_DIRECTORIES true "${_sdk_root}/ndk/*")
            if(_ndk_candidates)
                list(SORT _ndk_candidates COMPARE NATURAL ORDER DESCENDING)
                foreach(_ndk_candidate IN LISTS _ndk_candidates)
                    _firebird_ndk_is_valid("${_ndk_candidate}" _ndk_candidate_ok)
                    if(_ndk_candidate_ok)
                        set(_android_ndk_root "${_ndk_candidate}")
                        break()
                    endif()
                endforeach()
                if(_android_ndk_root)
                    break()
                endif()
            endif()
        endif()
    endforeach()
endif()

if(_make_executable MATCHES "nmake(\\.exe)?$")
    set(_make_parallel_args "")
else()
    set(_make_parallel_args "-j${FIREBIRD_MAKE_JOBS}")
endif()

if(FIREBIRD_ENABLE_CI_FALLBACK)
    find_program(_gh_executable NAMES gh)
    if(NOT _gh_executable)
        message(FATAL_ERROR "FIREBIRD_ENABLE_CI_FALLBACK=ON, but GitHub CLI 'gh' was not found in PATH.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

function(_firebird_qmake_command qmake_path out_var)
    set(_cmd ${qmake_path})
    get_filename_component(_qmake_dir "${qmake_path}" DIRECTORY)
    set(_qtconf "${_qmake_dir}/target_qt.conf")
    if(EXISTS "${_qtconf}")
        get_filename_component(_kit_dir "${_qmake_dir}" DIRECTORY)
        get_filename_component(_qt_root "${_kit_dir}" DIRECTORY)
        set(_host_qmake "${_qt_root}/macos/bin/qmake6")
        if(NOT EXISTS "${_host_qmake}")
            set(_host_qmake "${_qt_root}/macos/bin/qmake")
        endif()
        if(EXISTS "${_host_qmake}")
            set(_cmd ${_host_qmake} -qtconf ${_qtconf})
        endif()
    endif()
    set(${out_var} ${_cmd} PARENT_SCOPE)
endfunction()

function(_firebird_add_ci_target target_name workflow_filename)
    set(_cmd
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/build/ci_build.py
        --workflow ${workflow_filename}
        --ref ${FIREBIRD_CI_REF}
        --artifact-dir ${FIREBIRD_BUILD_ROOT}/artifacts/${target_name}
    )
    if(FIREBIRD_CI_REPO)
        list(APPEND _cmd --repo ${FIREBIRD_CI_REPO})
    endif()
    add_custom_target(${target_name}
        COMMAND ${_cmd}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Dispatching CI workflow ${workflow_filename} for target ${target_name}"
    )
endfunction()

function(_firebird_add_local_desktop_target platform out_target)
    set(_target_name firebird-build-desktop-${platform})
    set(_build_dir ${FIREBIRD_BUILD_ROOT}/${platform}-desktop)
    set(_desktop_configure_cmd
        ${CMAKE_COMMAND}
        -S ${CMAKE_SOURCE_DIR}
        -B ${_build_dir}
    )
    if(_subbuild_generator)
        list(APPEND _desktop_configure_cmd -G ${_subbuild_generator})
    endif()

    add_custom_target(${_target_name}
        COMMAND ${_desktop_configure_cmd}
        COMMAND ${CMAKE_COMMAND} --build ${_build_dir} --parallel
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Configuring and building desktop target (${platform})"
    )
    set(${out_target} ${_target_name} PARENT_SCOPE)
endfunction()

function(_firebird_add_docker_desktop_target platform image_tag out_target)
    if(NOT _docker_executable)
        message(FATAL_ERROR "Desktop target '${platform}' requires Docker, but docker is not available.")
    endif()
    set(_target_name firebird-build-desktop-${platform})

    if(platform STREQUAL "linux")
        set(_container_configure_cmd
            cmake -S . -B .build/linux-desktop -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_PREFIX_PATH=/opt/qt/host
            -DFIREBIRD_ENABLE_KDDOCKWIDGETS=OFF
        )
        set(_container_build_cmd
            cmake --build .build/linux-desktop --parallel ${FIREBIRD_MAKE_JOBS}
        )
    elseif(platform STREQUAL "windows")
        set(_container_configure_cmd
            cmake -S . -B .build/windows-desktop -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_TOOLCHAIN_FILE=/opt/firebird/toolchains/mingw64-toolchain.cmake
            -DCMAKE_PREFIX_PATH=/opt/qt/windows
            -DQT_HOST_PATH=/opt/qt/host
            -DFIREBIRD_ENABLE_KDDOCKWIDGETS=OFF
        )
        set(_container_build_cmd
            cmake --build .build/windows-desktop --parallel ${FIREBIRD_MAKE_JOBS}
        )
    else()
        message(FATAL_ERROR "Unsupported Docker desktop platform: ${platform}")
    endif()

    add_custom_target(${_target_name}
        COMMAND ${_docker_executable} run --rm
            -v ${CMAKE_SOURCE_DIR}:/src
            -w /src
            ${image_tag}
            ${_container_configure_cmd}
        COMMAND ${_docker_executable} run --rm
            -v ${CMAKE_SOURCE_DIR}:/src
            -w /src
            ${image_tag}
            ${_container_build_cmd}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Building desktop target (${platform}) in Docker (${image_tag})"
    )
    set(${out_target} ${_target_name} PARENT_SCOPE)
endfunction()

set(_platform_build_targets "")

if(desktop IN_LIST _selected_targets)
    if(NOT _desktop_platforms)
        message(FATAL_ERROR "Desktop target requested, but host platform '${CMAKE_HOST_SYSTEM_NAME}' is unsupported.")
    endif()

    set(_desktop_targets "")
    foreach(_desktop_platform IN LISTS _desktop_platforms)
        if(_desktop_platform STREQUAL _host_desktop_platform)
            _firebird_add_local_desktop_target(${_desktop_platform} _desktop_target_name)
            list(APPEND _desktop_targets ${_desktop_target_name})
        elseif(_desktop_platform STREQUAL "linux")
            if(_docker_executable)
                _firebird_add_docker_desktop_target(linux ${FIREBIRD_DOCKER_LINUX_IMAGE} _desktop_target_name)
                list(APPEND _desktop_targets ${_desktop_target_name})
            elseif(FIREBIRD_ENABLE_CI_FALLBACK)
                _firebird_add_ci_target(firebird-build-desktop-linux-ci main.yml)
                list(APPEND _desktop_targets firebird-build-desktop-linux-ci)
            else()
                message(FATAL_ERROR "Linux desktop target requires Docker on host '${CMAKE_HOST_SYSTEM_NAME}'.")
            endif()
        elseif(_desktop_platform STREQUAL "windows")
            if(_docker_executable)
                _firebird_add_docker_desktop_target(windows ${FIREBIRD_DOCKER_WINDOWS_IMAGE} _desktop_target_name)
                list(APPEND _desktop_targets ${_desktop_target_name})
            elseif(FIREBIRD_ENABLE_CI_FALLBACK)
                _firebird_add_ci_target(firebird-build-desktop-windows-ci windows.yml)
                list(APPEND _desktop_targets firebird-build-desktop-windows-ci)
            else()
                message(FATAL_ERROR "Windows desktop target requires Docker on host '${CMAKE_HOST_SYSTEM_NAME}'.")
            endif()
        elseif(_desktop_platform STREQUAL "macos")
            if(FIREBIRD_ENABLE_CI_FALLBACK)
                _firebird_add_ci_target(firebird-build-desktop-macos-ci macOS.yml)
                list(APPEND _desktop_targets firebird-build-desktop-macos-ci)
            else()
                message(FATAL_ERROR "macOS desktop target is only local on macOS hosts. Enable CI fallback or switch host.")
            endif()
        endif()
    endforeach()

    add_custom_target(firebird-build-desktop DEPENDS ${_desktop_targets})
    list(APPEND _platform_build_targets firebird-build-desktop)
endif()

if(android IN_LIST _selected_targets)
    if(_qmake_android_executable AND _make_executable AND _android_ndk_root)
        _firebird_validate_local_qt_host_tools("${_qmake_android_executable}" _android_qt_tools_ok _android_qt_tools_reason)
        if(NOT _android_qt_tools_ok)
            message(FATAL_ERROR "Android target requested, but local Qt host tools are not runnable. ${_android_qt_tools_reason}. Reinstall matching Qt kits with: python3 tools/build/bootstrap.py --target android --bootstrap-qt --yes")
        endif()
        _firebird_qmake_command("${_qmake_android_executable}" _android_qmake_cmd)
        add_custom_target(firebird-build-android
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREBIRD_BUILD_ROOT}/android
            COMMAND ${CMAKE_COMMAND} -E chdir ${FIREBIRD_BUILD_ROOT}/android
                    ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${_android_ndk_root} ANDROID_NDK_HOME=${_android_ndk_root} ${_android_qmake_cmd} ../../firebird.pro CONFIG+=release ANDROID_ABIS=arm64-v8a
            COMMAND ${CMAKE_COMMAND} -E chdir ${FIREBIRD_BUILD_ROOT}/android
                    ${_make_executable} ${_make_parallel_args}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "Configuring and building Android target (local)"
        )
    elseif(FIREBIRD_ENABLE_CI_FALLBACK)
        _firebird_add_ci_target(firebird-build-android android.yml)
    else()
        message(FATAL_ERROR "Android target requested, but local toolchain is incomplete (need Qt Android qmake and ANDROID_NDK_ROOT).")
    endif()
    list(APPEND _platform_build_targets firebird-build-android)
endif()

if(ios IN_LIST _selected_targets)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND _qmake_ios_executable AND _xcodebuild_executable AND _make_executable)
        _firebird_validate_local_qt_host_tools("${_qmake_ios_executable}" _ios_qt_tools_ok _ios_qt_tools_reason)
        if(NOT _ios_qt_tools_ok)
            message(FATAL_ERROR "iOS target requested, but local Qt host tools are not runnable. ${_ios_qt_tools_reason}. Reinstall matching Qt kits with: python3 tools/build/bootstrap.py --target ios --bootstrap-qt --yes")
        endif()
        _firebird_qmake_command("${_qmake_ios_executable}" _ios_qmake_cmd)
        add_custom_target(firebird-build-ios
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREBIRD_BUILD_ROOT}/ios
            COMMAND ${CMAKE_COMMAND} -E chdir ${FIREBIRD_BUILD_ROOT}/ios
                    ${_ios_qmake_cmd} ../../firebird.pro -spec macx-ios-clang CONFIG+=release
            COMMAND ${CMAKE_COMMAND} -E chdir ${FIREBIRD_BUILD_ROOT}/ios
                    ${_make_executable} ${_make_parallel_args}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "Configuring and building iOS target (local)"
        )
    elseif(FIREBIRD_ENABLE_CI_FALLBACK)
        _firebird_add_ci_target(firebird-build-ios ios.yml)
    else()
        message(FATAL_ERROR "iOS target requested, but Qt iOS qmake was not found at .build/qt/*/ios/bin/qmake. Run: python3 tools/build/bootstrap.py --target ios --bootstrap-qt --yes")
    endif()
    list(APPEND _platform_build_targets firebird-build-ios)
endif()

if(web IN_LIST _selected_targets)
    if(_make_executable AND _emcc_executable)
        add_custom_target(firebird-build-web
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREBIRD_BUILD_ROOT}/emscripten-cache
            COMMAND ${CMAKE_COMMAND} -E env EM_CACHE=${FIREBIRD_BUILD_ROOT}/emscripten-cache
                    ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/emscripten
                    ${_make_executable} BUILD_DIR=../.build/web ${_make_parallel_args}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "Building web target (local)"
        )
    elseif(FIREBIRD_ENABLE_CI_FALLBACK)
        _firebird_add_ci_target(firebird-build-web web.yml)
    else()
        message(FATAL_ERROR "Web target requested, but local Emscripten toolchain is incomplete.")
    endif()
    list(APPEND _platform_build_targets firebird-build-web)
endif()

if(webqt IN_LIST _selected_targets)
    if(_qt_web_cmake_executable)
        set(_webqt_emcc "${_emcc_executable}")
        if(_emsdk_root AND EXISTS "${_emsdk_root}/upstream/emscripten/emcc")
            set(_webqt_emcc "${_emsdk_root}/upstream/emscripten/emcc")
        elseif(_emsdk_root AND EXISTS "${_emsdk_root}/upstream/emscripten/emcc.bat")
            set(_webqt_emcc "${_emsdk_root}/upstream/emscripten/emcc.bat")
        endif()

        set(_webqt_env_cmd
            ${CMAKE_COMMAND} -E env
            EM_CACHE=${_webqt_em_cache}
        )
        if(_emsdk_root)
            list(APPEND _webqt_env_cmd EMSDK=${_emsdk_root})
            if(EXISTS "${_emsdk_root}/.emscripten")
                list(APPEND _webqt_env_cmd EM_CONFIG=${_emsdk_root}/.emscripten)
            endif()
        endif()

        set(_webqt_configure_cmd
            ${_qt_web_cmake_executable}
            ${CMAKE_SOURCE_DIR}/web-qt
            -DCMAKE_BUILD_TYPE=Release
            -DFIREBIRD_KDDOCKWIDGETS_PREFIX=${CMAKE_SOURCE_DIR}/.build/deps/kddockwidgets-2.4-wasm
        )
        if(_qt_web_host_path)
            list(APPEND _webqt_configure_cmd -DQT_HOST_PATH=${_qt_web_host_path})
        endif()
        if(_emscripten_toolchain_file)
            list(APPEND _webqt_configure_cmd -DQT_CHAINLOAD_TOOLCHAIN_FILE=${_emscripten_toolchain_file})
        endif()
        if(_ninja_executable)
            list(APPEND _webqt_configure_cmd -DCMAKE_MAKE_PROGRAM=${_ninja_executable})
        endif()
        if(_subbuild_generator)
            list(APPEND _webqt_configure_cmd -G ${_subbuild_generator})
        endif()

        add_custom_target(firebird-build-web-qt
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREBIRD_BUILD_ROOT}/web-qt
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_webqt_em_cache}
            COMMAND ${CMAKE_COMMAND} -E touch ${FIREBIRD_BUILD_ROOT}/web-qt/.emcc-zlib-smoke.c
            COMMAND ${_webqt_env_cmd} ${_webqt_emcc}
                    -x c -c ${FIREBIRD_BUILD_ROOT}/web-qt/.emcc-zlib-smoke.c
                    -sUSE_ZLIB=1
                    -o ${FIREBIRD_BUILD_ROOT}/web-qt/.emcc-zlib-smoke.o
            COMMAND ${CMAKE_COMMAND} -E rm -f ${FIREBIRD_BUILD_ROOT}/web-qt/CMakeCache.txt
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${FIREBIRD_BUILD_ROOT}/web-qt/CMakeFiles
            COMMAND ${CMAKE_COMMAND} -E chdir ${FIREBIRD_BUILD_ROOT}/web-qt
                    ${_webqt_env_cmd} ${_webqt_configure_cmd}
            COMMAND ${_webqt_env_cmd}
                    ${CMAKE_COMMAND} --build ${FIREBIRD_BUILD_ROOT}/web-qt --parallel
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "Building web Qt (WASM) target (local)"
        )
    else()
        message(FATAL_ERROR "Web Qt target requested, but Qt wasm kit was not found at .build/qt/*/wasm*/bin/qt-cmake. Run: python3 tools/build/bootstrap.py --target webqt --bootstrap-qt --bootstrap-kddockwidgets --yes")
    endif()
    list(APPEND _platform_build_targets firebird-build-web-qt)
endif()

add_custom_target(firebird-build-all DEPENDS ${_platform_build_targets})

message(STATUS "Firebird meta-build enabled")
message(STATUS "Selected targets: ${_selected_targets}")
message(STATUS "Host desktop platform: ${_host_desktop_platform}")
message(STATUS "Desktop targets on this host: ${_desktop_platforms}")
message(STATUS "CI fallback enabled: ${FIREBIRD_ENABLE_CI_FALLBACK}")
message(STATUS "Build root: ${FIREBIRD_BUILD_ROOT}")
message(STATUS "Aggregate target: firebird-build-all")
