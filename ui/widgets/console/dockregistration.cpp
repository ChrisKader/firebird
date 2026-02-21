#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/console/consolewidget.h"

DebugDockRegistration makeConsoleDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Console;
    registration.objectName = QStringLiteral("dockConsole");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Console");
    registration.icon = DebugDockIcon::Terminal;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.coreDock = true;
    registration.visibleByDefault = true;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new ConsoleWidget(parent);
    };
    return registration;
}
