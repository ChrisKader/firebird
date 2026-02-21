#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/breakpoints/breakpointwidget.h"

DebugDockRegistration makeBreakpointDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Breakpoints;
    registration.objectName = QStringLiteral("dockBreakpoints");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Breakpoints");
    registration.icon = DebugDockIcon::Bookmark;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new BreakpointWidget(parent);
    };
    return registration;
}
