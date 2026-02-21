#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/disassembly/disassemblywidget.h"

DebugDockRegistration makeDisassemblyDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::Disasm;
    registration.objectName = QStringLiteral("dockDisasm");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Disassembly");
    registration.icon = DebugDockIcon::Code;
    registration.defaultArea = Qt::RightDockWidgetArea;
    registration.coreDock = true;
    registration.visibleByDefault = true;
    registration.resetGroup = DebugDockGroup::Right;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new DisassemblyWidget(parent);
    };
    registration.initializeWidget = [](QWidget *widget, const QFont &iconFont) {
        if (auto *disassembly = qobject_cast<DisassemblyWidget *>(widget))
            disassembly->setIconFont(iconFont);
    };
    return registration;
}
