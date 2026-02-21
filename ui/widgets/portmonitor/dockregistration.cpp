#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/portmonitor/portmonitorwidget.h"

DebugDockRegistration makePortMonitorDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::PortMonitor;
    registration.objectName = QStringLiteral("dockPortMonitor");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Port Monitor");
    registration.icon = DebugDockIcon::Monitor;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new PortMonitorWidget(parent);
    };
    return registration;
}
