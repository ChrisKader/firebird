import QtQuick 6.0
import QtQuick.Controls 6.0
import Qt.labs.platform 1.1 as Platform
import QtQuick.Layouts 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0
import Firebird.UIComponents 1.0 as FBUI

RowLayout {
    property string filePath: ""
    property bool selectExisting: true
    property alias subtext: subtextLabel.text
    property bool showCreateButton: false
    signal create()

    // Hack to force reevaluation of Emu.fileExists(filePath) after reselection.
    // Needed on Android due to persistent permissions.
    property int forceRefresh: 0
    Loader {
        id: dialogLoader
        active: false
        sourceComponent: Platform.FileDialog {
            folder: Emu.dir(filePath)
            // If save dialogs are not supported, force an open dialog
            fileMode: selectExisting || !Emu.saveDialogSupported() ? Platform.FileDialog.OpenFile : Platform.FileDialog.SaveFile
            onAccepted: {
                filePath = Emu.toLocalFile(currentFile);
                forceRefresh++;
            }
        }
    }

    SystemPalette {
        id: paletteActive
    }

    ColumnLayout {
        Layout.fillWidth: true

        FBLabel {
            id: filenameLabel
            elide: Text.ElideRight

            Layout.fillWidth: true
            // Allow the label to shrink below its implicitWidth.
            // Without this, the layout doesn't allow it to go smaller...
            Layout.preferredWidth: 100

            font.italic: filePath === ""
            text: { forceRefresh; return filePath === "" ? qsTr("(none)") : Emu.basename(filePath); }
            color: { forceRefresh; return ((!selectExisting && Emu.saveDialogSupported()) || filePath === "" || Emu.fileExists(filePath)) ? paletteActive.text : "red"; }
        }

        FBLabel {
            id: subtextLabel
            elide: Text.ElideRight

            font.pixelSize: FBUI.TextMetrics && FBUI.TextMetrics.normalSize ? FBUI.TextMetrics.normalSize * 0.8 : 10
            Layout.fillWidth: true
            visible: text !== ""
        }
    }

    // Button for either custom creation functionality (onCreate) or
    // if the open file dialog doesn't allow creation, to open a file creation dialog.
    IconButton {
        visible: showCreateButton || (!selectExisting && !Emu.saveDialogSupported())
        iconSource: "qrc:/icons/resources/icons/document-new.png"

        Loader {
            id: createDialogLoader
            active: false
            sourceComponent: Platform.FileDialog {
                folder: Emu.dir(filePath)
                fileMode: Platform.FileDialog.SaveFile
                onAccepted: {
                    filePath = Emu.toLocalFile(currentFile);
                    forceRefresh++;
                }
            }
        }

        onClicked: {
            if(showCreateButton)
                parent.create()
            else {
                createDialogLoader.active = true;
                createDialogLoader.item.visible = true;
            }
        }
    }

    IconButton {
        iconSource: "qrc:/icons/resources/icons/document-edit.png"
        onClicked: {
            dialogLoader.active = true;
            dialogLoader.item.visible = true;
        }
    }
}
