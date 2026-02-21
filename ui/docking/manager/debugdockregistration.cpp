#include "ui/docking/manager/debugdockregistration.h"

QList<DebugDockRegistration> buildDebugDockRegistrations()
{
    return {
        makeDisassemblyDockRegistration(),
        makeRegisterDockRegistration(),
        makeStackDockRegistration(),
        makeHexViewDockRegistration(),
        makeBreakpointDockRegistration(),
        makeWatchpointDockRegistration(),
        makePortMonitorDockRegistration(),
        makeKeyHistoryDockRegistration(),
        makeConsoleDockRegistration(),
        makeMemoryVisualizerDockRegistration(),
        makeCycleCounterDockRegistration(),
        makeTimerMonitorDockRegistration(),
        makeLCDStateDockRegistration(),
        makeMMUViewerDockRegistration(),
    };
}
