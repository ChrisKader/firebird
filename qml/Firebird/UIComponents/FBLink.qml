import QtQuick 6.0
import QtQuick.Controls 6.0
import Firebird.UIComponents 1.0 as FBUI

FBLabel {
    signal clicked

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: parent.clicked()
    }
    
    color: FBUI.Theme.accent
    font.bold: true
}
