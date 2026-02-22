
import QtQuick 6.0
import QtQuick.Controls 6.0
import QtQuick.Layouts 6.0

import Firebird.UIComponents 1.0

Item {

    id: root
    property var listView

    property var configModel: ConfigPagesModel {}

    SystemPalette {
        id: paletteActive
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: swipeBar.left
            top: parent.top
            bottom: parent.bottom
        }
        spacing: 0

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            Repeater {
                model: root.configModel

                TabButton {
                    required property var modelData
                    text: qsTranslate("ConfigPagesModel", modelData.title)
                }
            }
        }

        Loader {
            id: pageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: root.configModel.get(tabBar.currentIndex) ? root.configModel.get(tabBar.currentIndex).file : ""
        }

        FBLabel {
            id: autoSaveLabel
            Layout.fillWidth: true
            Layout.margins: 2

            text: qsTr("Changes are saved automatically")
            font.italic: true
            color: paletteActive.placeholderText
        }
    }

    VerticalSwipeBar {
        id: swipeBar

        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
        }

        onClicked: {
            if (root.listView)
                root.listView.openDrawer();
        }
    }
}
