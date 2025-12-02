import QtQuick 6.0

import Firebird.UIComponents 1.0

Rectangle {
    property int maxWidth: parent ? Math.round(parent.width * 0.9) : 320
    height: (message && message.implicitHeight ? Math.round(message.implicitHeight + 8) : 24)
    width: (message && message.implicitWidth ? Math.min(maxWidth, Math.round(message.implicitWidth + 10)) : Math.min(maxWidth, 200))

    SystemPalette {
        id: paletteActive
    }

    color: paletteActive.window
    border.color: paletteActive.mid
    border.width: 1

    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 200 } }

    function showMessage(str) {
        message.text = str;
        opacity = 1;
        timer.restart();
    }

    FBLabel {
        id: message
        text: "Text"
        color: paletteActive.windowText
        width: parent ? parent.maxWidth : 320

        anchors.centerIn: parent

        horizontalAlignment: Text.Center
        font.pixelSize: (TextMetrics && TextMetrics.title1Size ? TextMetrics.title1Size : 16)
        wrapMode: Text.WordWrap

        Timer {
            id: timer
            interval: 2000
            onTriggered: parent.parent.opacity = 0;
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            timer.stop();
            parent.opacity = 0;
        }
    }
}
