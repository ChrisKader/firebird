#include "memoryvisualizerwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QMouseEvent>
#include <QPainter>
#include <QFontDatabase>
#include <QJsonObject>

#include "core/debug_api.h"
#include "ui/theme/widgettheme.h"

/* -- LegendWidget -------------------------------------------- */

LegendWidget::LegendWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(24);
}

void LegendWidget::setBpp(int bpp)
{
    m_bpp = bpp;
    update();
}

void LegendWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();
    const WidgetTheme &t = currentWidgetTheme();
    p.fillRect(rect(), t.surface);

    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);

    switch (m_bpp) {
    case MemoryVisualizerWidget::BPP_1: {
        int boxW = 20;
        p.fillRect(4, 2, boxW, h - 4, Qt::black);
        p.setPen(t.text);
        p.drawText(4 + boxW + 4, h - 4, QStringLiteral("0"));
        p.fillRect(4 + boxW + 20, 2, boxW, h - 4, Qt::white);
        p.drawText(4 + boxW * 2 + 24, h - 4, QStringLiteral("1"));
        break;
    }
    case MemoryVisualizerWidget::BPP_4: {
        int barW = w - 60;
        for (int i = 0; i < barW; i++) {
            int val = i * 15 / barW;
            uint8_t gray = val * 17;
            p.setPen(QColor(gray, gray, gray));
            p.drawLine(30 + i, 2, 30 + i, h - 4);
        }
        p.setPen(t.text);
        p.drawText(4, h - 4, QStringLiteral("0"));
        p.drawText(30 + barW + 4, h - 4, QStringLiteral("F"));
        break;
    }
    case MemoryVisualizerWidget::BPP_8: {
        int barW = w - 60;
        for (int i = 0; i < barW; i++) {
            uint8_t val = i * 255 / barW;
            p.setPen(QColor(val, val, val));
            p.drawLine(30 + i, 2, 30 + i, h - 4);
        }
        p.setPen(t.text);
        p.drawText(4, h - 4, QStringLiteral("00"));
        p.drawText(30 + barW + 4, h - 4, QStringLiteral("FF"));
        break;
    }
    case MemoryVisualizerWidget::BPP_16_RGB565: {
        int barW = (w - 20) / 3;
        int x = 4;
        /* Red */
        for (int i = 0; i < barW; i++) {
            uint8_t r = i * 255 / barW;
            p.setPen(QColor(r, 0, 0));
            p.drawLine(x + i, 2, x + i, h - 4);
        }
        p.setPen(t.text);
        p.drawText(x, h - 4, QStringLiteral("R5"));
        x += barW + 4;
        /* Green */
        for (int i = 0; i < barW; i++) {
            uint8_t g = i * 255 / barW;
            p.setPen(QColor(0, g, 0));
            p.drawLine(x + i, 2, x + i, h - 4);
        }
        p.setPen(t.text);
        p.drawText(x, h - 4, QStringLiteral("G6"));
        x += barW + 4;
        /* Blue */
        for (int i = 0; i < barW; i++) {
            uint8_t b = i * 255 / barW;
            p.setPen(QColor(0, 0, b));
            p.drawLine(x + i, 2, x + i, h - 4);
        }
        p.setPen(t.text);
        p.drawText(x, h - 4, QStringLiteral("B5"));
        break;
    }
    }
}

/* -- MemoryVisualizerWidget ---------------------------------- */

MemoryVisualizerWidget::MemoryVisualizerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    /* Address input */
    auto *addrLabel = new QLabel(tr("Addr:"), m_toolbar);
    m_toolbar->addWidget(addrLabel);
    m_addrEdit = new QLineEdit(m_toolbar);
    m_addrEdit->setPlaceholderText(QStringLiteral("hex address"));
    m_addrEdit->setMaximumWidth(100);
    m_addrEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9a-fA-F]{1,8}")), m_addrEdit));
    m_addrEdit->setText(QStringLiteral("%1").arg(m_baseAddr, 8, 16, QLatin1Char('0')));
    m_toolbar->addWidget(m_addrEdit);
    m_toolbar->addSeparator();

    /* Width */
    auto *wLabel = new QLabel(tr("W:"), m_toolbar);
    m_toolbar->addWidget(wLabel);
    m_widthSpin = new QSpinBox(m_toolbar);
    m_widthSpin->setRange(1, 1024);
    m_widthSpin->setValue(m_imgWidth);
    m_toolbar->addWidget(m_widthSpin);

    /* Height */
    auto *hLabel = new QLabel(tr("H:"), m_toolbar);
    m_toolbar->addWidget(hLabel);
    m_heightSpin = new QSpinBox(m_toolbar);
    m_heightSpin->setRange(1, 1024);
    m_heightSpin->setValue(m_imgHeight);
    m_toolbar->addWidget(m_heightSpin);
    m_toolbar->addSeparator();

    /* BPP selector */
    m_bppCombo = new QComboBox(m_toolbar);
    m_bppCombo->addItem(tr("1 bpp"), BPP_1);
    m_bppCombo->addItem(tr("4 bpp"), BPP_4);
    m_bppCombo->addItem(tr("8 bpp"), BPP_8);
    m_bppCombo->addItem(tr("16 bpp RGB565"), BPP_16_RGB565);
    m_bppCombo->setCurrentIndex(BPP_16_RGB565);
    m_toolbar->addWidget(m_bppCombo);
    m_toolbar->addSeparator();

    /* Zoom controls */
    m_zoomOutBtn = new QToolButton(m_toolbar);
    m_zoomOutBtn->setText(QStringLiteral("-"));
    m_zoomOutBtn->setToolTip(tr("Zoom Out"));
    m_toolbar->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("2x"), m_toolbar);
    m_zoomLabel->setMinimumWidth(24);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_toolbar->addWidget(m_zoomLabel);

    m_zoomInBtn = new QToolButton(m_toolbar);
    m_zoomInBtn->setText(QStringLiteral("+"));
    m_zoomInBtn->setToolTip(tr("Zoom In"));
    m_toolbar->addWidget(m_zoomInBtn);
    m_toolbar->addSeparator();

    /* Auto-refresh toggle */
    m_autoRefresh = new QCheckBox(tr("Auto"), m_toolbar);
    m_toolbar->addWidget(m_autoRefresh);

    /* Refresh button */
    auto *refreshBtn = new QPushButton(tr("Refresh"), m_toolbar);
    m_toolbar->addWidget(refreshBtn);
    connect(refreshBtn, &QPushButton::clicked, this, &MemoryVisualizerWidget::refresh);

    layout->addWidget(m_toolbar);

    /* Value label (click-to-highlight info) */
    m_valueLabel = new QLabel(this);
    m_valueLabel->setContentsMargins(4, 2, 4, 2);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    m_valueLabel->setFont(mono);
    m_valueLabel->setVisible(false);
    layout->addWidget(m_valueLabel);

    /* Image display area */
    auto *scrollArea = new QScrollArea(this);
    m_imageLabel = new QLabel(scrollArea);
    m_imageLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_imageLabel->installEventFilter(this);
    scrollArea->setWidget(m_imageLabel);
    scrollArea->setWidgetResizable(false);  /* allow label to size to pixmap */
    layout->addWidget(scrollArea, 1);

    /* Legend bar */
    m_legendBar = new LegendWidget(this);
    layout->addWidget(m_legendBar);

    /* Timer for auto-refresh */
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &MemoryVisualizerWidget::refresh);
    connect(m_autoRefresh, &QCheckBox::toggled, this, [this](bool on) {
        if (on) m_timer->start();
        else m_timer->stop();
    });

    /* Update parameters on change */
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { m_imgWidth = v; });
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { m_imgHeight = v; });
    connect(m_bppCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_bpp = m_bppCombo->itemData(idx).toInt();
                m_legendBar->setBpp(m_bpp);
            });

    /* Zoom controls */
    connect(m_zoomInBtn, &QToolButton::clicked, this, [this]() {
        if (m_zoomLevel < 8) {
            m_zoomLevel *= 2;
            updateZoomLabel();
            refresh();
        }
    });
    connect(m_zoomOutBtn, &QToolButton::clicked, this, [this]() {
        if (m_zoomLevel > 1) {
            m_zoomLevel /= 2;
            updateZoomLabel();
            refresh();
        }
    });
}

QJsonObject MemoryVisualizerWidget::serializeState() const
{
    QJsonObject state;
    state.insert(QStringLiteral("baseAddr"), QStringLiteral("%1").arg(m_baseAddr, 8, 16, QLatin1Char('0')));
    state.insert(QStringLiteral("imageWidth"), m_imgWidth);
    state.insert(QStringLiteral("imageHeight"), m_imgHeight);
    state.insert(QStringLiteral("bpp"), m_bpp);
    state.insert(QStringLiteral("zoom"), m_zoomLevel);
    if (m_autoRefresh)
        state.insert(QStringLiteral("autoRefresh"), m_autoRefresh->isChecked());
    return state;
}

void MemoryVisualizerWidget::restoreState(const QJsonObject &state)
{
    bool ok = false;
    uint32_t baseAddr = state.value(QStringLiteral("baseAddr")).toString().toUInt(&ok, 16);
    if (!ok) {
        const int fallback = state.value(QStringLiteral("baseAddr")).toInt(-1);
        if (fallback >= 0) {
            baseAddr = static_cast<uint32_t>(fallback);
            ok = true;
        }
    }
    if (ok) {
        m_baseAddr = baseAddr;
        if (m_addrEdit)
            m_addrEdit->setText(QStringLiteral("%1").arg(m_baseAddr, 8, 16, QLatin1Char('0')));
    }

    if (m_widthSpin)
        m_widthSpin->setValue(state.value(QStringLiteral("imageWidth")).toInt(m_widthSpin->value()));
    if (m_heightSpin)
        m_heightSpin->setValue(state.value(QStringLiteral("imageHeight")).toInt(m_heightSpin->value()));

    if (m_bppCombo) {
        const int bpp = state.value(QStringLiteral("bpp")).toInt(m_bpp);
        int idx = m_bppCombo->findData(bpp);
        if (idx < 0)
            idx = m_bppCombo->currentIndex();
        m_bppCombo->setCurrentIndex(idx);
    }

    int zoom = state.value(QStringLiteral("zoom")).toInt(m_zoomLevel);
    if (zoom < 1)
        zoom = 1;
    if (zoom > 8)
        zoom = 8;
    m_zoomLevel = zoom;
    updateZoomLabel();

    if (m_autoRefresh)
        m_autoRefresh->setChecked(state.value(QStringLiteral("autoRefresh")).toBool(m_autoRefresh->isChecked()));

    refresh();
}

void MemoryVisualizerWidget::updateZoomLabel()
{
    m_zoomLabel->setText(QStringLiteral("%1x").arg(m_zoomLevel));
}

bool MemoryVisualizerWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_imageLabel && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        handleImageClick(me->pos(), me->button() == Qt::RightButton);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void MemoryVisualizerWidget::handleImageClick(const QPoint &pos, bool rightButton)
{
    if (rightButton) {
        m_hasHighlight = false;
        m_valueLabel->setVisible(false);
        refresh();
        return;
    }

    int px = pos.x() / m_zoomLevel;
    int py = pos.y() / m_zoomLevel;

    if (px < 0 || px >= m_imgWidth || py < 0 || py >= m_imgHeight)
        return;

    int pixelIdx = py * m_imgWidth + px;
    int byteOffset;
    int bytesPerPixel;

    switch (m_bpp) {
    case BPP_1:         byteOffset = pixelIdx / 8; bytesPerPixel = 1; break;
    case BPP_4:         byteOffset = pixelIdx / 2; bytesPerPixel = 1; break;
    case BPP_8:         byteOffset = pixelIdx;     bytesPerPixel = 1; break;
    case BPP_16_RGB565: byteOffset = pixelIdx * 2; bytesPerPixel = 2; break;
    default:            byteOffset = pixelIdx * 2; bytesPerPixel = 2; break;
    }

    m_highlightAddr = m_baseAddr + byteOffset;
    m_highlightPixel = QPoint(px, py);
    m_hasHighlight = true;

    uint32_t val = 0;
    debug_read_memory(m_highlightAddr, &val, bytesPerPixel);

    QString valStr;
    if (m_bpp == BPP_16_RGB565 && bytesPerPixel == 2) {
        uint16_t rgb565 = val & 0xFFFF;
        uint8_t r = ((rgb565 >> 11) & 0x1F);
        uint8_t g = ((rgb565 >> 5) & 0x3F);
        uint8_t b = (rgb565 & 0x1F);
        valStr = QStringLiteral("Addr: %1 = %2  (R:%3 G:%4 B:%5)")
            .arg(m_highlightAddr, 8, 16, QLatin1Char('0'))
            .arg(rgb565, 4, 16, QLatin1Char('0'))
            .arg(r, 2, 16, QLatin1Char('0'))
            .arg(g, 2, 16, QLatin1Char('0'))
            .arg(b, 2, 16, QLatin1Char('0'));
    } else {
        valStr = QStringLiteral("Addr: %1 = %2")
            .arg(m_highlightAddr, 8, 16, QLatin1Char('0'))
            .arg(val, bytesPerPixel * 2, 16, QLatin1Char('0'));
    }
    m_valueLabel->setText(valStr);
    m_valueLabel->setVisible(true);

    refresh();
}

void MemoryVisualizerWidget::refresh()
{
    if (!isVisible())
        return;

    bool ok = false;
    m_baseAddr = m_addrEdit->text().toUInt(&ok, 16);
    if (!ok)
        return;
    renderImage();
}

void MemoryVisualizerWidget::renderImage()
{
    int w = m_imgWidth;
    int h = m_imgHeight;

    /* Calculate bytes needed */
    int bytesPerPixel;
    switch (m_bpp) {
    case BPP_1:         bytesPerPixel = 0; break; /* handled specially */
    case BPP_4:         bytesPerPixel = 0; break;
    case BPP_8:         bytesPerPixel = 1; break;
    case BPP_16_RGB565: bytesPerPixel = 2; break;
    default:            bytesPerPixel = 2; break;
    }

    int totalBytes;
    if (m_bpp == BPP_1)
        totalBytes = (w * h + 7) / 8;
    else if (m_bpp == BPP_4)
        totalBytes = (w * h + 1) / 2;
    else
        totalBytes = w * h * bytesPerPixel;

    if (totalBytes > 1024 * 1024)
        totalBytes = 1024 * 1024; /* safety limit */

    QByteArray buf(totalBytes, 0);
    debug_read_memory(m_baseAddr, buf.data(), totalBytes);

    m_image = QImage(w, h, QImage::Format_RGB32);
    m_image.fill(Qt::black);

    const uint8_t *data = reinterpret_cast<const uint8_t *>(buf.constData());

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int pixelIdx = y * w + x;
            uint32_t color = 0;

            switch (m_bpp) {
            case BPP_1: {
                int byteIdx = pixelIdx / 8;
                int bitIdx = 7 - (pixelIdx % 8);
                if (byteIdx < totalBytes) {
                    uint8_t val = (data[byteIdx] >> bitIdx) & 1;
                    color = val ? 0xFFFFFF : 0x000000;
                }
                break;
            }
            case BPP_4: {
                int byteIdx = pixelIdx / 2;
                if (byteIdx < totalBytes) {
                    uint8_t val = (pixelIdx & 1) ? (data[byteIdx] & 0x0F)
                                                  : (data[byteIdx] >> 4);
                    uint8_t gray = val * 17;
                    color = (gray << 16) | (gray << 8) | gray;
                }
                break;
            }
            case BPP_8: {
                if (pixelIdx < totalBytes) {
                    uint8_t val = data[pixelIdx];
                    color = (val << 16) | (val << 8) | val;
                }
                break;
            }
            case BPP_16_RGB565: {
                int byteIdx = pixelIdx * 2;
                if (byteIdx + 1 < totalBytes) {
                    uint16_t rgb565 = data[byteIdx] | (data[byteIdx + 1] << 8);
                    uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
                    uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
                    uint8_t b = (rgb565 & 0x1F) << 3;
                    color = (r << 16) | (g << 8) | b;
                }
                break;
            }
            }

            m_image.setPixel(x, y, 0xFF000000 | color);
        }
    }

    /* Scale up using zoom level */
    QImage scaled = m_image.scaled(w * m_zoomLevel, h * m_zoomLevel,
                                    Qt::KeepAspectRatio, Qt::FastTransformation);

    /* Draw highlight rectangle on the scaled image */
    if (m_hasHighlight) {
        QPainter hp(&scaled);
        const WidgetTheme &t = currentWidgetTheme();
        QPen pen(t.accent, 2);
        hp.setPen(pen);
        int rx = m_highlightPixel.x() * m_zoomLevel;
        int ry = m_highlightPixel.y() * m_zoomLevel;
        hp.drawRect(rx, ry, m_zoomLevel, m_zoomLevel);
    }

    m_imageLabel->setPixmap(QPixmap::fromImage(scaled));
    m_imageLabel->adjustSize();
}
