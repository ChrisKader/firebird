import QtQuick 6.0
import QtQuick.Controls 6.0
import Qt.labs.platform 1.1 as Platform
import QtQuick.Dialogs 6.2 as QuickDialogs
import QtQuick.Layouts 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0
import Firebird.UIComponents 1.0 as FBUI

RowLayout {
    property string filePath: ""
    property bool selectExisting: true
    property alias subtext: subtextLabel.text
    property bool showCreateButton: false
    property string nameFilter: ""
    signal create()
    property int wasmRequestCounter: 0
    property string wasmPendingRequestId: ""

    // Hack to force reevaluation of Emu.fileExists(filePath) after reselection.
    // Needed on Android due to persistent permissions.
    property int forceRefresh: 0
    function openDialog(loader) {
        if(!loader.item)
            return;
        if(loader.item.open)
            loader.item.open();
        else
            loader.item.visible = true;
    }
    function requestWasmImport() {
        wasmRequestCounter++;
        wasmPendingRequestId = "file-select-" + wasmRequestCounter;
        Emu.importLocalFileForWasm(wasmPendingRequestId, nameFilter);
    }

    Connections {
        target: Emu
        function onWasmLocalFileImported(requestId, localPath, errorText) {
            if(requestId !== wasmPendingRequestId)
                return;
            wasmPendingRequestId = "";
            if(localPath !== "") {
                filePath = localPath;
                forceRefresh++;
            } else if(errorText !== "") {
                console.warn("WASM file import failed:", errorText);
            }
        }
    }

    Loader {
        id: dialogLoader
        active: false
        sourceComponent: Qt.platform.os === "wasm" ? quickDialogComponent : platformDialogComponent
        property bool pendingOpen: false
        onLoaded: {
            if(pendingOpen) {
                pendingOpen = false;
                parent.openDialog(dialogLoader);
            }
        }
    }

    Component {
        id: platformDialogComponent
        Platform.FileDialog {
            folder: Emu.dir(filePath)
            // If save dialogs are not supported, force an open dialog
            fileMode: selectExisting || !Emu.saveDialogSupported() ? Platform.FileDialog.OpenFile : Platform.FileDialog.SaveFile
            onAccepted: {
                filePath = Emu.toLocalFile(currentFile);
                forceRefresh++;
            }
        }
    }

    Component {
        id: quickDialogComponent
        QuickDialogs.FileDialog {
            currentFolder: Emu.dir(filePath)
            fileMode: selectExisting || !Emu.saveDialogSupported() ? QuickDialogs.FileDialog.OpenFile : QuickDialogs.FileDialog.SaveFile
            onAccepted: {
                filePath = Emu.toLocalFile(selectedFile);
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
            sourceComponent: Qt.platform.os === "wasm" ? createQuickDialogComponent : createPlatformDialogComponent
            property bool pendingOpen: false
            onLoaded: {
                if(pendingOpen) {
                    pendingOpen = false;
                    parent.openDialog(createDialogLoader);
                }
            }
        }

        Component {
            id: createPlatformDialogComponent
            Platform.FileDialog {
                folder: Emu.dir(filePath)
                fileMode: Platform.FileDialog.SaveFile
                onAccepted: {
                    filePath = Emu.toLocalFile(currentFile);
                    forceRefresh++;
                }
            }
        }

        Component {
            id: createQuickDialogComponent
            QuickDialogs.FileDialog {
                currentFolder: Emu.dir(filePath)
                fileMode: QuickDialogs.FileDialog.SaveFile
                onAccepted: {
                    filePath = Emu.toLocalFile(selectedFile);
                    forceRefresh++;
                }
            }
        }

        onClicked: {
            if(showCreateButton)
                parent.create()
            else {
                createDialogLoader.pendingOpen = true;
                createDialogLoader.active = true;
                parent.openDialog(createDialogLoader);
            }
        }
    }

    IconButton {
        iconSource: "qrc:/icons/resources/icons/document-edit.png"
        onClicked: {
            if(Qt.platform.os === "wasm" && selectExisting) {
                requestWasmImport();
                return;
            }
            dialogLoader.pendingOpen = true;
            dialogLoader.active = true;
            parent.openDialog(dialogLoader);
        }
    }
}
