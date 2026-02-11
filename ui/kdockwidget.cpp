#include "ui/kdockwidget.h"

#include <QAction>

KDockWidget::KDockWidget(const QString &uniqueName,
                         const QString &title,
                         QWidget *parent)
    : DockWidget(uniqueName, parent)
{
    setObjectName(uniqueName);
    setWindowTitle(title);
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    setTitle(title);
#endif
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
