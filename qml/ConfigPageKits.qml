import QtQuick 6.0
import QtQuick.Controls 6.0
import QtQuick.Layouts 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0

ColumnLayout {
    property var kitModel
    Component.onCompleted: kitModel = Emu.kits

    spacing: 5

    KitList {
        id: kitList

        Layout.fillHeight: true
        Layout.fillWidth: true

        Layout.rightMargin: 1
        Layout.leftMargin: 1
        Layout.topMargin: 5

        kitModel: parent.kitModel
    }

    GroupBox {
        Layout.fillWidth: true
        Layout.minimumWidth: contentItem.Layout.minimumWidth
        Layout.bottomMargin: -1
        title: qsTr("Kit Properties")

        GridLayout {
            anchors.fill: parent
            columns: (width < 550 || Qt.platform.os === "android") ? 2 : 4

            FBLabel {
                Layout.columnSpan: parent.columns
                Layout.fillWidth: true
                color: "red"
                visible: boot1Edit.filePath == "" || flashEdit.filePath == ""
                wrapMode: Text.WordWrap
                text: qsTr("You need to specify files for Boot1 and Flash")
            }

            FBLabel {
                text: qsTr("Name:")
                elide: Text.ElideMiddle
            }

            TextField {
                id: nameEdit
                placeholderText: qsTr("Name")
                Layout.fillWidth: true

                text: kitList.currentName
                onTextChanged: {
                    if(text !== kitList.currentName)
                        kitModel.setDataRow(kitList.currentIndex, text, KitModel.NameRole);
                    text = Qt.binding(function() { return kitList.currentName; });
                }
            }

            FBLabel {
                text: qsTr("Boot1:")
                elide: Text.ElideMiddle
            }

            FileSelect {
                id: boot1Edit
                Layout.fillWidth: true
                filePath: kitList.currentBoot1
                onFilePathChanged: {
                    if(filePath !== kitList.currentBoot1)
                        kitModel.setDataRow(kitList.currentIndex, filePath, KitModel.Boot1Role);
                    filePath = Qt.binding(function() { return kitList.currentBoot1; });
                }
            }

            FBLabel {
                text: qsTr("Flash:")
                elide: Text.ElideMiddle
            }

            FileSelect {
                id: flashEdit
                Layout.fillWidth: true
                filePath: kitList.currentFlash
                onFilePathChanged: {
                    if(filePath !== kitList.currentFlash)
                        kitModel.setDataRow(kitList.currentIndex, filePath, KitModel.FlashRole);
                    filePath = Qt.binding(function() { return kitList.currentFlash; });
                }
                showCreateButton: true
                onCreate: flashDialog.visible = true
            }

            FlashDialog {
                id: flashDialog
                onFlashCreated: {
                    kitModel.setDataRow(kitList.currentIndex, filePath, KitModel.FlashRole);
                }
            }

            FBLabel {
                text: qsTr("Snapshot:")
                elide: Text.ElideMiddle
            }

            FileSelect {
                id: snapshotEdit
                Layout.fillWidth: true
                selectExisting: false
                filePath: kitList.currentSnapshot
                onFilePathChanged: {
                    if(filePath !== kitList.currentSnapshot)
                        kitModel.setDataRow(kitList.currentIndex, filePath, KitModel.SnapshotRole);
                    filePath = Qt.binding(function() { return kitList.currentSnapshot; });
                }
            }
        }
    }
}
