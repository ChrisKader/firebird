import QtQuick 6.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    id: rectangle3
    property alias text1: nbutton1.text
    property alias text2: nbutton2.text
    property alias svgPath1: nbutton1.svgPath
    property alias svgPath2: nbutton2.svgPath
    property alias topText1: textTop1.text
    property alias topText2: textTop2.text
    property string topSvgPath1: ""
    property string topSvgPath2: ""
    property alias button1: nbutton1
    property alias button2: nbutton2
    property alias id1: nbutton1.keymap_id
    property alias id2: nbutton2.keymap_id

    width: 50
    height: 30
    color: "transparent"

    NButton {
        id: nbutton1
        x: 0
        y: 10
        keymap_id: 1
        width: 25
        height: 20
        text: "a"
    }

    NButton {
        id: nbutton2
        keymap_id: 2
        x: 25
        y: 10
        width: 25
        height: 20
        text: "b"
    }

    Rectangle {
        id: rectangle1
        width: 8
        height: 20
        color: FBUI.Theme.surface
        radius: 0
        anchors.verticalCenterOffset: 5
        anchors.horizontalCenterOffset: 0
        z: 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        border.width: 1
        border.color: FBUI.Theme.border
    }

    Rectangle {
        id: rectangle2
        width: 8
        height: 18
        color: FBUI.Theme.sunken
        radius: 0
        anchors.verticalCenterOffset: 5
        anchors.horizontalCenterOffset: 0
        z: 1
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        id: textTop1
        x: 0
        y: -2
        width: 25
        height: labelSvg1.height
        color: FBUI.Theme.accent
        text: "aa"
        visible: topSvgPath1 === ""
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        z: -1
        font.pixelSize: 8
        font.bold: true
        opacity: 0.9
    }

    SecondaryLabel {
        id: labelSvg1
        placement: "none"
        x: 0
        y: -2
        width: 25
        visible: topSvgPath1 !== ""
        pathData: topSvgPath1
        z: -1
        opacity: 0.9
    }

    Text {
        id: textTop2
        x: 25
        y: -2
        width: 25
        height: labelSvg2.height
        color: FBUI.Theme.accent
        text: "bb"
        visible: topSvgPath2 === ""
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: 8
        z: -2
        font.bold: true
        opacity: 0.9
    }

    SecondaryLabel {
        id: labelSvg2
        placement: "none"
        x: 25
        y: -2
        width: 25
        visible: topSvgPath2 !== ""
        pathData: topSvgPath2
        z: -2
        opacity: 0.9
    }
}
