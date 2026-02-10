import Firebird.Emu 1.0

import QtQuick 6.0
import QtQuick.Layouts 6.0
import Firebird.UIComponents 1.0 as FBUI

Rectangle {
    id: rectangle1
    width: 265
    height: 354

    readonly property color accent: FBUI.Theme.accent
    readonly property color accentMuted: FBUI.Theme.accentMuted

    SvgPaths { id: svgPaths }

    color: FBUI.Theme.background

    ColumnLayout {
        id: columnLayout1
        width: 33
        anchors.bottom: rectangle3.bottom
        anchors.bottomMargin: 0
        z: 4
        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.top: rectangle3.top
        anchors.topMargin: 14
        // This defaults to 5 and is affected by scaling,
        // which is not desired here as the keypad is scaled
        // by its parent already.
        spacing: 0

        NBigButton {
            id: nButton1
            text: "esc"
            svgPath: svgPaths.esc
            Layout.fillWidth: true
            keymap_id: 73

            SecondaryLabel { pathData: svgPaths.undo }
        }

        NBigButton {
            id: nButton2
            text: "pad"
            svgPath: svgPaths.scratchpad
            Layout.fillWidth: true
            keymap_id: 65

            SecondaryLabel { pathData: svgPaths.save }
        }

        NBigButton {
            id: nButton3
            text: "tab"
            svgPath: svgPaths.tab
            Layout.fillWidth: true
            keymap_id: 75
        }
    }

    ColumnLayout {
        id: columnLayout2
        x: -7
        width: 33
        anchors.bottom: rectangle3.bottom
        anchors.bottomMargin: 0
        z: 3
        anchors.top: rectangle3.top
        anchors.topMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 5
        // See above.
        spacing: 0

        NBigButton {
            id: nButton4
            text: "⌂on"
            svgPath: svgPaths.home
            Layout.fillWidth: true
            keymap_id: 9

            SecondaryLabel { pathData: svgPaths.pageBarOff }

            onClicked: {
                if(!Emu.isRunning)
                {
                    Emu.useDefaultKit();

                    if(Emu.getSnapshotPath() !== "")
                        Emu.resume();
                    else
                        Emu.restart();
                }
            }
        }

        NBigButton {
            id: nButton5
            text: "doc"
            svgPath: svgPaths.doc
            Layout.fillWidth: true
            keymap_id: 69

            SecondaryLabel { pathData: svgPaths.addPage }
        }

        NBigButton {
            id: nButton6
            text: "menu"
            svgPath: svgPaths.menu
            Layout.fillWidth: true
            keymap_id: 71

            SecondaryLabel {
                pathData: svgPaths.contextMenu
                width: 20
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    Touchpad {
        id: touchpad1
        width: gridLayout1.width
        border.color: FBUI.Theme.border
        anchors.bottom: rectangle3.bottom
        anchors.bottomMargin: 6
        z: 2
        anchors.horizontalCenter: gridLayout1.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 20
    }

    GridLayout {
        id: gridLayout1
        rowSpacing: 10
        rows: 5
        columns: 3
        columnSpacing: 10
        transformOrigin: Item.Center
        anchors.right: nDualButton7.left
        anchors.rightMargin: 18
        anchors.left: nDualButton1.right
        anchors.leftMargin: 18
        anchors.bottom: nButton8.bottom
        anchors.bottomMargin: 0
        anchors.top: rectangle3.bottom
        anchors.topMargin: 12

        NBigButton {
            id: nButton11
            Layout.preferredWidth: 33
            text: "shift"
            svgPath: svgPaths.shift
            keymap_id: 85

            SecondaryLabel { pathData: svgPaths.caps }
        }

        NBigButton {
            id: nButton12
            Layout.preferredWidth: 33
            text: "var"
            svgPath: svgPaths.varKey
            Layout.fillWidth: false
            border.width: 1
            clip: false
            keymap_id: 56
            Layout.column: 2

            SecondaryLabel { pathData: svgPaths.store }
        }

        NNumButton {
            id: nNumButton1
            text: "7"
            svgPath: svgPaths.num7
            keymap_id: 40
        }

        NNumButton {
            id: nNumButton2
            text: "8"
            svgPath: svgPaths.num8
            keymap_id: 72
        }

        NNumButton {
            id: nNumButton3
            text: "9"
            svgPath: svgPaths.num9
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            keymap_id: 36
        }

        NNumButton {
            id: nNumButton4
            text: "4"
            svgPath: svgPaths.num4
            keymap_id: 29
        }

        NNumButton {
            id: nNumButton5
            text: "5"
            svgPath: svgPaths.num5
            keymap_id: 61
        }

        NNumButton {
            id: nNumButton6
            text: "6"
            svgPath: svgPaths.num6
            keymap_id: 25
        }

        NNumButton {
            id: nNumButton7
            text: "1"
            svgPath: svgPaths.num1
            keymap_id: 18
        }

        NNumButton {
            id: nNumButton8
            text: "2"
            svgPath: svgPaths.num2
            keymap_id: 70
        }

        NNumButton {
            id: nNumButton9
            text: "3"
            svgPath: svgPaths.num3
            keymap_id: 14
        }

        NNumButton {
            id: nNumButton10
            text: "0"
            svgPath: svgPaths.num0
            keymap_id: 7
        }

        NNumButton {
            id: nNumButton11
            text: "."
            svgPath: svgPaths.decimal
            keymap_id: 59

            SecondaryLabel { pathData: svgPaths.capture }
        }

        NNumButton {
            id: nNumButton12
            text: "(-)"
            svgPath: svgPaths.negative
            keymap_id: 3

            SecondaryLabel { pathData: svgPaths.ans }
        }
    }

    NBigButton {
        id: nButton7
        width: 33
        text: "ctrl"
        svgPath: svgPaths.ctrl
        anchors.top: gridLayout1.top
        anchors.topMargin: 0
        keymap_id: 86
        anchors.left: parent.left
        anchors.leftMargin: 5
        active_color: rectangle1.accent
        back_color: rectangle1.accentMuted
        border_color: FBUI.Theme.border
        font_color: FBUI.Theme.textOnAccent
    }

    NBigButton {
        id: nButton10
        x: 230
        width: 33
        text: "del"
        svgPath: svgPaths.del
        anchors.top: gridLayout1.top
        anchors.topMargin: 0
        keymap_id: 64
        anchors.right: parent.right
        anchors.rightMargin: 5

        SecondaryLabel { pathData: svgPaths.clear }
    }

    GridLayout {
        id: gridLayout2
        anchors.top: gridLayout1.bottom
        anchors.topMargin: 11
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        anchors.left: nDualButton4.left
        anchors.leftMargin: 0
        anchors.right: nButton8.right
        anchors.rightMargin: 0
        rowSpacing: 0
        columnSpacing: 10
        columns: 9

        NAlphaButton { Layout.preferredWidth: 21; text: "EE"; svgPath: svgPaths.ee; keymap_id: 30 }
        NAlphaButton { text: "A"; svgPath: svgPaths.letterA; keymap_id: 50 }
        NAlphaButton { text: "B"; svgPath: svgPaths.letterB; keymap_id: 49 }
        NAlphaButton { text: "C"; svgPath: svgPaths.letterC; keymap_id: 48 }
        NAlphaButton { text: "D"; svgPath: svgPaths.letterD; keymap_id: 46 }
        NAlphaButton { text: "E"; svgPath: svgPaths.letterE; keymap_id: 45 }
        NAlphaButton { text: "F"; svgPath: svgPaths.letterF; keymap_id: 44 }
        NAlphaButton { text: "G"; svgPath: svgPaths.letterG; keymap_id: 39 }
        NAlphaButton { Layout.preferredWidth: 21; text: "?!▸"; svgPath: svgPaths.punctuation; keymap_id: 8 }
        NAlphaButton { Layout.preferredWidth: 21; text: "π▸"; svgPath: svgPaths.pi; keymap_id: 19 }
        NAlphaButton { text: "H"; svgPath: svgPaths.letterH; keymap_id: 38 }
        NAlphaButton { text: "I"; svgPath: svgPaths.letterI; keymap_id: 37 }
        NAlphaButton { text: "J"; svgPath: svgPaths.letterJ; keymap_id: 35 }
        NAlphaButton { text: "K"; svgPath: svgPaths.letterK; keymap_id: 34 }
        NAlphaButton { text: "L"; svgPath: svgPaths.letterL; keymap_id: 33 }
        NAlphaButton { text: "M"; svgPath: svgPaths.letterM; keymap_id: 28 }
        NAlphaButton { text: "N"; svgPath: svgPaths.letterN; keymap_id: 27 }
        NAlphaButton { Layout.preferredWidth: 21; text: ""; svgPath: svgPaths.flag; keymap_id: 66 }
        NAlphaButton { Layout.preferredWidth: 21; text: ","; svgPath: svgPaths.comma; keymap_id: 87 }
        NAlphaButton { text: "O"; svgPath: svgPaths.letterO; keymap_id: 26 }
        NAlphaButton { text: "P"; svgPath: svgPaths.letterP; keymap_id: 24 }
        NAlphaButton { text: "Q"; svgPath: svgPaths.letterQ; keymap_id: 23 }
        NAlphaButton { text: "R"; svgPath: svgPaths.letterR; keymap_id: 22 }
        NAlphaButton { text: "S"; svgPath: svgPaths.letterS; keymap_id: 17 }
        NAlphaButton { id: nAlphaButtonT; text: "T"; svgPath: svgPaths.letterT; keymap_id: 16 }
        NAlphaButton { id: nAlphaButtonU; text: "U"; svgPath: svgPaths.letterU; keymap_id: 15 }
        NAlphaButton { Layout.preferredWidth: 21; text: "↵"; svgPath: svgPaths.returnKey; keymap_id: 0 }
        Rectangle {
            color: "#00000000"
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
        }
        NAlphaButton { text: "V"; svgPath: svgPaths.letterV; keymap_id: 13 }
        NAlphaButton { text: "W"; svgPath: svgPaths.letterW; keymap_id: 12 }
        NAlphaButton { text: "X"; svgPath: svgPaths.letterX; keymap_id: 11 }
        NAlphaButton { text: "Y"; svgPath: svgPaths.letterY; keymap_id: 6 }
        NAlphaButton { text: "Z"; svgPath: svgPaths.letterZ; keymap_id: 5 }

        NAlphaButton {
            text: "space"
            svgPath: svgPaths.space
            Layout.fillWidth: true
            keymap_id: 4
            Layout.columnSpan: 2
        }
    }

    NDualButton {
        id: nDualButton1
        x: 5
        anchors.top: nButton7.bottom
        anchors.topMargin: 0
        topText1: "|≠≥>"
        topText2: ""
        topSvgPath1: svgPaths.inequality
        id1: 51
        id2: 20
        anchors.left: parent.left
        anchors.leftMargin: 5
        text2: "trig"
        text1: "="
        svgPath1: svgPaths.equal
        svgPath2: svgPaths.trig

        SecondaryLabel {
            placement: "none"
            pathData: svgPaths.hintsQuestion
            width: 14; height: 14
            padding: 0
            anchors.bottom: nDualButton1.button2.top
            anchors.horizontalCenter: nDualButton1.button2.horizontalCenter
        }
    }

    NDualButton {
        id: nDualButton2
        x: 5
        topText1: "<sup>n<sup>√x"
        topText2: "√"
        topSvgPath1: svgPaths.nthRoot
        topSvgPath2: svgPaths.sqrt
        anchors.top: nDualButton1.bottom
        anchors.topMargin: 0
        id1: 53
        id2: 31
        anchors.left: parent.left
        anchors.leftMargin: 5
        text2: "x<sup>2</sup>"
        text1: "^"
        svgPath1: svgPaths.caret
        svgPath2: svgPaths.xSquared
    }

    NDualButton {
        id: nDualButton3
        x: 5
        topText2: "log"
        topText1: "ln"
        topSvgPath1: svgPaths.ln
        topSvgPath2: svgPaths.log
        anchors.top: nDualButton2.bottom
        anchors.topMargin: 0
        id1: 42
        id2: 21
        anchors.left: parent.left
        anchors.leftMargin: 5
        text2: "10<sup>x</sup>"
        text1: "e<sup>x</sup>"
        svgPath1: svgPaths.eToTheX
        svgPath2: svgPaths.tenToTheX
    }

    NDualButton {
        id: nDualButton4
        topText2: "{ }"
        topText1: "[ ]"
        topSvgPath1: svgPaths.brackets
        topSvgPath2: svgPaths.braces
        anchors.top: nDualButton3.bottom
        anchors.topMargin: 0
        id1: 60
        id2: 58
        anchors.left: parent.left
        anchors.leftMargin: 5
        text2: ")"
        text1: "("
        svgPath1: svgPaths.parenLeft
        svgPath2: svgPaths.parenRight
    }

    NDualButton {
        id: nDualButton5
        x: 210
        anchors.top: nButton10.bottom
        anchors.topMargin: 0
        topText2: "∞β°"
        topText1: ":="
        topSvgPath1: svgPaths.define
        topSvgPath2: svgPaths.symbolTemplate
        id1: 63
        id2: 62
        anchors.right: parent.right
        anchors.rightMargin: 5
        text2: ""
        text1: ""
        svgPath1: svgPaths.templates
        svgPath2: svgPaths.catalog
    }

    NDualButton {
        id: nDualButton6
        x: 210
        topText2: "÷"
        topText1: "\"□\""
        topSvgPath1: svgPaths.mathBox
        topSvgPath2: svgPaths.fractionTemplate
        anchors.top: nDualButton5.bottom
        anchors.topMargin: 0
        id1: 52
        id2: 41
        anchors.right: parent.right
        anchors.rightMargin: 5
        text2: "÷"
        text1: "×"
        svgPath1: svgPaths.multiply
        svgPath2: svgPaths.divide
    }

    NDualButton {
        id: nDualButton7
        x: 210
        topText2: ""
        topText1: ""
        anchors.top: nDualButton6.bottom
        anchors.topMargin: 0
        id1: 68
        id2: 57
        anchors.right: parent.right
        anchors.rightMargin: 5
        text2: "‒"
        text1: "+"
        svgPath1: svgPaths.plus
        svgPath2: svgPaths.minus

        SecondaryLabel {
            placement: "none"
            pathData: svgPaths.contrastUp
            width: 8; height: 8
            padding: 0
            anchors.bottom: nDualButton7.button1.top
            anchors.horizontalCenter: nDualButton7.button1.horizontalCenter
        }

        SecondaryLabel {
            placement: "none"
            pathData: svgPaths.contrastDown
            width: 8; height: 8
            padding: 0
            anchors.bottom: nDualButton7.button2.top
            anchors.horizontalCenter: nDualButton7.button2.horizontalCenter
        }
    }

    NButton {
        id: nButton8
        x: 210
        anchors.right: parent.right
        anchors.rightMargin: 5
        width: 50
        height: 20
        text: "enter"
        svgPath: svgPaths.enter
        anchors.top: nDualButton7.bottom
        anchors.topMargin: 10

        SecondaryLabel {
            pathData: svgPaths.approximate
            width: 50
            anchors.bottom: nButton8.top
            anchors.bottomMargin: 1
        }
    }

    Rectangle {
        id: rectangle3
        height: 104
        color: FBUI.Theme.surface
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.left: parent.left
        anchors.leftMargin: 0
        anchors.right: parent.right
        anchors.rightMargin: 0
        z: 1
    }
}
