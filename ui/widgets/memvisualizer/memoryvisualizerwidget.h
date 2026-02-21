#ifndef MEMORYVISUALIZERWIDGET_H
#define MEMORYVISUALIZERWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QImage>
#include <stdint.h>

#include "ui/docking/dockstate.h"

class LegendWidget;

class MemoryVisualizerWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit MemoryVisualizerWidget(QWidget *parent = nullptr);
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

    enum BPP { BPP_1 = 0, BPP_4, BPP_8, BPP_16_RGB565 };

public slots:
    void refresh();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void renderImage();
    void updateZoomLabel();
    void handleImageClick(const QPoint &pos, bool rightButton);

    QLabel *m_imageLabel = nullptr;
    QLineEdit *m_addrEdit = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QComboBox *m_bppCombo = nullptr;
    QCheckBox *m_autoRefresh = nullptr;
    QToolBar *m_toolbar = nullptr;
    QTimer *m_timer;
    QImage m_image;

    uint32_t m_baseAddr = 0xC0000000;
    int m_imgWidth = 320;
    int m_imgHeight = 240;
    int m_bpp = BPP_16_RGB565;

    /* Zoom */
    int m_zoomLevel = 2;
    QToolButton *m_zoomInBtn = nullptr;
    QToolButton *m_zoomOutBtn = nullptr;
    QLabel *m_zoomLabel = nullptr;

    /* Legend */
    LegendWidget *m_legendBar = nullptr;

    /* Click-to-highlight */
    uint32_t m_highlightAddr = 0;
    bool m_hasHighlight = false;
    QPoint m_highlightPixel;
    QLabel *m_valueLabel = nullptr;
};

/* Custom painted legend bar showing the color mapping for current BPP */
class LegendWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LegendWidget(QWidget *parent = nullptr);
    void setBpp(int bpp);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_bpp = MemoryVisualizerWidget::BPP_16_RGB565;
};

#endif // MEMORYVISUALIZERWIDGET_H
