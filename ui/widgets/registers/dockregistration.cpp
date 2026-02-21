#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/registers/registerwidget.h"

DebugDockRegistration makeRegisterDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Registers;
    registration.objectName = QStringLiteral("dockRegisters");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Registers");
    registration.icon = DebugDockIcon::List;
    registration.defaultArea = Qt::RightDockWidgetArea;
    registration.coreDock = true;
    registration.visibleByDefault = true;
    registration.resetGroup = DebugDockGroup::Right;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new RegisterWidget(parent);
    };
    return registration;
}
