pragma Singleton
import QtQuick 6.0

QtObject {
    // Default font sizes for Qt 6
    // Using typical default of 13pt as baseline
    readonly property int normalSize: 13
    readonly property int title1Size: Math.round(normalSize * 1.2)
    readonly property int title2Size: Math.round(normalSize * 1.4)
}
