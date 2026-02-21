#ifndef DEBUGDOCKREGISTRATION_H
#define DEBUGDOCKREGISTRATION_H

#include <QDockWidget>
#include <QFont>
#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

enum class DebugDockKind {
    Disasm,
    Registers,
    Stack,
    Memory,
    Breakpoints,
    Watchpoints,
    PortMonitor,
    KeyHistory,
    Console,
    MemVis,
    CycleCounter,
    TimerMonitor,
    LCDState,
    MMUViewer,
};

enum class DebugDockGroup {
    Right,
    BottomMemory,
    BottomTools,
};

enum class DebugDockIcon {
    None,
    Code,
    List,
    ViewColumn,
    Memory,
    Bookmark,
    Visibility,
    Monitor,
    History,
    Terminal,
    GridOn,
    CycleCounter,
    Timer,
    Display,
    Layers,
};

struct DebugDockRegistration {
    DebugDockKind kind = DebugDockKind::Disasm;
    QString objectName;
    const char *titleKey = "";
    DebugDockIcon icon = DebugDockIcon::None;
    Qt::DockWidgetArea defaultArea = Qt::BottomDockWidgetArea;
    bool coreDock = false;
    bool visibleByDefault = false;
    DebugDockGroup resetGroup = DebugDockGroup::BottomTools;
    bool tabWithPreviousInReset = false;
    std::function<QWidget *(QWidget *parent)> createWidget;
    std::function<void(QWidget *widget, const QFont &iconFont)> initializeWidget;
};

QList<DebugDockRegistration> buildDebugDockRegistrations();

DebugDockRegistration makeDisassemblyDockRegistration();
DebugDockRegistration makeRegisterDockRegistration();
DebugDockRegistration makeStackDockRegistration();
DebugDockRegistration makeHexViewDockRegistration();
DebugDockRegistration makeBreakpointDockRegistration();
DebugDockRegistration makeWatchpointDockRegistration();
DebugDockRegistration makePortMonitorDockRegistration();
DebugDockRegistration makeKeyHistoryDockRegistration();
DebugDockRegistration makeConsoleDockRegistration();
DebugDockRegistration makeMemoryVisualizerDockRegistration();
DebugDockRegistration makeCycleCounterDockRegistration();
DebugDockRegistration makeTimerMonitorDockRegistration();
DebugDockRegistration makeLCDStateDockRegistration();
DebugDockRegistration makeMMUViewerDockRegistration();

#endif // DEBUGDOCKREGISTRATION_H
