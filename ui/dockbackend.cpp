#include "ui/dockbackend.h"

#include "ui/dockwidget.h"

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/KDDockWidgets.h>
    #include <kddockwidgets/qtcommon/View.h>
    #include <kddockwidgets/core/TitleBar.h>
    #include <kddockwidgets/core/View.h>
#else
    #include <QDockWidget>
#endif

namespace DockBackend {

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

static KDDockWidgets::InitialOption buildInitialOption(DockWidget *dock,
                                                       bool startHidden,
                                                       bool preserveCurrentSize,
                                                       const QSize &preferredSize)
{
    KDDockWidgets::InitialOption initial;
    if (preferredSize.isValid() && preferredSize.width() > 0 && preferredSize.height() > 0)
        initial.preferredSize = preferredSize;
    if (preserveCurrentSize) {
        const QSize current = dock ? dock->size() : QSize();
        if (current.isValid() && current.width() > 0 && current.height() > 0)
            initial.preferredSize = current;
    }
    if ((!initial.preferredSize.isValid()) && dock && dock->widget()) {
        const QSize hinted = dock->widget()->sizeHint();
        if (hinted.isValid() && hinted.width() > 0 && hinted.height() > 0)
            initial.preferredSize = hinted;
    }
    if (startHidden)
        initial.visibility = KDDockWidgets::InitialVisibilityOption::StartHidden;
    return initial;
}
#endif

void addDockWidgetCompat(QMainWindow *window,
                         DockWidget *dock,
                         Qt::DockWidgetArea area,
                         DockWidget *relativeTo,
                         bool startHidden,
                         bool preserveCurrentSize,
                         const QSize &preferredSize)
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (auto *kdd = asKDDMainWindow(window)) {
        const KDDockWidgets::InitialOption initial =
            buildInitialOption(dock, startHidden, preserveCurrentSize, preferredSize);
        kdd->addDockWidget(dock, toKDDLocation(area), relativeTo, initial);
        return;
    }
#else
    Q_UNUSED(relativeTo);
    Q_UNUSED(startHidden);
    Q_UNUSED(preferredSize);
    Q_UNUSED(preserveCurrentSize);
    window->addDockWidget(area, dock);
#endif
}

void addDockWidgetCompatAnyRelative(QMainWindow *window,
                                    DockWidget *dock,
                                    Qt::DockWidgetArea area,
                                    QObject *relativeToAny,
                                    bool startHidden,
                                    bool preserveCurrentSize,
                                    const QSize &preferredSize)
{
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (!window || !dock)
        return;
    auto *relativeTo = dynamic_cast<KDDockWidgets::QtWidgets::DockWidget *>(relativeToAny);
    if (auto *kdd = asKDDMainWindow(window)) {
        const KDDockWidgets::InitialOption initial =
            buildInitialOption(dock, startHidden, preserveCurrentSize, preferredSize);
        kdd->addDockWidget(dock, toKDDLocation(area), relativeTo, initial);
        return;
    }
    addDockWidgetCompat(window, dock, area, nullptr, startHidden, preserveCurrentSize, preferredSize);
#else
    auto *relativeTo = qobject_cast<DockWidget *>(relativeToAny);
    addDockWidgetCompat(window, dock, area, relativeTo, startHidden, preserveCurrentSize, preferredSize);
#endif
}

void tabifyDockWidgetCompat(QMainWindow *window,
                            DockWidget *first,
                            DockWidget *second)
{
    if (!window || !first || !second || first == second)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (asKDDMainWindow(window)) {
        first->addDockWidgetAsTab(second);
        return;
    }
#else
    window->tabifyDockWidget(first, second);
#endif
}

void removeDockWidgetCompat(QMainWindow *window,
                            DockWidget *dock)
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    Q_UNUSED(window);
    dock->close();
#else
    window->removeDockWidget(dock);
#endif
}

void splitDockWidgetCompat(QMainWindow *window,
                           DockWidget *first,
                           DockWidget *second,
                           Qt::Orientation orientation)
{
    if (!window || !first || !second)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    Q_UNUSED(window);
    const KDDockWidgets::InitialOption initial = buildInitialOption(second, false, false, QSize());
    first->addDockWidgetToContainingWindow(
        second,
        orientation == Qt::Horizontal ? KDDockWidgets::Location_OnRight
                                      : KDDockWidgets::Location_OnBottom,
        first,
        initial);
#else
    window->splitDockWidget(first, second, orientation);
#endif
}

void resizeDocksCompat(QMainWindow *window,
                       const QList<DockWidget *> &docks,
                       const QList<int> &sizes,
                       Qt::Orientation orientation)
{
    Q_UNUSED(window);
    Q_UNUSED(docks);
    Q_UNUSED(sizes);
    Q_UNUSED(orientation);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
    if (!window)
        return;
    QList<QDockWidget *> qDocks;
    qDocks.reserve(docks.size());
    for (DockWidget *dock : docks)
        qDocks.append(dock);
    window->resizeDocks(qDocks, sizes, orientation);
#endif
}

QWidget *dockTitleBarHostWidget(DockWidget *dock)
{
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (!dock)
        return nullptr;
    if (auto *titleBar = dock->actualTitleBar()) {
        if (auto *view = titleBar->view())
            return KDDockWidgets::QtCommon::View_qt::asQWidget(view);
    }
#else
    Q_UNUSED(dock);
#endif
    return nullptr;
}

} // namespace DockBackend
