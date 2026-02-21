#include "dockwidget.h"
#include "ui/theme/widgettheme.h"

#include <QHBoxLayout>

#ifndef FIREBIRD_USE_KDDOCKWIDGETS

DockWidget::DockWidget(const QString &title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)
{
    /* Build thin title bar: [icon] title */
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 1, 4, 1);
    layout->setSpacing(4);

    m_thinIcon = new QLabel(bar);
    m_thinIcon->setFixedSize(12, 12);
    m_thinIcon->setVisible(false);
    layout->addWidget(m_thinIcon);

    m_thinTitleBar = new QLabel(title, bar);
    QFont f = m_thinTitleBar->font();
    f.setPointSize(8);
    f.setBold(true);
    m_thinTitleBar->setFont(f);
    layout->addWidget(m_thinTitleBar, 1);

    m_thinBarWidget = bar;
    applyThinBarStyle();

    connect(this, &QDockWidget::topLevelChanged, this, &DockWidget::refreshTitlebar);
    connect(this, &QDockWidget::windowTitleChanged, this, [this](const QString &text) {
        m_thinTitleBar->setText(text);
    });
}

void DockWidget::setWidget(QWidget *widget)
{
    // Qt docs: "Custom size hints, minimum and maximum sizes and size policies
    // should be implemented in the child widget."  Enforce a minimum on the
    // content widget so that Qt's dock layout never collapses a tab group to
    // zero when the user resizes an adjacent dock.
    if (widget && widget->minimumWidth() < 60)
        widget->setMinimumWidth(60);
    if (widget && widget->minimumHeight() < 40)
        widget->setMinimumHeight(40);
    QDockWidget::setWidget(widget);
}

void DockWidget::hideTitlebar(bool b)
{
    hide_titlebar_if_possible = b;
    refreshTitlebar();
}

void DockWidget::refreshTitlebar()
{
    /* Update icon from toggle action if available */
    QAction *act = toggleViewAction();
    if (act && !act->icon().isNull()) {
        m_thinIcon->setPixmap(act->icon().pixmap(12, 12));
        m_thinIcon->setVisible(true);
    }

    if (isFloating() || !hide_titlebar_if_possible) {
        /* Default Qt title bar -- native window chrome when floating. */
        setTitleBarWidget(nullptr);
    } else {
        /* Thin label bar shows the dock name when docked. */
        setTitleBarWidget(m_thinBarWidget);
    }
}

void DockWidget::applyThinBarStyle()
{
    const WidgetTheme &t = currentWidgetTheme();
    if (m_thinBarWidget) {
        m_thinBarWidget->setStyleSheet(
            QStringLiteral("background: %1; border-bottom: 1px solid %2;")
                .arg(t.dockTitle.name(), t.border.name()));
    }
    if (m_thinTitleBar) {
        m_thinTitleBar->setStyleSheet(
            QStringLiteral("color: %1; background: transparent; border: none;")
                .arg(t.textMuted.name()));
    }
    if (m_thinIcon) {
        m_thinIcon->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    }
}

#endif
