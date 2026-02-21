#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/hexview/hexviewwidget.h"

DebugDockRegistration makeHexViewDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Memory;
    registration.objectName = QStringLiteral("dockMemory");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Memory");
    registration.icon = DebugDockIcon::Memory;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.coreDock = true;
    registration.visibleByDefault = true;
    registration.resetGroup = DebugDockGroup::BottomMemory;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new HexViewWidget(parent);
    };
    return registration;
}
