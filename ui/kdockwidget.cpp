#include "ui/kdockwidget.h"

#include <QAction>

KDockWidget::KDockWidget(const QString &title, QWidget *parent)
    : DockWidget(title, parent)
{
}

void KDockWidget::applyThinTitlebar(bool enabled)
{
    hideTitlebar(enabled);
}

void KDockWidget::setDockIcon(const QIcon &icon)
{
    QAction *toggle = toggleViewAction();
    if (toggle)
        toggle->setIcon(icon);
}
