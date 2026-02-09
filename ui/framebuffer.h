#ifndef QMLFRAMEBUFFER_H
#define QMLFRAMEBUFFER_H

#include <QQuickPaintedItem>

enum class LCDScaleMode {
    NearestNeighbor,
    Bilinear,
    SharpBilinear
};

extern LCDScaleMode lcd_scale_mode;

class QMLFramebuffer : public QQuickPaintedItem
{
public:
    explicit QMLFramebuffer(QQuickItem *parent = 0);
    virtual void paint(QPainter *p) override;
};

QImage renderFramebuffer();
void paintFramebuffer(QPainter *p);

#endif // QMLFRAMEBUFFER_H
