import QtQuick 6.0
import Firebird.UIComponents 1.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    property alias text: label.text
    signal clicked

    implicitWidth: (label && label.height ? label.height + 4 : 24)
    width: implicitWidth
    color: FBUI.Theme.surfaceAlt
    border.color: FBUI.Theme.border
    radius: 2

    FBLabel {
        id: label
        rotation: -90
        anchors.centerIn: parent
        anchors.rightMargin: 2
        text: qsTr("Swipe here")
    }

    MouseArea {
        anchors.fill: parent
        onClicked: clicked()
    }
}
