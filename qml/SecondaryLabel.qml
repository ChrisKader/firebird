import QtQuick 6.0
import Firebird.UIComponents 1.0 as FBUI

SvgIcon {
    id: root
    // Placement relative to parent: "top", "bottom", "left", "right", "none"
    property string placement: "top"
    property real margin: 0

    width: 33
    height: 11
    fillColor: FBUI.Theme.accent
    padding: 0.15

    anchors.bottom: placement === "top" ? parent.top : undefined
    anchors.top: placement === "bottom" ? parent.bottom : undefined
    anchors.right: placement === "left" ? parent.left : undefined
    anchors.left: placement === "right" ? parent.right : undefined
    anchors.horizontalCenter: (placement === "top" || placement === "bottom") ? parent.horizontalCenter : undefined
    anchors.verticalCenter: (placement === "left" || placement === "right") ? parent.verticalCenter : undefined
    anchors.bottomMargin: margin
    anchors.topMargin: margin
    anchors.rightMargin: margin
    anchors.leftMargin: margin
}
