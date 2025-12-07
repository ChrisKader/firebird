import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0
import "KitRoles.js" as KitRoles

ColumnLayout {
    id: emuPage
    spacing: 5
    readonly property int normalSize: (TextMetrics && TextMetrics.normalSize ? TextMetrics.normalSize : 13)
    readonly property int title1Size: (TextMetrics && TextMetrics.title1Size ? TextMetrics.title1Size : 16)
    readonly property int title2Size: (TextMetrics && TextMetrics.title2Size ? TextMetrics.title2Size : 18)

    FBLabel {
        text: qsTr("Startup")
        font.pixelSize: emuPage.title2Size
        Layout.topMargin: 5
        Layout.bottomMargin: 5
    }

    FBLabel {
        // The hell, QML!
        Layout.maximumWidth: parent.width
        wrapMode: Text.WordWrap
        text: {
            if(!Emu.isMobile())
                return qsTr("When opening Firebird, the selected Kit will be started. If available, it will resume the emulation from the provided snapshot.")
            else
                return qsTr("Choose the Kit selected on startup and after restarting. If the checkbox is active, it will be launched when Firebird starts.")
        }

        font.pixelSize: emuPage.normalSize
    }

    RowLayout {
        // No spacing so that the spin box looks like part of the label
        spacing: 0
        Layout.fillWidth: true

        CheckBox {
            Layout.maximumWidth: parent.parent.width - startupKit.width
            text: qsTr("On Startup, run Kit")

            checked: Emu.autostart
            onCheckedChanged: Emu.autostart = checked
        }

        ComboBox {
            id: startupKit
            Layout.maximumWidth: parent.parent.width * 0.4
            textRole: "name"
            model: Emu.kits
            currentIndex: Emu.kitIndexForID(Emu.defaultKit)
            onCurrentIndexChanged: {
                Emu.defaultKit = model.getDataRow(currentIndex, KitRoles.IDRole);
                currentIndex = Qt.binding(function() { return Emu.kitIndexForID(Emu.defaultKit); });
            }
        }
    }

    FBLabel {
        text: qsTr("Shutdown")
        font.pixelSize: emuPage.title1Size
        Layout.topMargin: 10
        Layout.bottomMargin: 5
        visible: Qt.platform.os !== "ios"
    }

    FBLabel {
        Layout.maximumWidth: parent.width
        wrapMode: Text.WordWrap
        text: {
            if(Qt.platform.os === "android")
                return qsTr("When closing firebird using the back button, save the current state to the current snapshot. Does not work when firebird is in the background.")
            else
                return qsTr("On Application end, save the current state to the current snapshot.");
        }
        font.pixelSize: emuPage.normalSize
        visible: Qt.platform.os !== "ios"
    }

    CheckBox {
        text: qsTr("Save snapshot on shutdown")

        checked: Emu.suspendOnClose
        visible: Qt.platform.os !== "ios"
        onCheckedChanged: {
            Emu.suspendOnClose = checked;
            checked = Qt.binding(function() { return Emu.suspendOnClose; });
        }
    }

    FBLabel {
        text: qsTr("UI Preferences")
        font.pixelSize: emuPage.title2Size
        Layout.topMargin: 10
        Layout.bottomMargin: 5
        visible: Emu.isMobile()
    }

    FBLabel {
        Layout.maximumWidth: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Change the side of the keypad in landscape orientation.")
        font.pixelSize: emuPage.normalSize
        visible: Emu.isMobile()
    }

    CheckBox {
        text: qsTr("Left-handed mode")

        checked: Emu.leftHanded
        visible: Emu.isMobile()
        onCheckedChanged: {
            Emu.leftHanded = checked;
            checked = Qt.binding(function() { return Emu.leftHanded; });
        }
    }

    FBLabel {
        text: qsTr("Appearance")
        font.pixelSize: emuPage.title2Size
        Layout.topMargin: 10
        Layout.bottomMargin: 5
    }

    CheckBox {
        text: qsTr("Dark mode")

        checked: Emu.darkTheme
        onCheckedChanged: {
            Emu.darkTheme = checked;
            checked = Qt.binding(function() { return Emu.darkTheme; });
        }
    }

    Item {
        Layout.fillHeight: true
    }

}
