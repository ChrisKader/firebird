#include "mainwindow.h"

#include <QMenu>

#include "ui/docking/dockbackend.h"
#include "ui/docking/dockwidget.h"
#include "ui/docking/kdockwidget.h"

void MainWindow::applyStandardDockFeatures(DockWidget *dw, bool closable) const
{
    if (!dw)
        return;
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    QDockWidget::DockWidgetFeatures features = QDockWidget::DockWidgetMovable |
                                               QDockWidget::DockWidgetFloatable;
    if (closable)
        features |= QDockWidget::DockWidgetClosable;
    dw->setFeatures(features);
}

DockWidget *MainWindow::createMainDock(const QString &title,
                                       QWidget *widget,
                                       const QString &objectName,
                                       Qt::DockWidgetArea area,
                                       QMenu *docksMenu,
                                       const QIcon &icon,
                                       bool hideTitlebar,
                                       bool closable,
                                       bool startVisible)
{
    const QString uniqueName = objectName.isEmpty() ? title : objectName;
    auto *dw = new KDockWidget(uniqueName, title, content_window);
    if (hideTitlebar)
        dw->applyThinTitlebar(true);
    if (!icon.isNull())
        dw->setWindowIcon(icon);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw, closable);
    DockBackend::addDockWidgetCompat(content_window, dw, area, nullptr, !startVisible);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
    if (!startVisible)
        dw->hide();
#endif
    if (docksMenu) {
        QAction *action = dw->toggleViewAction();
        if (!icon.isNull())
            action->setIcon(icon);
        docksMenu->addAction(action);
    }
    return dw;
}
