import QtQuick 6.0
import QtQuick.Controls 6.0
import Firebird.UIComponents 1.0 as FBUI

Label {
    color: FBUI.Theme.text
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "system"
}
