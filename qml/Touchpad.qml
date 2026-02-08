import QtQuick 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    id: rectangle2
    width: 100
    height: 70
    color: FBUI.Theme.surface
    radius: 10
    border.width: 2
    border.color: FBUI.Theme.border

    SvgPaths { id: svgPaths }

    Rectangle {
        id: rectangle1
        x: 29
        y: 18
        width: 35
        height: 35
        color: "transparent"
        radius: 8
        border.color: FBUI.Theme.borderStrong
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter

        SvgIcon {
            pathData: svgPaths.touchpadGrab
            fillColor: FBUI.Theme.textMuted
            width: 20; height: 20
            padding: 0
            anchors.centerIn: parent
        }
    }

    // Touchpad secondary navigation icons (blue, at border edge)
    // On the real calculator these sit at/just outside the touchpad rim.
    SvgIcon {
        pathData: svgPaths.touchpadUp
        fillColor: FBUI.Theme.accent
        width: 16; height: 7
        padding: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top; anchors.bottomMargin: 1
    }
    SvgIcon {
        pathData: svgPaths.touchpadDown
        fillColor: FBUI.Theme.accent
        width: 16; height: 7
        padding: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom; anchors.topMargin: 1
    }
    SvgIcon {
        pathData: svgPaths.touchpadLeft
        fillColor: FBUI.Theme.accent
        width: 7; height: 16
        padding: 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.left; anchors.rightMargin: 1
    }
    SvgIcon {
        pathData: svgPaths.touchpadRight
        fillColor: FBUI.Theme.accent
        width: 7; height: 16
        padding: 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.right; anchors.leftMargin: 1
    }

    // Touchpad arrow indicators (inside touchpad, near edges)
    SvgIcon {
        pathData: svgPaths.arrowUp
        fillColor: FBUI.Theme.textMuted
        width: 8; height: 5
        padding: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top; anchors.topMargin: 6
    }
    SvgIcon {
        pathData: svgPaths.arrowDown
        fillColor: FBUI.Theme.textMuted
        width: 8; height: 5
        padding: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 6
    }
    SvgIcon {
        pathData: svgPaths.arrowLeft
        fillColor: FBUI.Theme.textMuted
        width: 5; height: 8
        padding: 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left; anchors.leftMargin: 6
    }
    SvgIcon {
        pathData: svgPaths.arrowRight
        fillColor: FBUI.Theme.textMuted
        width: 5; height: 8
        padding: 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right; anchors.rightMargin: 6
    }

    Rectangle {
        id: highlight
        x: 0
        y: 0
        width: 10
        height: 10
        color: FBUI.Theme.accent
        opacity: 0.0
        radius: 10
        visible: false
    }

    /* Click and hold at same place -> Down
       Click and quick release -> Down
       Click and move -> Contact */
    MouseArea {
        anchors.rightMargin: 0
        anchors.bottomMargin: 0
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true

        property int origX
        property int origY
        property int moveThreshold: 5
        property bool isDown

        Connections {
            target: Emu
            function onTouchpadStateChanged(x, y, contact, down) {
                if(contact || down)
                {
                    highlight.x = x*rectangle2.width - highlight.width/2;
                    highlight.y = y*rectangle2.height - highlight.height/2;
                    highlight.opacity = down ? 0.45 : 0.3;
                }

                highlight.visible = contact || down;
            }
        }

        Timer {
            id: clickOnHoldTimer
            interval: 200
            running: false
            repeat: false
            onTriggered: {
                parent.isDown = true;
                parent.submitState();
            }
        }

        Timer {
            id: clickOnReleaseTimer
            interval: 100
            running: false
            repeat: false
            onTriggered: {
                parent.isDown = false;
                parent.submitState();
            }
        }

        function submitState() {
            Emu.setTouchpadState(mouseX/width, mouseY/height, isDown || pressed, isDown);
        }

        onMouseXChanged: {
            if(Math.abs(mouseX - origX) > moveThreshold)
                clickOnHoldTimer.stop();

            submitState();
        }

        onMouseYChanged: {
            if(Math.abs(mouseY - origY) > moveThreshold)
                clickOnHoldTimer.stop();

            submitState();
        }

        onReleased: {
            if(clickOnHoldTimer.running)
            {
                clickOnHoldTimer.stop();
                isDown = true;
                clickOnReleaseTimer.restart();
            }
            else
                isDown = false;

            submitState();
        }

        onPressed: function(mouse) {
            origX = mouse.x;
            origY = mouse.y;
            isDown = false;
            clickOnHoldTimer.restart();
        }
    }
}
