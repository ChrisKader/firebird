#include "ui/docking/manager/debugdockregistration.h"

#include "ui/widgets/memvisualizer/memoryvisualizerwidget.h"

DebugDockRegistration makeMemoryVisualizerDockRegistration()
{
    DebugDockRegistration registration;
    registration.kind = DebugDockKind::MemVis;
    registration.objectName = QStringLiteral("dockMemVis");
    registration.titleKey = QT_TRANSLATE_NOOP("DockManager", "Memory Visualizer");
    registration.icon = DebugDockIcon::GridOn;
    registration.defaultArea = Qt::BottomDockWidgetArea;
    registration.resetGroup = DebugDockGroup::BottomMemory;
    registration.createWidget = [](QWidget *parent) -> QWidget * {
        return new MemoryVisualizerWidget(parent);
    };
    return registration;
}
