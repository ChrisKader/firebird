#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/mmuviewer/mmuviewerwidget.h"

DebugDockRegistration makeMMUViewerDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::MMUViewer;
    registration.objectName = QStringLiteral("dockMMUViewer");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "MMU Viewer");
    registration.icon = DebugDockIcon::Layers;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomMemory;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new MMUViewerWidget(parent);
    };
    return registration;
}
