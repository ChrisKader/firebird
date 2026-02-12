#include "lcdwidget.h"
#include "core/keypad.h"
#include "ui/keypadbridge.h"
#include "app/qmlbridge.h"
#include "ui/framebuffer.h"

LCDWidget::LCDWidget(QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f)
{
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

void LCDWidget::mousePressEvent(QMouseEvent *event)
{
    if (QMLBridge *bridge = qmlBridgeInstance()) {
        bridge->setTouchpadState((qreal)event->x() / width(), (qreal)event->y() / height(), true,
                                 event->button() == Qt::RightButton);
    }
}

void LCDWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::RightButton)
        keypad.touchpad_down = keypad.touchpad_contact = false;
    else
        keypad.touchpad_contact = false;

    if (QMLBridge *bridge = qmlBridgeInstance())
        bridge->touchpadStateChanged();
    keypad.kpc.gpio_int_active |= 0x800;
    keypad_int_check();
}

void LCDWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (QMLBridge *bridge = qmlBridgeInstance()) {
        bridge->setTouchpadState((qreal)event->x() / width(), (qreal)event->y() / height(),
                                 keypad.touchpad_contact, keypad.touchpad_down);
    }
}

void LCDWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
}

void LCDWidget::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
}

void LCDWidget::closeEvent(QCloseEvent *e)
{
    QWidget::closeEvent(e);

    emit closed();
}

void LCDWidget::resizeEvent(QResizeEvent *)
{
    int percent = qRound(qMin(width() / 320.0, height() / 240.0) * 100.0);
    emit scaleChanged(percent);
}

void LCDWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintFramebuffer(&painter);
    painter.save();
    QPen pen(palette().color(QPalette::Mid));
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.restore();
}
