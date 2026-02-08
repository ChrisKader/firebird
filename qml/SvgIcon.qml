import QtQuick 6.0
import QtQuick.Shapes 6.0

Item {
    id: root
    property string pathData: ""
    property color fillColor: "white"
    property real padding: 0.20

    // Parse optional "Dw,h " prefix encoding normalized path dimensions.
    // Paths are uniformly normalized to [0,normW]x[0,normH] where max(normW,normH)=1.
    readonly property var _dims: {
        var d = pathData;
        if (d.length > 1 && d.charAt(0) === 'D') {
            var sp = d.indexOf(' ');
            if (sp > 0) {
                var parts = d.substring(1, sp).split(',');
                if (parts.length === 2)
                    return { w: parseFloat(parts[0]), h: parseFloat(parts[1]), s: sp + 1 };
            }
        }
        return { w: 1, h: 1, s: 0 };
    }
    readonly property string cleanPath: pathData.substring(_dims.s)

    Shape {
        id: shape
        visible: root.pathData !== ""
        preferredRendererType: Shape.CurveRenderer

        readonly property real padX: root.width * root.padding
        readonly property real padY: root.height * root.padding
        readonly property real drawW: Math.max(1, root.width - 2 * padX)
        readonly property real drawH: Math.max(1, root.height - 2 * padY)

        // Uniform scale: fit [0,normW]x[0,normH] path into drawW x drawH
        readonly property real nw: root._dims.w > 0 ? root._dims.w : 1
        readonly property real nh: root._dims.h > 0 ? root._dims.h : 1
        readonly property real fitScale: Math.min(drawW / nw, drawH / nh)
        readonly property real rendW: nw * fitScale
        readonly property real rendH: nh * fitScale

        x: padX + (drawW - rendW) / 2
        y: padY + (drawH - rendH) / 2
        width: rendW
        height: rendH

        ShapePath {
            fillColor: root.fillColor
            strokeColor: "transparent"
            scale: Qt.size(shape.fitScale, shape.fitScale)
            PathSvg { path: root.cleanPath }
        }
    }
}
