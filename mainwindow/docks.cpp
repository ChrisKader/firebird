#include "mainwindow.h"

#include <QMainWindow>
#include <QMenu>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/DockWidget.h>
    #include <kddockwidgets/KDDockWidgets.h>
#endif

#include "ui/dockwidget.h"
#include "ui/kdockwidget.h"

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
static KDDockWidgets::QtWidgets::MainWindow *asKDDMainWindow(QMainWindow *window)
{
    return dynamic_cast<KDDockWidgets::QtWidgets::MainWindow *>(window);
}

static KDDockWidgets::Location toKDDLocation(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return KDDockWidgets::Location_OnLeft;
    case Qt::TopDockWidgetArea: return KDDockWidgets::Location_OnTop;
    case Qt::RightDockWidgetArea: return KDDockWidgets::Location_OnRight;
    case Qt::BottomDockWidgetArea: return KDDockWidgets::Location_OnBottom;
    default: return KDDockWidgets::Location_OnRight;
    }
}
#endif

static void addDockWidgetCompat(QMainWindow *window,
                                DockWidget *dock,
                                Qt::DockWidgetArea area,
                                DockWidget *relativeTo = nullptr,
                                bool startHidden = false,
                                bool preserveCurrentSize = false,
                                const QSize &preferredSize = QSize())
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (auto *kdd = asKDDMainWindow(window)) {
        KDDockWidgets::InitialOption initial;
        if (preferredSize.isValid() && preferredSize.width() > 0 && preferredSize.height() > 0) {
            initial.preferredSize = preferredSize;
        }
        if (preserveCurrentSize) {
            const QSize current = dock->size();
            if (current.isValid() && current.width() > 0 && current.height() > 0)
                initial.preferredSize = current;
        }
        if (!initial.preferredSize.isValid() && dock->widget()) {
            const QSize hinted = dock->widget()->sizeHint();
            if (hinted.isValid() && hinted.width() > 0 && hinted.height() > 0)
                initial.preferredSize = hinted;
        }
        if (startHidden)
            initial.visibility = KDDockWidgets::InitialVisibilityOption::StartHidden;
        kdd->addDockWidget(dock, toKDDLocation(area), relativeTo, initial);
        return;
    }
#else
    Q_UNUSED(relativeTo);
    Q_UNUSED(startHidden);
    Q_UNUSED(preferredSize);
    window->addDockWidget(area, dock);
#endif
}

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
    addDockWidgetCompat(content_window, dw, area, nullptr, !startVisible);
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
