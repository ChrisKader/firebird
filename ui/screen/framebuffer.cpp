#include "framebuffer.h"

#include <array>
#include <cassert>

#include <QImage>
#include <QPainter>
#include <QGuiApplication>
#include <QPalette>
#include <QScreen>

#include "core/debug/debug.h"
#include "core/emu.h"
#include "core/peripherals/lcd.h"
#include "core/peripherals/misc.h"

#include "ui/input/keypadbridge.h"
#include "ui/theme/widgettheme.h"

LCDScaleMode lcd_scale_mode = LCDScaleMode::Bilinear;

static int render_contrast_level()
{
    int contrast = hdq1w.lcd_contrast;
    if (!emulate_cx2)
        return contrast;

    /* CX II user-visible backlight steps map to a narrower contrast window
     * (roughly 0xF3..0x6C PWM -> ~7..85 contrast). Normalize that window to
     * the full renderer range so "brightest" in-OS looks bright on screen. */
    constexpr int kCx2ContrastMin = 7;
    constexpr int kCx2ContrastMax = 85;
    if (contrast <= 0)
        return contrast;
    if (contrast <= kCx2ContrastMin)
        return 1;
    if (contrast >= kCx2ContrastMax)
        return LCD_CONTRAST_MAX;
    return ((contrast - kCx2ContrastMin) * LCD_CONTRAST_MAX
        + (kCx2ContrastMax - kCx2ContrastMin) / 2)
        / (kCx2ContrastMax - kCx2ContrastMin);
}

QImage renderFramebuffer()
{
    static std::array<uint16_t, 320 * 240> framebuffer;

    lcd_cx_draw_frame(framebuffer.data());
    QImage::Format format = QImage::Format_RGB16;

    if(!emulate_cx)
    {
        format = QImage::Format_RGB444;
        uint16_t *px = framebuffer.data();
        for(unsigned int i = 0; i < 320*240; ++i)
        {
            uint8_t pix = *px & 0xF;
            uint16_t n = (pix << 8) | (pix << 4) | pix;
            *px = ~n & 0xFFF;
            ++px;
        }
    }

    QImage image(reinterpret_cast<const uchar*>(framebuffer.data()), 320, 240, 320 * 2, format);

    return image;
}

void paintFramebuffer(QPainter *p)
{
#ifdef IS_IOS_BUILD
    // iOS retina screens need the real device pixel ratio (2 on retina)
    static const double devicePixelRatio = ((QGuiApplication*)QCoreApplication::instance())->primaryScreen()->devicePixelRatio();
#else
    // Desktop always uses 1 (tested on macOS retina + non-retina, Windows)
    static const double devicePixelRatio = 1;
#endif

    QRect painterWindowScaled(p->window().topLeft(), p->window().size() / devicePixelRatio);

    if(cpu_events & EVENT_SLEEP)
    {
        p->fillRect(painterWindowScaled, Qt::black);
    }
    else if(hdq1w.lcd_contrast == 0)
    {
        p->fillRect(painterWindowScaled, Qt::transparent);
        p->setPen(QGuiApplication::palette().color(QPalette::WindowText));
        p->drawText(painterWindowScaled, Qt::AlignCenter, QObject::tr("LCD turned off"));
    }
    else
    {
        QImage raw = renderFramebuffer();
        QImage image;
        switch (lcd_scale_mode) {
        case LCDScaleMode::NearestNeighbor:
            image = raw.scaled(p->window().size(), Qt::KeepAspectRatio, Qt::FastTransformation);
            break;
        case LCDScaleMode::SharpBilinear: {
            // Integer-scale with nearest-neighbor first, then smooth to final size
            int sx = qMax(1, p->window().width() / raw.width());
            int sy = qMax(1, p->window().height() / raw.height());
            int intScale = qMin(sx, sy);
            QImage intScaled = raw.scaled(raw.width() * intScale, raw.height() * intScale,
                                          Qt::KeepAspectRatio, Qt::FastTransformation);
            image = intScaled.scaled(p->window().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            break;
        }
        case LCDScaleMode::Bilinear:
        default:
            image = raw.scaled(p->window().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            break;
        }
        image.setDevicePixelRatio(devicePixelRatio);
        const int x = (p->window().width() - image.width()) / 2;
        const int y = (p->window().height() - image.height()) / 2;
        QRect imageRect(x, y, image.width(), image.height());
        p->drawImage(imageRect.topLeft(), image);

        // Simulate backlight dimming: overlay black with opacity based on contrast.
        // contrast=LCD_CONTRAST_MAX -> fully bright (no overlay), contrast=1 -> nearly black.
        int contrast_level = render_contrast_level();
        if (contrast_level < LCD_CONTRAST_MAX) {
            int alpha = 255 - (contrast_level * 255 / LCD_CONTRAST_MAX);
            p->setCompositionMode(QPainter::CompositionMode_SourceOver);
            p->fillRect(imageRect, QColor(0, 0, 0, alpha));
            p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        // Draw a visible border around the rendered framebuffer area.
        QColor border = currentWidgetTheme().border;
        border.setAlpha(220);
        QPen pen(border);
        pen.setWidth(1);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        // Draw inside the image bounds to avoid clipping on the edges.
        p->drawRect(imageRect.adjusted(0, 0, -1, -1));
    }

    if(in_debugger)
    {
        const WidgetTheme &theme = currentWidgetTheme();
        p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor overlay = theme.window;
        overlay.setAlpha(150);
        p->fillRect(painterWindowScaled, overlay);
        p->setPen(theme.text);
        p->drawText(painterWindowScaled, Qt::AlignCenter, QObject::tr("In debugger"));
    }
}

QMLFramebuffer::QMLFramebuffer(QQuickItem *parent)
 : QQuickPaintedItem(parent)
{
    installEventFilter(&qt_keypad_bridge);
}

void QMLFramebuffer::paint(QPainter *p)
{
    paintFramebuffer(p);
}
