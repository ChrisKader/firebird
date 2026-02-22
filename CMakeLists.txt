cmake_minimum_required(VERSION 3.16)
project(firebird-emu VERSION 1.0 LANGUAGES C CXX ASM)

set(FIREBIRD_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

option(FIREBIRD_META_BUILD "Enable meta-build driver that orchestrates platform builds" OFF)
if(FIREBIRD_META_BUILD)
    include(cmake/meta_build.cmake)
    return()
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickWidgets Widgets)
find_package(Qt6 QUIET COMPONENTS WidgetsPrivate)
find_package(ZLIB REQUIRED)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
endif()

option(FIREBIRD_ENABLE_KDDOCKWIDGETS "Enable KDDockWidgets docking backend" ON)
set(
    FIREBIRD_KDDOCKWIDGETS_PREFIX
    "${FIREBIRD_SOURCE_DIR}/.build/deps/kddockwidgets-2.4"
    CACHE PATH
    "Optional local KDDockWidgets install prefix"
)

qt_add_executable(firebird-emu WIN32 MACOSX_BUNDLE
    # Core emulator
    core/cpu/arm_interpreter.cpp
    core/jit/armcode_bin.h
    core/jit/armsnippets.h
    core/jit/armsnippets_loader.c
    core/jit/asmcode.c core/jit/asmcode.h
    core/cpu/bitfield.h
    core/soc/casplus.c core/soc/casplus.h
    core/cpu/coproc.cpp
    core/cpu/cpu.cpp core/cpu/cpu.h
    core/cpu/cpudefs.h
    core/soc/cx2.cpp core/soc/cx2.h
    core/peripherals/cx2_peripherals.cpp
    core/debug/debug.cpp core/debug/debug.h
    core/debug/debug_cli.cpp
    core/debug/debug_remote.cpp
    core/debug/debug_api.cpp core/debug/debug_api.h
    core/debug/debug_api_peek.cpp
    core/crypto/des.c core/crypto/des.h
    core/disassembly/disasm.c core/disassembly/disasm.h
    core/emu.cpp core/emu.h
    core/storage/fieldparser.cpp core/storage/fieldparser.h
    core/storage/flash.cpp core/storage/flash.h
    core/storage/nand_fs.cpp core/storage/nand_fs.h
    core/debug/gdbstub.c core/debug/gdbstub.h
    core/gif.cpp core/gif.h
    core/peripherals/interrupt.c core/peripherals/interrupt.h
    core/peripherals/keypad.cpp core/peripherals/keypad.h
    core/peripherals/lcd.c core/peripherals/lcd.h
    core/peripherals/link.c core/peripherals/link.h
    core/memory/mem.c core/memory/mem.h
    core/peripherals/misc.c core/peripherals/misc.h
    core/memory/mmu.c core/memory/mmu.h
    core/debug/nspire_log_hook.cpp core/debug/nspire_log_hook.h
    core/os/os.h
    core/timing/schedule.c core/timing/schedule.h
    core/peripherals/serial.c
    core/crypto/sha256.c core/crypto/sha256.h
    core/cpu/thumb_interpreter.cpp
    core/cpu/translate.h
    core/usb/usb.c core/usb/usb.h
    core/usb/usb_cx2.cpp core/usb/usb_cx2.h
    core/usb/usb_cx2_state.cpp
    core/usb/usblink.c core/usb/usblink.h
    core/usb/usblink_cx2.cpp core/usb/usblink_cx2.h
    core/usb/usblink_queue.cpp core/usb/usblink_queue.h
    # Application infrastructure
    app/emuthread.cpp app/emuthread.h
    core/power/powercontrol.cpp core/power/powercontrol.h
    app/qmlbridge.cpp app/qmlbridge.h
    app/qmlbridge/settings.cpp
    app/qmlbridge/runtime.cpp
    ui/models/kitmodel.cpp ui/models/kitmodel.h
    # Shared UI components
    ui/docking/widgets/dockwidget.cpp ui/docking/widgets/dockwidget.h
    ui/docking/backend/dockbackend.cpp ui/docking/backend/dockbackend.h
    ui/docking/state/dockstate.h
    ui/docking/widgets/kdockwidget.cpp ui/docking/widgets/kdockwidget.h
    ui/theme/widgettheme.cpp ui/theme/widgettheme.h
    ui/theme/materialicons.cpp ui/theme/materialicons.h
    ui/screen/lcdwidget.cpp ui/screen/lcdwidget.h
    ui/screen/framebuffer.cpp ui/screen/framebuffer.h
    ui/input/keypadbridge.cpp ui/input/keypadbridge.h
    ui/input/keymap.h
    ui/text/ansitextwriter.cpp ui/text/ansitextwriter.h
    # Debugger subsystem
    ui/docking/manager/dockmanager.cpp ui/docking/manager/dockmanager.h
    ui/docking/manager/debugdockregistration.cpp ui/docking/manager/debugdockregistration.h
    ui/docking/manager/dockmanager_state.cpp
    ui/widgets/disassembly/disassemblywidget.cpp ui/widgets/disassembly/disassemblywidget.h
    ui/widgets/disassembly/disassemblywidget_runtime.cpp
    ui/widgets/disassembly/dockregistration.cpp
    ui/widgets/registers/registerwidget.cpp ui/widgets/registers/registerwidget.h
    ui/widgets/registers/dockregistration.cpp
    ui/widgets/hexview/hexviewwidget.cpp ui/widgets/hexview/hexviewwidget.h
    ui/widgets/hexview/hexviewwidget_search.cpp
    ui/widgets/hexview/hexviewwidget_context.cpp
    ui/widgets/hexview/dockregistration.cpp
    ui/widgets/breakpoints/breakpointwidget.cpp ui/widgets/breakpoints/breakpointwidget.h
    ui/widgets/breakpoints/dockregistration.cpp
    ui/widgets/watchpoints/watchpointwidget.cpp ui/widgets/watchpoints/watchpointwidget.h
    ui/widgets/watchpoints/dockregistration.cpp
    ui/widgets/stack/stackwidget.cpp ui/widgets/stack/stackwidget.h
    ui/widgets/stack/dockregistration.cpp
    ui/widgets/portmonitor/portmonitorwidget.cpp ui/widgets/portmonitor/portmonitorwidget.h
    ui/widgets/portmonitor/dockregistration.cpp
    ui/widgets/keyhistory/keyhistorywidget.cpp ui/widgets/keyhistory/keyhistorywidget.h
    ui/widgets/keyhistory/dockregistration.cpp
    ui/widgets/console/consolewidget.cpp ui/widgets/console/consolewidget.h
    ui/widgets/console/dockregistration.cpp
    ui/widgets/console/state.cpp
    ui/widgets/memvisualizer/memoryvisualizerwidget.cpp ui/widgets/memvisualizer/memoryvisualizerwidget.h
    ui/widgets/memvisualizer/dockregistration.cpp
    ui/widgets/cyclecounter/cyclecounterwidget.cpp ui/widgets/cyclecounter/cyclecounterwidget.h
    ui/widgets/cyclecounter/dockregistration.cpp
    ui/widgets/timermonitor/timermonitorwidget.cpp ui/widgets/timermonitor/timermonitorwidget.h
    ui/widgets/timermonitor/dockregistration.cpp
    ui/widgets/lcdstate/lcdstatewidget.cpp ui/widgets/lcdstate/lcdstatewidget.h
    ui/widgets/lcdstate/dockregistration.cpp
    ui/widgets/gotodialog.cpp ui/widgets/gotodialog.h
    ui/widgets/nandbrowser/nandbrowserwidget.cpp ui/widgets/nandbrowser/nandbrowserwidget.h
    ui/widgets/nandbrowser/nandbrowserwidget_tree.cpp
    ui/widgets/nandbrowser/nandbrowserwidget_views.cpp
    ui/widgets/nandbrowser/nandbrowserwidget_io.cpp
    ui/widgets/nandbrowser/nandbrowserwidget_search.cpp
    ui/widgets/nandbrowser/nandfileeditor.cpp ui/widgets/nandbrowser/nandfileeditor.h
    ui/widgets/hwconfig/hwconfigwidget.cpp ui/widgets/hwconfig/hwconfigwidget.h
    ui/widgets/hwconfig/state.cpp
    ui/widgets/mmuviewer/mmuviewerwidget.cpp ui/widgets/mmuviewer/mmuviewerwidget.h
    ui/widgets/mmuviewer/dockregistration.cpp
    # Dialogs
    dialogs/fbaboutdialog.cpp dialogs/fbaboutdialog.h
    # File transfer
    transfer/usblinktreewidget.cpp transfer/usblinktreewidget.h
    # Main window (stays in root for AUTOUIC)
    main.cpp
    mainwindow.cpp mainwindow.h mainwindow.ui
    mainwindow/bootstrap.cpp
    mainwindow/bootstrap/startup.cpp
    mainwindow/layout_persistence.cpp
    mainwindow/docks.cpp
    mainwindow/docks/connectivity.cpp
    mainwindow/docks/overlay.cpp
    mainwindow/docks/setup.cpp
    mainwindow/docks/baseline.cpp
    mainwindow/docks/reset.cpp
    mainwindow/theme.cpp
    mainwindow/runtime.cpp
    mainwindow/runtime/actions.cpp
    mainwindow/runtime/window.cpp
)

# mainwindow.ui stays in the repo root; runtime module sources include the
# generated ui header but must not trigger local AUTOUIC lookup.
set_source_files_properties(mainwindow/bootstrap.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/bootstrap/startup.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/layout_persistence.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks/connectivity.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks/overlay.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks/setup.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks/baseline.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/docks/reset.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/runtime.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/runtime/actions.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/runtime/window.cpp PROPERTIES SKIP_AUTOUIC ON)
set_source_files_properties(mainwindow/theme.cpp PROPERTIES SKIP_AUTOUIC ON)
qt_add_qml_module(firebird-emu
    URI firebird_emu
    VERSION ${PROJECT_VERSION}
    QML_FILES
        qml/Keypad.qml
        qml/NAlphaButton.qml
        qml/NBigButton.qml
        qml/NButton.qml
        qml/NNumButton.qml
        qml/Touchpad.qml
        qml/NDualButton.qml
        qml/SecondaryLabel.qml
        qml/SvgPaths.qml
        qml/SvgIcon.qml
        qml/MobileUI.qml
        qml/SidebarButton.qml
        qml/ConfigPagesModel.qml
        qml/Firebird/UIComponents/PageDelegate.qml
        qml/Firebird/UIComponents/PageList.qml
        qml/FBConfigDialog.qml
        qml/ConfigPageDebug.qml
        qml/Firebird/UIComponents/ConfigPages.qml
        qml/ConfigPageKits.qml
        qml/Firebird/UIComponents/KitItem.qml
        qml/Firebird/UIComponents/KitList.qml
        qml/Firebird/UIComponents/FBLabel.qml
        qml/Firebird/UIComponents/FileSelect.qml
        qml/Firebird/UIComponents/FBLink.qml
        qml/ConfigPageEmulation.qml
        qml/ConfigPageFileTransfer.qml
        qml/Firebird/UIComponents/TextMetrics.qml
        qml/Firebird/UIComponents/Theme.qml
        qml/ScrollingKeypad.qml
        qml/Firebird/UIComponents/Toast.qml
        qml/MobileUIConfig.qml
        qml/MobileUIFront.qml
        qml/MobileUIDrawer.qml
        qml/DrawerButton.qml
        qml/Firebird/UIComponents/VerticalSwipeBar.qml
        qml/FlashDialog.qml
        qml/Firebird/UIComponents/IconButton.qml
        qml/KitRoles.js
    RESOURCES
        qml/Firebird/UIComponents/qmldir
    RESOURCE_PREFIX /qml
    NO_RESOURCE_TARGET_PATH
)

target_include_directories(firebird-emu PRIVATE
    ${FIREBIRD_SOURCE_DIR}
)

# Avoid shadowing libstdc++ <debug/...> headers with core/debug/debug.h while
# still allowing quoted includes like "emu.h" from non-core sources.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(firebird-emu PRIVATE
        -iquote${FIREBIRD_SOURCE_DIR}/core
        -iquote${FIREBIRD_SOURCE_DIR}/core/debug
    )
else()
    target_include_directories(firebird-emu PRIVATE
        ${FIREBIRD_SOURCE_DIR}/core
    )
endif()

target_compile_definitions(firebird-emu PRIVATE

    FB_VERSION=1.6
)

target_link_libraries(firebird-emu PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Quick
    Qt::QuickWidgets
    Qt::Widgets
    ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(firebird-emu PRIVATE
        /W4
        /Zi
    )
else()
    target_compile_options(firebird-emu PRIVATE
        -Wall
        -Wextra
        -g
    )
endif()

target_compile_definitions(firebird-emu PRIVATE
    QT_NO_CAST_FROM_ASCII
)

if(FIREBIRD_ENABLE_KDDOCKWIDGETS)
    set(_kddockwidgets_version 2.4.0)
    set(_kddockwidgets_local_prefix "${FIREBIRD_KDDOCKWIDGETS_PREFIX}")
    set(_kddockwidgets_local_config
        "${_kddockwidgets_local_prefix}/lib/cmake/KDDockWidgets-qt6/KDDockWidgets-qt6Config.cmake")
    if(EXISTS "${_kddockwidgets_local_config}")
        get_filename_component(_kddockwidgets_local_dir "${_kddockwidgets_local_config}" DIRECTORY)
        set(KDDockWidgets-qt6_DIR "${_kddockwidgets_local_dir}" CACHE PATH "Local KDDockWidgets-qt6 package directory" FORCE)
        list(PREPEND CMAKE_PREFIX_PATH "${_kddockwidgets_local_prefix}")
    endif()

    find_package(KDDockWidgets ${_kddockwidgets_version} EXACT CONFIG QUIET)
    if(NOT TARGET KDDockWidgets::KDDockWidgets
       AND NOT TARGET KDDockWidgets::kddockwidgets
       AND NOT TARGET KDAB::kddockwidgets)
        find_package(KDDockWidgets-qt6 ${_kddockwidgets_version} EXACT CONFIG REQUIRED)
    endif()
    target_compile_definitions(firebird-emu PRIVATE FIREBIRD_USE_KDDOCKWIDGETS=1)

    set(_kddockwidgets_target "")
    if(TARGET KDDockWidgets::KDDockWidgets)
        set(_kddockwidgets_target KDDockWidgets::KDDockWidgets)
    elseif(TARGET KDDockWidgets::kddockwidgets)
        set(_kddockwidgets_target KDDockWidgets::kddockwidgets)
    elseif(TARGET KDAB::kddockwidgets)
        set(_kddockwidgets_target KDAB::kddockwidgets)
    else()
        message(FATAL_ERROR "KDDockWidgets package found, but no known CMake target was exported.")
    endif()

    target_link_libraries(firebird-emu PRIVATE ${_kddockwidgets_target})

endif()

# Resources:
set(resources_resource_files
    "resources/icons/Actions-media-playback-pause-icon.png"
    "resources/icons/Actions-network-connect-icon.png"
    "resources/icons/Actions-network-disconnect-icon.png"
    "resources/icons/application-exit.png"
    "resources/icons/camera-photo.png"
    "resources/icons/document-edit.png"
    "resources/icons/document-new.png"
    "resources/icons/document.png"
    "resources/icons/drive-removable-media-usb.png"
    "resources/icons/edit-bomb.png"
    "resources/icons/flash-create.png"
    "resources/icons/kmplayer.png"
    "resources/icons/media-flash.png"
    "resources/icons/media-floppy.png"
    "resources/icons/preferences-desktop-keyboard.png"
    "resources/icons/preferences-other.png"
    "resources/icons/smartphone.png"
    "resources/icons/system-reboot.png"
    "resources/icons/system-suspend-hibernate.png"
    "resources/icons/system-suspend.png"
    "resources/icons/tools-report-bug.png"
    "resources/icons/utilities-terminal.png"
    "resources/icons/video-display.png"
    "resources/org.firebird-emus.firebird-emu.png"
)

qt_add_resources(firebird-emu "resources"
    PREFIX
        "/icons"
    FILES
        ${resources_resource_files}
)
set(resources2_resource_files
    "i18n/de_DE.qm"
    "i18n/fr_FR.qm"
    "i18n/pl_PL.qm"
)

qt_add_resources(firebird-emu "resources2"
    PREFIX
        "/i18n"
    FILES
        ${resources2_resource_files}
)
set(resources3_resource_files
    "resources/keyimages/catalog.png"
    "resources/keyimages/context_menu.png"
    "resources/keyimages/contrast_minus.png"
    "resources/keyimages/contrast_plus.png"
    "resources/keyimages/flag.png"
    "resources/keyimages/questionmark.png"
    "resources/keyimages/templates.png"
)

qt_add_resources(firebird-emu "resources3"
    PREFIX
        "/keyimages"
    FILES
        ${resources3_resource_files}
)
set_source_files_properties("resources/fonts/MaterialIconsRound-Regular.otf"
    PROPERTIES QT_RESOURCE_ALIAS "MaterialIconsRound-Regular.otf"
)
set_source_files_properties("resources/fonts/MaterialSymbolsRounded.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "MaterialSymbolsRounded.ttf"
)
set_source_files_properties("resources/fonts/MaterialSymbolsRounded.woff2"
    PROPERTIES QT_RESOURCE_ALIAS "MaterialSymbolsRounded.woff2"
)
set(resources4_resource_files
    "resources/fonts/MaterialIconsRound-Regular.otf"
    "resources/fonts/MaterialSymbolsRounded.ttf"
    "resources/fonts/MaterialSymbolsRounded.woff2"
)

qt_add_resources(firebird-emu "resources4"
    PREFIX
        "/fonts"
    FILES
        ${resources4_resource_files}
)

if(EMSCRIPTEN)
    target_sources(firebird-emu PUBLIC
        core/os/os-emscripten.c
    )
elseif(UNIX)
    target_sources(firebird-emu PUBLIC
        core/os/os-linux.c
    )
endif()

if(WIN32)
    target_sources(firebird-emu PUBLIC
        core/os/os-win32.c
    )

    target_link_libraries(firebird-emu PRIVATE
        winmm
        ws2_32
    )

    if(MINGW)
        target_compile_options(firebird-emu PRIVATE
            -mno-ms-bitfields
        )
    endif()
endif()

if(ANDROID)
    target_sources(firebird-emu PUBLIC
        core/os/os-android.cpp
    )
endif()

if(ANDROID OR IOS)
    target_compile_definitions(firebird-emu PRIVATE
        MOBILE_UI
    )
endif()

if(IOS)
    target_compile_definitions(firebird-emu PRIVATE
        IS_IOS_BUILD
    )

    target_compile_options(firebird-emu
        -mno-thumb
    )
endif()

# JIT / translation configuration (mirrors qmake defaults).
set(TRANSLATION_ENABLED "AUTO" CACHE STRING "Enable JIT translation (AUTO/ON/OFF)")
set_property(CACHE TRANSLATION_ENABLED PROPERTY STRINGS AUTO ON OFF)

set(SUPPORT_LINUX "AUTO" CACHE STRING "Enable Linux-like fast paths (AUTO/ON/OFF)")
set_property(CACHE SUPPORT_LINUX PROPERTY STRINGS AUTO ON OFF)

if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 FB_ARCH_RAW)
else()
    set(FB_ARCH_RAW "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${FB_ARCH_RAW}" FB_ARCH_RAW_LOWER)
set(FB_ARCH "${FB_ARCH_RAW_LOWER}")
if(FB_ARCH_RAW_LOWER MATCHES "^(arm64|aarch64)$")
    set(FB_ARCH "aarch64")
elseif(FB_ARCH_RAW_LOWER MATCHES "^arm")
    set(FB_ARCH "arm")
elseif(FB_ARCH_RAW_LOWER MATCHES "^(x86_64|amd64)$")
    set(FB_ARCH "x86_64")
elseif(FB_ARCH_RAW_LOWER MATCHES "^(i[3-6]86|x86)$")
    set(FB_ARCH "x86")
endif()
if(EMSCRIPTEN)
    set(FB_ARCH "wasm")
endif()

set(TRANSLATE_SOURCES "")
set(ASM_SOURCES "")
set(DISABLE_ASMCODE_C FALSE)
set(TRANSLATION_AVAILABLE FALSE)

if(FB_ARCH STREQUAL "x86")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/cpu/translate_x86.c)
    set(ASM_SOURCES core/jit/asmcode_x86.S)
    set(DISABLE_ASMCODE_C TRUE)
elseif(FB_ARCH STREQUAL "x86_64")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/cpu/translate_x86_64.c)
    set(ASM_SOURCES core/jit/asmcode_x86_64.S)
elseif(FB_ARCH STREQUAL "arm")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/cpu/translate_arm.cpp)
    set(ASM_SOURCES core/jit/asmcode_arm.S)
elseif(FB_ARCH STREQUAL "aarch64")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/cpu/translate_aarch64.cpp)
    set(ASM_SOURCES core/jit/asmcode_aarch64.S)
endif()

string(TOUPPER "${TRANSLATION_ENABLED}" TRANSLATION_ENABLED_UPPER)
if(TRANSLATION_ENABLED_UPPER STREQUAL "AUTO")
    set(TRANSLATION_ENABLED_ON ${TRANSLATION_AVAILABLE})
elseif(TRANSLATION_ENABLED_UPPER STREQUAL "ON" OR TRANSLATION_ENABLED_UPPER STREQUAL "TRUE")
    if(NOT TRANSLATION_AVAILABLE)
        message(FATAL_ERROR "No JIT found for arch ${FB_ARCH}")
    endif()
    set(TRANSLATION_ENABLED_ON TRUE)
elseif(TRANSLATION_ENABLED_UPPER STREQUAL "OFF" OR TRANSLATION_ENABLED_UPPER STREQUAL "FALSE")
    set(TRANSLATION_ENABLED_ON FALSE)
else()
    message(FATAL_ERROR "TRANSLATION_ENABLED must be AUTO, ON, or OFF")
endif()

if(TRANSLATION_ENABLED_ON)
    target_sources(firebird-emu PRIVATE ${ASM_SOURCES} ${TRANSLATE_SOURCES})
else()
    if(NOT EMSCRIPTEN)
        target_compile_definitions(firebird-emu PRIVATE NO_TRANSLATION)
    endif()
endif()

if(DISABLE_ASMCODE_C AND TRANSLATION_ENABLED_ON)
    set_source_files_properties(core/jit/asmcode.c PROPERTIES HEADER_FILE_ONLY TRUE)
else()
    set_source_files_properties(core/jit/asmcode.c PROPERTIES HEADER_FILE_ONLY FALSE)
endif()

string(TOUPPER "${SUPPORT_LINUX}" SUPPORT_LINUX_UPPER)
if(SUPPORT_LINUX_UPPER STREQUAL "AUTO")
    set(SUPPORT_LINUX_ON TRUE)
    if(ANDROID OR IOS)
        set(SUPPORT_LINUX_ON FALSE)
    endif()
elseif(SUPPORT_LINUX_UPPER STREQUAL "ON" OR SUPPORT_LINUX_UPPER STREQUAL "TRUE")
    set(SUPPORT_LINUX_ON TRUE)
elseif(SUPPORT_LINUX_UPPER STREQUAL "OFF" OR SUPPORT_LINUX_UPPER STREQUAL "FALSE")
    set(SUPPORT_LINUX_ON FALSE)
else()
    message(FATAL_ERROR "SUPPORT_LINUX must be AUTO, ON, or OFF")
endif()

if(SUPPORT_LINUX_ON)
    target_compile_definitions(firebird-emu PRIVATE SUPPORT_LINUX)
endif()

if(FB_ARCH STREQUAL "arm" AND NOT ANDROID)
    target_compile_options(firebird-emu PRIVATE -march=armv7-a -marm)
endif()

message(STATUS "FB_ARCH: ${FB_ARCH}")
message(STATUS "TRANSLATION_ENABLED: ${TRANSLATION_ENABLED_ON}")
message(STATUS "SUPPORT_LINUX: ${SUPPORT_LINUX_ON}")

if(lupdate_only)
    target_sources(firebird-emu PUBLIC
        qml/ConfigPageDebug.qml
        qml/ConfigPageEmulation.qml
        qml/ConfigPageFileTransfer.qml
        qml/ConfigPageKits.qml
        qml/ConfigPagesModel.qml
        qml/FBConfigDialog.qml
        qml/Firebird/Emu/Emu.qml
        qml/Firebird/Emu/EmuScreen.qml
        qml/Firebird/UIComponents/ConfigPages.qml
        qml/Firebird/UIComponents/FBLabel.qml
        qml/Firebird/UIComponents/FileSelect.qml
        qml/Firebird/UIComponents/PageDelegate.qml
        qml/Firebird/UIComponents/PageList.qml
        qml/FlashDialog.qml
        qml/Keypad.qml
        qml/MobileControl2.qml
        qml/MobileUI.qml
        qml/NAlphaButton.qml
        qml/NBigButton.qml
        qml/NButton.qml
        qml/NDualButton.qml
        qml/NNumButton.qml
        qml/SidebarButton.qml
        qml/Touchpad.qml
    )
endif()

if(just_show_up_in_qt_creator)
    target_sources(firebird-emu PUBLIC
        core/jit/asmcode.c
        core/jit/asmcode_aarch64.S
        core/jit/asmcode_arm.S
        core/jit/asmcode_x86.S
        core/jit/asmcode_x86_64.S
        core/os/os-emscripten.c
        core/cpu/translate_aarch64.cpp
        core/cpu/translate_arm.cpp
        core/cpu/translate_x86.c
        core/cpu/translate_x86_64.c
        emscripten/main.cpp
        headless/main.cpp
    )
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(advisory-file-size
        COMMAND ${Python3_EXECUTABLE} ${FIREBIRD_SOURCE_DIR}/tools/dev/check_file_size.py --target 600 --cap 1000
        WORKING_DIRECTORY ${FIREBIRD_SOURCE_DIR}
        COMMENT "Advisory file-size policy check (target 600, cap 1000)"
        VERBATIM
    )
endif()

install(TARGETS firebird-emu
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(COMMAND qt_generate_deploy_qml_app_script)
    qt_generate_deploy_qml_app_script(
        TARGET firebird-emu
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
        MACOS_BUNDLE_POST_BUILD
    )
    install(SCRIPT ${deploy_script})
endif()
