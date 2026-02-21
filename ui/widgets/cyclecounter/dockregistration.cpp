#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/cyclecounter/cyclecounterwidget.h"

DebugDockRegistration makeCycleCounterDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::CycleCounter;
    registration.objectName = QStringLiteral("dockCycleCounter");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Cycle Counter");
    registration.icon = DebugDockIcon::CycleCounter;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new CycleCounterWidget(parent);
    };
    return registration;
}
