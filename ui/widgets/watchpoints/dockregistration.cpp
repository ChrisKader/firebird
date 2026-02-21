#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/watchpoints/watchpointwidget.h"

DebugDockRegistration makeWatchpointDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Watchpoints;
    registration.objectName = QStringLiteral("dockWatchpoints");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Watchpoints");
    registration.icon = DebugDockIcon::Visibility;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new WatchpointWidget(parent);
    };
    return registration;
}
