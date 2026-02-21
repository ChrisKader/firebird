#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/keyhistory/keyhistorywidget.h"

DebugDockRegistration makeKeyHistoryDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::KeyHistory;
    registration.objectName = QStringLiteral("dockKeyHistory");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Key History");
    registration.icon = DebugDockIcon::History;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomTools;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new KeyHistoryWidget(parent);
    };
    return registration;
}
