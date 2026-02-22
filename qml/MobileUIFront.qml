
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0

import QtQuick 6.0
import QtQuick.Layouts 6.0

GridLayout {
    id: mobileui
    property var listView

    readonly property bool tabletMode: width > height
    readonly property real tabletControlsWidth: Math.floor(keypad.width/keypad.height * (mobileui.height - iosmargin.height))

    // For previewing just this component
    width: 600
    height: 800

    columns: tabletMode ? 3 : 2
    columnSpacing: 0
    rowSpacing: 0
    layoutDirection: tabletMode ? (Emu.leftHanded ? Qt.RightToLeft : Qt.LeftToRight) : Qt.LeftToRight

    VerticalSwipeBar {
        id: swipeBar
        Layout.preferredHeight: screen.implicitHeight
        visible: !mobileui.tabletMode

        onClicked: {
            if (mobileui.listView && typeof mobileui.listView.openDrawer === "function")
                mobileui.listView.openDrawer();
        }
    }

    EmuScreen {
        id: screen
        implicitHeight: (mobileui.width - swipeBar.implicitWidth) / 320 * 240
        Layout.fillWidth: true
        Layout.fillHeight: mobileui.tabletMode

        focus: true

        Timer {
            interval: 35
            running: true
            repeat: true
            onTriggered: screen.update()
        }
    }

    Flickable {
        id: controls

        Layout.fillHeight: mobileui.tabletMode
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.maximumHeight: contentHeight
        Layout.columnSpan: mobileui.tabletMode ? 1 : 2
        Layout.minimumWidth: mobileui.tabletMode ? mobileui.tabletControlsWidth : 0
        Layout.maximumWidth: mobileui.tabletMode ? mobileui.tabletControlsWidth : Number.MAX_VALUE

        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        contentWidth: parent.width
        contentHeight: keypad.height*controls.width/keypad.width + iosmargin.height
        clip: true
        pixelAligned: true

        Keypad {
            id: keypad
            transform: Scale { origin.x: 0; origin.y: 0; xScale: controls.width/keypad.width; yScale: controls.width/keypad.width }
        }

        Rectangle {
            id: iosmargin

            SystemPalette {
                id: paletteActive
            }

            color: paletteActive.window

            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }

            // This is needed to avoid opening the control center
            height: Qt.platform.os === "ios" ? 20 : 0
        }
    }

    Rectangle {
        Layout.fillHeight: true
        Layout.fillWidth: true
        Layout.columnSpan: mobileui.tabletMode ? 1 : 2

        SystemPalette {
            id: paletteBottomActive
        }

        color: paletteBottomActive.window
    }

}
