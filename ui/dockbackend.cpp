#include "ui/dockbackend.h"

#include "ui/dockwidget.h"

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/KDDockWidgets.h>
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
        KDDockWidgets::InitialOption initial;
        if (preferredSize.isValid() && preferredSize.width() > 0 && preferredSize.height() > 0)
            initial.preferredSize = preferredSize;
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
    Q_UNUSED(preserveCurrentSize);
    window->addDockWidget(area, dock);
#endif
}

} // namespace DockBackend
