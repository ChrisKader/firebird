#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/timermonitor/timermonitorwidget.h"

DebugDockRegistration makeTimerMonitorDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::TimerMonitor;
    registration.objectName = QStringLiteral("dockTimerMonitor");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Timer Monitor");
    registration.icon = DebugDockIcon::Timer;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new TimerMonitorWidget(parent);
    };
    return registration;
}
