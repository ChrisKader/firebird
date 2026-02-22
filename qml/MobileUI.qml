
import Firebird.Emu 1.0
import Firebird.UIComponents 1.0

import QtQuick 6.0
import QtQuick.Controls 6.0
import QtQml 2.15
import Qt.labs.platform 1.1 as Platform

ApplicationWindow {
    id: app
    title: "Firebird Emu"
    visible: true

    property bool closeAfterSuspend: false
    property bool ignoreSuspendOnClose: false

    onXChanged: Emu.mobileX = x
    onYChanged: Emu.mobileY = y
    width: Emu.mobileWidth != -1 ? Emu.mobileWidth : 320
    onWidthChanged: Emu.mobileWidth = width
    height: Emu.mobileHeight != -1 ? Emu.mobileHeight : 480
    onHeightChanged: Emu.mobileHeight = height

    minimumWidth: 320
    minimumHeight: 480

    Component.onCompleted: {
        if(Emu.isMobile())
        {
            Emu.useDefaultKit();

            if(Emu.autostart
                && Emu.getFlashPath() !== ""
                && Emu.getBoot1Path() !== "")
            {
                if(Emu.getSnapshotPath() !== "")
                    Emu.resume();
                else
                    Emu.restart();
            }
        }
        else
        {
            if(Emu.mobileX != -1)
                x = Emu.mobileX;

            if(Emu.mobileY != -1)
                y = Emu.mobileY;
        }
    }

    onClosing: {
        if(!Emu.isRunning || !Emu.suspendOnClose || ignoreSuspendOnClose)
            return;

        closeAfterSuspend = true;
        Emu.suspend();
        close.accepted = false;
    }

    Platform.MessageDialog {
        id: suspendFailedDialog
        buttons: Platform.MessageDialog.Yes | Platform.MessageDialog.No
        title: qsTr("Suspend failed")
        text: qsTr("Suspending the emulation failed. Do you still want to quit Firebird?")

        onYesClicked: {
            app.ignoreSuspendOnClose = true;
            app.close();
        }
    }

    Connections {
        target: Emu
        function onEmuSuspended(success) {
            if(app.closeAfterSuspend)
            {
                app.closeAfterSuspend = false;

                if(success)
                {
                    app.ignoreSuspendOnClose = true;
                    app.close();
                }
                else
                    suspendFailedDialog.visible = true;
            }
        }
        function onToastMessage(msg) {
            toast.showMessage(msg);
        }
    }

    Connections {
        target: Qt.application
        function onStateChanged() {
            // qmllint disable missing-property
            switch (Qt.application.state)
            {
                case Qt.ApplicationSuspended: // Might be reaped on mobile
                    // fallthrough
                case Qt.ApplicationInactive: // Not focused
                    // fallthrough
                case Qt.ApplicationHidden: // Not visible
                    if(Emu.isMobile()) // Keep running on desktop
                        Emu.setPaused(true);
                break;
                case Qt.ApplicationActive: // Visible and in focus
                    if(Emu.isMobile()) // Only unpause if paused
                        Emu.setPaused(false);
                break;
            }
            // qmllint enable missing-property
        }
    }

    Toast {
        id: toast
        x: 60
        z: 1

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 61
        anchors.horizontalCenter: parent.horizontalCenter
    }

    ListView {
        id: listView

        focus: true

        anchors.fill: parent
        orientation: Qt.Horizontal
        snapMode: ListView.SnapOneItem
        boundsBehavior: ListView.StopAtBounds
        pixelAligned: true

        // Keep the pages alive without feedback loops
        cacheBuffer: width * model.length

        model: [ "MobileUIConfig.qml", "MobileUIDrawer.qml", "MobileUIFront.qml" ]

        /* The delegates write their X offsets into this array, so that we can use
           them as values for contentX. */
        property var pageX: []

        Component.onCompleted: {
            // Open drawer if emulation does not start automatically
            Emu.useDefaultKit(); // We need this here to get the default values
            if(!Emu.autostart
               || Emu.getBoot1Path() === ""
               || Emu.getFlashPath() === "")
                positionViewAtIndex(1, ListView.SnapPosition);
            else
                positionViewAtIndex(2, ListView.SnapPosition);
        }

        NumberAnimation {
            id: anim
            target: listView
            property: "contentX"
            duration: 200
            easing.type: Easing.InQuad
        }

        function animateToIndex(i) {
            anim.to = pageX[i];
            anim.from = listView.contentX;
            anim.start();
        }

        function closeDrawer() {
            animateToIndex(2);
        }

        function openDrawer() {
            animateToIndex(1);
        }

        function openConfiguration() {
            animateToIndex(0);
        }

        delegate: Item {
            id: delegateItem
            required property int index
            required property var modelData
            // The pages are expensive, keep them
            ListView.delayRemove: true

            Component.onCompleted: {
                listView.pageX[index] = x;
            }

            onXChanged: {
                listView.pageX[index] = x;
            }

            width: modelData === "MobileUIDrawer.qml" ? 250 : app.width
            height: app.height

            Rectangle {
                id: overlay
                z: 1
                anchors.fill: parent
                color: "black"
                opacity: {
                    var xOffset = listView.contentX - parent.x;
                    return Math.min(Math.max(0.0, Math.abs(xOffset) / listView.width), 0.6);
                }
                visible: opacity > 0.01

                MouseArea {
                    anchors.fill: parent
                    enabled: parent.visible

                    onReleased: {
                        listView.animateToIndex(delegateItem.index)
                    }
                }
            }

            Loader {
                id: loader
                z: 0
                focus: Math.round(parent.x) == Math.round(listView.contentX);
                anchors.fill: parent
                source: delegateItem.modelData
            }
        }
    }
}
