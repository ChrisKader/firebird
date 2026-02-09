#ifndef ACTIVITYBAR_H
#define ACTIVITYBAR_H

#include <QWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFont>
#include <QMap>
#include <QPaintEvent>

class ActivityBarButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ActivityBarButton(ushort codepoint, const QFont &iconFont,
                               QWidget *parent = nullptr);

    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setBadgeCount(int count);
    int badgeCount() const { return m_badge; }
    void setAccentColor(const QColor &c) { m_accentColor = c; update(); }
    void setActiveTextColor(const QColor &c) { m_activeTextColor = c; update(); }
    void setInactiveTextColor(const QColor &c) { m_inactiveTextColor = c; update(); }
    void setBadgeBgColor(const QColor &c) { m_badgeBg = c; update(); }
    void setBadgeFgColor(const QColor &c) { m_badgeFg = c; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ushort m_codepoint;
    QFont m_iconFont;
    bool m_active = false;
    int m_badge = 0;
    QColor m_accentColor{QStringLiteral("#007acc")};
    QColor m_activeTextColor{QStringLiteral("#ffffff")};
    QColor m_inactiveTextColor{QStringLiteral("#858585")};
    QColor m_badgeBg{QStringLiteral("#007acc")};
    QColor m_badgeFg{QStringLiteral("#ffffff")};
};

class ActivityBar : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityBar(const QFont &iconFont, QWidget *parent = nullptr);

    void addEntry(const QString &id, ushort iconCodepoint, const QString &tooltip);
    void setActive(const QString &id);
    QString activeId() const { return m_activeId; }
    void setBadge(const QString &id, int count);
    int badge(const QString &id) const;
    void clearBadge(const QString &id);
    void updateTheme();

signals:
    void entryClicked(const QString &id);

private:
    QVBoxLayout *m_topLayout;
    QVBoxLayout *m_bottomLayout;
    QFont m_iconFont;
    struct Entry {
        ActivityBarButton *btn;
        QString id;
    };
    QList<Entry> m_entries;
    QString m_activeId;
};

#endif // ACTIVITYBAR_H
