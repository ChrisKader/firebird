import QtQuick 6.0
import QtQuick.Controls 6.0
import Firebird.UIComponents 1.0 as FBUI

ScrollView {
    id: controls
    property alias keypad: keypad
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
    clip: true

    background: Rectangle {
        color: FBUI.Theme.background
    }

    Flickable {
        flickableDirection: Flickable.VerticalFlick

        contentWidth: parent.width
        contentHeight: keypad.height*controls.width/keypad.width

        Keypad {
            id: keypad
            transform: Scale { origin.x: 0; origin.y: 0; xScale: controls.width/keypad.width; yScale: controls.width/keypad.width }
        }
    }
}
