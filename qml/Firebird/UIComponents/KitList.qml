import QtQuick 6.0
import QtQuick.Controls 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    property alias currentItem: listView.currentItem
    property alias currentIndex: listView.currentIndex
    property alias kitModel: listView.model

    readonly property string currentName: (currentItem && currentItem.name) ? currentItem.name : ""
    readonly property string currentBoot1: (currentItem && currentItem.boot1) ? currentItem.boot1 : ""
    readonly property string currentFlash: (currentItem && currentItem.flash) ? currentItem.flash : ""
    readonly property string currentSnapshot: (currentItem && currentItem.snapshot) ? currentItem.snapshot : ""

    color: FBUI.Theme.surface
    border {
        color: FBUI.Theme.border
        width: 1
    }

    ScrollView {
        anchors.margins: parent.border.width
        anchors.fill: parent

        activeFocusOnTab: true

        ListView {
            id: listView

            anchors.centerIn: parent
            anchors.fill: parent
            focus: true
            highlightMoveDuration: 50
            highlightResizeDuration: 0

            highlight: Rectangle {
                color: FBUI.Theme.accent
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
            }

            delegate: Item {
                id: delegateItem
                required property string name
                required property string boot1
                required property string flash
                required property string snapshot
                required property int index

                height: item.height + 10
                width: listView.width - listView.anchors.margins

                MouseArea {
                    anchors.fill: parent
                    onClicked: function() {
                        parent.ListView.view.currentIndex = delegateItem.index
                        parent.forceActiveFocus()
                    }
                }

                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }

                    color: FBUI.Theme.border
                    height: 1
                }

                KitItem {
                    id: item
                    width: parent.width - 15
                    anchors.centerIn: parent

                    kitName: parent.name
                    flashFile: Emu.basename(parent.flash)
                    stateFile: Emu.basename(parent.snapshot)
                }

                FBLink {
                    anchors {
                        top: parent.top
                        right: copyButton.left
                        topMargin: 5
                        rightMargin: 5
                    }

                    text: qsTr("Remove")
                    visible: parent.ListView.view.currentIndex === delegateItem.index && parent.ListView.view.count > 1
                    onClicked: {
                        listView.model.remove(delegateItem.index)
                    }
                }

                FBLink {
                    id: copyButton

                    anchors {
                        top: parent.top
                        right: parent.right
                        topMargin: 5
                        rightMargin: 5
                    }

                    text: qsTr("Copy")
                    visible: parent.ListView.view.currentIndex === delegateItem.index
                    onClicked: {
                        listView.model.copy(delegateItem.index)
                    }
                }
            }

            section.property: "type"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                anchors.horizontalCenter: parent.horizontalCenter
                color: FBUI.Theme.surfaceAlt
                height: label.implicitHeight + 4
                width: listView.width - listView.anchors.margins

                FBLabel {
                    id: label
                    font.bold: true
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    verticalAlignment: Text.AlignVCenter
                    text: parent.section
                }

                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }

                    color: FBUI.Theme.border
                    height: 1
                }
            }
        }
    }
}
