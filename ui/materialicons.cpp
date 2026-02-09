#include "materialicons.h"

#include <QPainter>
#include <QPixmap>

namespace MaterialIcons {

QIcon fromCodepoint(const QFont &font, ushort codepoint, int size, const QColor &color)
{
    if (font.family().isEmpty())
        return {};

    const int px = size;
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);

    QFont f(font);
    f.setPixelSize(px);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, QString(QChar(codepoint)));
    p.end();

    return QIcon(pm);
}

} // namespace MaterialIcons
