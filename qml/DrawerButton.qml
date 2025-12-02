import QtQuick 6.0
import QtQuick.Controls 6.0
import QtQuick.Layouts 6.0
import Firebird.UIComponents 1.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    property alias icon: image.source
    property alias title: label.text
    property alias borderTopVisible: borderTop.visible
    property alias borderBottomVisible: borderBottom.visible
    property bool disabled: false
    property alias font: label.font
    property bool toggle: false
    property bool toggleState: false
    property int spacing: 10

    opacity: disabled ? 0.5 : 1.0

    height: 48

    Layout.fillWidth: true

    signal clicked()

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        onClicked: {
            if(disabled)
                return;

            parent.clicked()

            if(toggle)
                toggleState = !toggleState;
        }
    }

    Rectangle {
        id: borderTop
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }

        height: 1
        color: FBUI.Theme.border
    }

    Rectangle {
        id: borderBottom
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        height: 1
        color: FBUI.Theme.border
    }

    color: (mouseArea.pressed !== toggleState) && !disabled ? FBUI.Theme.surfaceAlt : "transparent"
    Behavior on color { ColorAnimation { duration: 200; } }

    Image {
        id: image

        x: spacing

        height: parent.height * 0.8

        anchors.verticalCenter: parent.verticalCenter

        fillMode: Image.PreserveAspectFit
    }

    FBLabel {
        id: label

        color: paletteActive.windowText

        x: image.x + image.width + spacing

        anchors {
            top: parent.top
            bottom: parent.bottom
        }

        font.pixelSize: (TextMetrics && TextMetrics.title2Size ? TextMetrics.title2Size : 16)
        verticalAlignment: Text.AlignVCenter
    }
}
