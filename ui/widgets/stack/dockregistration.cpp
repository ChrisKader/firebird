#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/stack/stackwidget.h"

DebugDockRegistration makeStackDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Stack;
    registration.objectName = QStringLiteral("dockStack");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Stack");
    registration.icon = DebugDockIcon::ViewColumn;
    registration.defaultArea = Qt::RightDockWidgetArea;
    registration.resetGroup = DebugDockGroup::Right;
    registration.tabWithPreviousInReset = true;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new StackWidget(parent);
    };
    return registration;
}
