import QtQuick 6.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    property alias currentItem: listView.currentItem
    property alias currentIndex: listView.currentIndex
    property alias model: listView.model

    color: FBUI.Theme.surface
    border {
        color: FBUI.Theme.border
        width: 1
    }

    Item {
        anchors.margins: parent.border.width
        anchors.fill: parent
        clip: true

        ListView {
            id: listView

            anchors.centerIn: parent
            anchors.fill: parent
            anchors.topMargin: width * 0.05
            anchors.bottomMargin: width * 0.05
            focus: true
            highlightMoveDuration: 150

            highlight: Rectangle {
                color: FBUI.Theme.accent
                anchors.horizontalCenter: parent.horizontalCenter
            }

            delegate: PageDelegate {
                text: qsTranslate("ConfigPagesModel", title)
                iconSource: icon
            }
        }
    }
}
