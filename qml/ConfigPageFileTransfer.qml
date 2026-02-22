
import QtQuick 6.0
import QtQuick.Controls 6.0
import Qt.labs.platform 1.1 as Platform
import QtQuick.Dialogs 6.2 as QuickDialogs
import QtQuick.Layouts 6.0
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0

ColumnLayout {
    id: transferPage
    spacing: 5
    readonly property int normalSize: (TextMetrics && TextMetrics.normalSize ? TextMetrics.normalSize : 13)
    readonly property int title2Size: (TextMetrics && TextMetrics.title2Size ? TextMetrics.title2Size : 18)
    property int wasmImportCounter: 0
    property string wasmPendingImportRequestId: ""

    function openDialog(loader) {
        if(!loader.item)
            return;
        if(loader.item.open)
            loader.item.open();
        else
            loader.item.visible = true;
    }

    function startTransfer(urls) {
        transferStatus.text = qsTr("Starting");
        transferProgress.indeterminate = true;
        for (let i = 0; i < urls.length; ++i) {
            let source = urls[i];
            if(typeof source === "string")
                source = "file://" + source;
            Emu.sendFile(source, Emu.usbdir);
        }
    }

    function requestWasmTransferImport() {
        wasmImportCounter++;
        wasmPendingImportRequestId = "transfer-import-" + wasmImportCounter;
        Emu.importLocalFileForWasm(wasmPendingImportRequestId, "");
    }

    FBLabel {
        text: qsTr("File Transfer")
        font.pixelSize: transferPage.title2Size
        Layout.topMargin: 5
        Layout.bottomMargin: 5
    }

    FBLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("If you are unable to use the main window's file transfer using either drag'n'drop or the file explorer, you can send files here.")
        font.pixelSize: transferPage.normalSize
        visible: !Emu.isMobile()
    }

    FBLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Here you can send files into the target folder specified below.")
        font.pixelSize: transferPage.normalSize
    }

    Loader {
        id: fileDialogLoader
        active: false
        sourceComponent: Qt.platform.os === "wasm" ? quickOpenDialogComponent : platformOpenDialogComponent
        property bool pendingOpen: false
        onLoaded: {
            if (pendingOpen) {
                pendingOpen = false;
                transferPage.openDialog(fileDialogLoader);
            }
        }
    }

    Connections {
        target: Emu
        function onWasmLocalFileImported(requestId, localPath, errorText) {
            if(requestId !== wasmPendingImportRequestId)
                return;
            wasmPendingImportRequestId = "";
            if(localPath !== "")
                transferPage.startTransfer([localPath]);
            else {
                transferStatus.text = errorText !== "" ? errorText : qsTr("File import cancelled");
                transferProgress.indeterminate = false;
            }
        }
    }

    Component {
        id: platformOpenDialogComponent
        Platform.FileDialog {
            nameFilters: [ qsTr("TNS Documents") +"(*.tns)", qsTr("Operating Systems") + "(*.tno *.tnc *.tco *.tcc *.tlo *.tmo *.tmc *.tco2 *.tcc2 *.tct2)" ]
            fileMode: Platform.FileDialog.OpenFiles
            onAccepted: transferPage.startTransfer(currentFiles)
        }
    }

    Component {
        id: quickOpenDialogComponent
        QuickDialogs.FileDialog {
            nameFilters: [ qsTr("TNS Documents") +"(*.tns)", qsTr("Operating Systems") + "(*.tno *.tnc *.tco *.tcc *.tlo *.tmo *.tmc *.tco2 *.tcc2 *.tct2)" ]
            fileMode: QuickDialogs.FileDialog.OpenFiles
            onAccepted: transferPage.startTransfer(selectedFiles)
        }
    }

    RowLayout {
        Layout.fillWidth: true

        Button {
            text: qsTr("Send files")
            // If this button is disabled, the transfer directory textinput has the focus again,
            // which is annoying on mobile.
            // enabled: Emu.isRunning
            Layout.topMargin: 5
            Layout.bottomMargin: 5
            onClicked: {
                if(Qt.platform.os === "wasm") {
                    requestWasmTransferImport();
                    return;
                }
                fileDialogLoader.pendingOpen = true;
                fileDialogLoader.active = true;
                transferPage.openDialog(fileDialogLoader);
            }
        }

        Button {
            text: qsTr("Leave Press-to-Test mode")
            Layout.topMargin: 5
            Layout.bottomMargin: 5
            onClicked: {
                Emu.sendExitPTT();
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        FBLabel {
            id: transferStatusLabel
            text: qsTr("Status:")
        }

        FBLabel {
            id: transferStatus
            Layout.fillWidth: true
            text: qsTr("Idle")
        }

        Connections {
            target: Emu
            function onUsblinkProgressChanged(percent) {
                if(percent < 0) {
                    transferStatus.text = qsTr("Failed!");
                    transferProgress.value = 0;
                    transferProgress.indeterminate = false;
                } else {
                    transferStatus.text = (percent >= 100) ? qsTr("Done!") : (percent + "%");
                    transferProgress.value = percent;
                    transferProgress.indeterminate = false;
                }
            }
        }
    }

    ProgressBar {
        id: transferProgress
        Layout.fillWidth: true
        from: 0
        to: 100
    }

    FBLabel {
        text: qsTr("Target Directory")
        font.pixelSize: transferPage.title2Size
        Layout.topMargin: 5
        Layout.bottomMargin: 5
    }

    FBLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("When dragging files onto Firebird, it will try to send them to the emulated system.")
        font.pixelSize: transferPage.normalSize
    }

    RowLayout {
        Layout.fillWidth: true

        FBLabel {
            Layout.fillWidth: true
            text: qsTr("Target folder for dropped files:")
            wrapMode: Text.WordWrap
        }

        TextField {
            Layout.fillWidth: true
            text: Emu.usbdir
            onTextChanged: {
                Emu.usbdir = text
                text = Qt.binding(function() { return Emu.usbdir; });
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
