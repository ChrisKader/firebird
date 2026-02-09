cmake_minimum_required(VERSION 3.16)
project(firebird-emu VERSION 1.0 LANGUAGES C CXX ASM)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickWidgets Widgets)

qt_standard_project_setup()

qt_add_executable(firebird-emu WIN32 MACOSX_BUNDLE
    # Core emulator
    core/arm_interpreter.cpp
    core/armcode_bin.h
    core/armsnippets.h
    core/armsnippets_loader.c
    core/asmcode.c core/asmcode.h
    core/bitfield.h
    core/casplus.c core/casplus.h
    core/coproc.cpp
    core/cpu.cpp core/cpu.h
    core/cpudefs.h
    core/cx2.cpp core/cx2.h
    core/debug.cpp core/debug.h
    core/debug_api.cpp core/debug_api.h
    core/des.c core/des.h
    core/disasm.c core/disasm.h
    core/emu.cpp core/emu.h
    core/fieldparser.cpp core/fieldparser.h
    core/flash.cpp core/flash.h
    core/gdbstub.c core/gdbstub.h
    core/gif.cpp core/gif.h
    core/interrupt.c core/interrupt.h
    core/keypad.cpp core/keypad.h
    core/lcd.c core/lcd.h
    core/link.c core/link.h
    core/mem.c core/mem.h
    core/misc.c core/misc.h
    core/mmu.c core/mmu.h
    core/os/os.h
    core/schedule.c core/schedule.h
    core/serial.c
    core/sha256.c core/sha256.h
    core/thumb_interpreter.cpp
    core/translate.h
    core/usb.c core/usb.h
    core/usb_cx2.cpp core/usb_cx2.h
    core/usblink.c core/usblink.h
    core/usblink_cx2.cpp core/usblink_cx2.h
    core/usblink_queue.cpp core/usblink_queue.h
    # Application infrastructure
    app/emuthread.cpp app/emuthread.h
    app/qmlbridge.cpp app/qmlbridge.h
    app/kitmodel.cpp app/kitmodel.h
    # Shared UI components
    ui/dockwidget.cpp ui/dockwidget.h
    ui/widgettheme.cpp ui/widgettheme.h
    ui/materialicons.cpp ui/materialicons.h
    ui/activitybar.cpp ui/activitybar.h
    ui/lcdwidget.cpp ui/lcdwidget.h
    ui/framebuffer.cpp ui/framebuffer.h
    ui/keypadbridge.cpp ui/keypadbridge.h
    ui/keymap.h
    ui/consolelineedit.cpp ui/consolelineedit.h
    ui/ansitextwriter.cpp ui/ansitextwriter.h
    # Debugger subsystem
    debugger/dockmanager.cpp debugger/dockmanager.h
    debugger/disassembly/disassemblywidget.cpp debugger/disassembly/disassemblywidget.h
    debugger/registers/registerwidget.cpp debugger/registers/registerwidget.h
    debugger/hexview/hexviewwidget.cpp debugger/hexview/hexviewwidget.h
    debugger/breakpoints/breakpointwidget.cpp debugger/breakpoints/breakpointwidget.h
    debugger/watchpoints/watchpointwidget.cpp debugger/watchpoints/watchpointwidget.h
    debugger/stack/stackwidget.cpp debugger/stack/stackwidget.h
    debugger/portmonitor/portmonitorwidget.cpp debugger/portmonitor/portmonitorwidget.h
    debugger/keyhistory/keyhistorywidget.cpp debugger/keyhistory/keyhistorywidget.h
    debugger/console/consolewidget.cpp debugger/console/consolewidget.h
    debugger/memvisualizer/memoryvisualizerwidget.cpp debugger/memvisualizer/memoryvisualizerwidget.h
    debugger/cyclecounter/cyclecounterwidget.cpp debugger/cyclecounter/cyclecounterwidget.h
    debugger/timermonitor/timermonitorwidget.cpp debugger/timermonitor/timermonitorwidget.h
    debugger/lcdstate/lcdstatewidget.cpp debugger/lcdstate/lcdstatewidget.h
    debugger/gotodialog.cpp debugger/gotodialog.h
    debugger/nandbrowser/nandbrowserwidget.cpp debugger/nandbrowser/nandbrowserwidget.h
    debugger/hwconfig/hwconfigwidget.cpp debugger/hwconfig/hwconfigwidget.h
    # Dialogs
    dialogs/fbaboutdialog.cpp dialogs/fbaboutdialog.h
    # File transfer
    transfer/usblinktreewidget.cpp transfer/usblinktreewidget.h
    # Main window (stays in root for AUTOUIC)
    main.cpp
    mainwindow.cpp mainwindow.h mainwindow.ui
)
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

target_include_directories(firebird-emu PRIVATE ${CMAKE_SOURCE_DIR})

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
    z
)

target_compile_options(firebird-emu PRIVATE
    -Wall
    -Wextra
    -g
)

target_compile_definitions(firebird-emu PRIVATE
    QT_NO_CAST_FROM_ASCII
)

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

if(UNIX)
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

    target_compile_options(firebird-emu PRIVATE
        -mno-ms-bitfields
    )
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

set(TRANSLATE_SOURCES "")
set(ASM_SOURCES "")
set(DISABLE_ASMCODE_C FALSE)
set(TRANSLATION_AVAILABLE FALSE)

if(FB_ARCH STREQUAL "x86")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/translate_x86.c)
    set(ASM_SOURCES core/asmcode_x86.S)
    set(DISABLE_ASMCODE_C TRUE)
elseif(FB_ARCH STREQUAL "x86_64")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/translate_x86_64.c)
    set(ASM_SOURCES core/asmcode_x86_64.S)
elseif(FB_ARCH STREQUAL "arm")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/translate_arm.cpp)
    set(ASM_SOURCES core/asmcode_arm.S)
elseif(FB_ARCH STREQUAL "aarch64")
    set(TRANSLATION_AVAILABLE TRUE)
    set(TRANSLATE_SOURCES core/translate_aarch64.cpp)
    set(ASM_SOURCES core/asmcode_aarch64.S)
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
    target_compile_definitions(firebird-emu PRIVATE NO_TRANSLATION)
endif()

if(DISABLE_ASMCODE_C AND TRANSLATION_ENABLED_ON)
    set_source_files_properties(core/asmcode.c PROPERTIES HEADER_FILE_ONLY TRUE)
else()
    set_source_files_properties(core/asmcode.c PROPERTIES HEADER_FILE_ONLY FALSE)
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
        core/asmcode.c
        core/asmcode_aarch64.S
        core/asmcode_arm.S
        core/asmcode_x86.S
        core/asmcode_x86_64.S
        core/os/os-emscripten.c
        core/translate_aarch64.cpp
        core/translate_arm.cpp
        core/translate_x86.c
        core/translate_x86_64.c
        emscripten/main.cpp
        headless/main.cpp
    )
endif()

install(TARGETS firebird-emu
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET firebird-emu
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
    MACOS_BUNDLE_POST_BUILD
)
install(SCRIPT ${deploy_script})
