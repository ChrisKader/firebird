#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/lcdstate/lcdstatewidget.h"

DebugDockRegistration makeLCDStateDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::LCDState;
    registration.objectName = QStringLiteral("dockLCDState");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "LCD State");
    registration.icon = DebugDockIcon::Display;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new LCDStateWidget(parent);
    };
    return registration;
}
