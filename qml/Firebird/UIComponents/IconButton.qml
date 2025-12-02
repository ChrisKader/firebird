import QtQuick 6.0
import QtQuick.Controls 6.0
import Firebird.UIComponents 1.0 as FBUI

/* A push button with a symbol instead of text.
 * ToolButton and <img/> in Label don't size correctly,
 * so do it manually.
 * With QQC2, button icons have a better default size
 * and it can also be specified explicitly. */

Button {
    id: button
    property string iconSource: ""

    implicitHeight: FBUI.TextMetrics && FBUI.TextMetrics.normalSize ? FBUI.TextMetrics.normalSize * 2.5 : 32
    implicitWidth: implicitHeight

    Image {
        id: image
        source: button.iconSource
        height: Math.round(parent.height * 0.6)
        anchors.centerIn: parent

        fillMode: Image.PreserveAspectFit
    }
}
