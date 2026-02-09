#include "activitybar.h"
#include "widgettheme.h"

#include <QPainter>
#include <QVBoxLayout>

/* -- ActivityBarButton ---------------------------------------- */

ActivityBarButton::ActivityBarButton(ushort codepoint, const QFont &iconFont,
                                     QWidget *parent)
    : QToolButton(parent), m_codepoint(codepoint), m_iconFont(iconFont)
{
    setFixedSize(48, 48);
    setAutoRaise(true);
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void ActivityBarButton::setActive(bool active)
{
    m_active = active;
    setChecked(active);
    update();
}

void ActivityBarButton::setBadgeCount(int count)
{
    m_badge = count;
    update();
}

void ActivityBarButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    /* Background on hover */
    if (underMouse() && !m_active) {
        p.fillRect(rect(), QColor(255, 255, 255, 15));
    }

    /* Active indicator: 2px accent bar on left edge */
    if (m_active) {
        p.fillRect(0, 0, 2, height(), m_accentColor);
    }

    /* Icon glyph */
    QFont f(m_iconFont);
    f.setPixelSize(24);
    p.setFont(f);
    p.setPen(m_active ? m_activeTextColor : m_inactiveTextColor);

    QRect iconRect = rect();
    p.drawText(iconRect, Qt::AlignCenter, QString(QChar(m_codepoint)));

    /* Badge circle */
    if (m_badge > 0) {
        int badgeSize = 16;
        int bx = width() - badgeSize - 4;
        int by = 4;
        QRect badgeRect(bx, by, badgeSize, badgeSize);

        p.setPen(Qt::NoPen);
        p.setBrush(m_badgeBg);
        p.drawEllipse(badgeRect);

        QFont badgeFont;
        badgeFont.setPixelSize(9);
        badgeFont.setBold(true);
        p.setFont(badgeFont);
        p.setPen(m_badgeFg);
        QString text = m_badge > 99 ? QStringLiteral("99+")
                                     : QString::number(m_badge);
        p.drawText(badgeRect, Qt::AlignCenter, text);
    }
}

/* -- ActivityBar ---------------------------------------------- */

ActivityBar::ActivityBar(const QFont &iconFont, QWidget *parent)
    : QWidget(parent), m_iconFont(iconFont)
{
    setFixedWidth(48);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_topLayout = new QVBoxLayout;
    m_topLayout->setContentsMargins(0, 0, 0, 0);
    m_topLayout->setSpacing(0);
    outerLayout->addLayout(m_topLayout);

    outerLayout->addStretch(1);

    m_bottomLayout = new QVBoxLayout;
    m_bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_bottomLayout->setSpacing(0);
    outerLayout->addLayout(m_bottomLayout);
}

void ActivityBar::addEntry(const QString &id, ushort iconCodepoint,
                           const QString &tooltip)
{
    auto *btn = new ActivityBarButton(iconCodepoint, m_iconFont, this);
    btn->setToolTip(tooltip);

    connect(btn, &QToolButton::clicked, this, [this, id]() {
        emit entryClicked(id);
    });

    m_topLayout->addWidget(btn);
    m_entries.append({btn, id});
}

void ActivityBar::setActive(const QString &id)
{
    m_activeId = id;
    for (auto &entry : m_entries) {
        entry.btn->setActive(entry.id == id);
    }
}

void ActivityBar::setBadge(const QString &id, int count)
{
    for (auto &entry : m_entries) {
        if (entry.id == id) {
            entry.btn->setBadgeCount(count);
            break;
        }
    }
}

int ActivityBar::badge(const QString &id) const
{
    for (const auto &entry : m_entries) {
        if (entry.id == id)
            return entry.btn->badgeCount();
    }
    return 0;
}

void ActivityBar::clearBadge(const QString &id)
{
    setBadge(id, 0);
}

void ActivityBar::updateTheme()
{
    const WidgetTheme &t = currentWidgetTheme();

    setStyleSheet(QStringLiteral("ActivityBar { background: %1; }")
                      .arg(t.activityBarBg.name()));

    for (auto &entry : m_entries) {
        entry.btn->setAccentColor(t.activityBarActiveBorder);
        entry.btn->setActiveTextColor(t.activityBarActiveFg);
        entry.btn->setInactiveTextColor(t.activityBarFg);
        entry.btn->setBadgeBgColor(t.activityBarBadgeBg);
        entry.btn->setBadgeFgColor(t.activityBarBadgeFg);
    }
}
