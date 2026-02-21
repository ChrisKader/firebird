#include "ui/docking/manager/dockmanager.h"

#include <QMainWindow>
#include <QMenu>
#include <QShortcut>

#include "ui/docking/backend/dockbackend.h"
#include "ui/docking/widgets/dockwidget.h"
#include "ui/docking/widgets/kdockwidget.h"
#include "ui/input/keypadbridge.h"
#include "ui/widgets/disassembly/disassemblywidget.h"
#include "ui/widgets/registers/registerwidget.h"
#include "ui/widgets/hexview/hexviewwidget.h"
#include "ui/widgets/breakpoints/breakpointwidget.h"
#include "ui/widgets/watchpoints/watchpointwidget.h"
#include "ui/widgets/portmonitor/portmonitorwidget.h"
#include "ui/widgets/stack/stackwidget.h"
#include "ui/widgets/keyhistory/keyhistorywidget.h"
#include "ui/widgets/console/consolewidget.h"
#include "ui/widgets/memvisualizer/memoryvisualizerwidget.h"
#include "ui/widgets/cyclecounter/cyclecounterwidget.h"
#include "ui/widgets/timermonitor/timermonitorwidget.h"
#include "ui/widgets/lcdstate/lcdstatewidget.h"
#include "ui/widgets/mmuviewer/mmuviewerwidget.h"
#include "ui/widgets/gotodialog.h"
#include <QFileDialog>
#include <QMessageBox>

namespace {

enum class DebugDockId {
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

const char *dockObjectName(DebugDockId id)
{
    switch (id) {
    case DebugDockId::Disasm:       return "dockDisasm";
    case DebugDockId::Registers:    return "dockRegisters";
    case DebugDockId::Stack:        return "dockStack";
    case DebugDockId::Memory:       return "dockMemory";
    case DebugDockId::Breakpoints:  return "dockBreakpoints";
    case DebugDockId::Watchpoints:  return "dockWatchpoints";
    case DebugDockId::PortMonitor:  return "dockPortMonitor";
    case DebugDockId::KeyHistory:   return "dockKeyHistory";
    case DebugDockId::Console:      return "dockConsole";
    case DebugDockId::MemVis:       return "dockMemVis";
    case DebugDockId::CycleCounter: return "dockCycleCounter";
    case DebugDockId::TimerMonitor: return "dockTimerMonitor";
    case DebugDockId::LCDState:     return "dockLCDState";
    case DebugDockId::MMUViewer:    return "dockMMUViewer";
    }
    return "dockUnknown";
}

void applyStandardDockFeatures(DockWidget *dw, bool closable = true)
{
    if (!dw)
        return;
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    QDockWidget::DockWidgetFeatures features = QDockWidget::DockWidgetMovable |
                                               QDockWidget::DockWidgetFloatable;
    if (closable)
        features |= QDockWidget::DockWidgetClosable;
    dw->setFeatures(features);
}

} // namespace

DockManager::DockManager(QMainWindow *host, const QFont &iconFont,
                         QObject *parent)
    : QObject(parent), m_host(host), m_iconFont(iconFont)
{
}

DisassemblyWidget *DockManager::disassembly() const
{
    return m_disasmWidget.data();
}

HexViewWidget *DockManager::hexView() const
{
    return m_hexWidget.data();
}

ConsoleWidget *DockManager::console() const
{
    return m_consoleWidget.data();
}

DockWidget *DockManager::consoleDock() const
{
    return m_consoleDock.data();
}

WatchpointWidget *DockManager::watchpoints() const
{
    return m_watchpointWidget.data();
}

void DockManager::setDockFocusPolicy(DockFocusPolicy policy)
{
    m_dockFocusPolicy = policy;
}

void DockManager::registerMainDock(MainDockId id, DockWidget *dock)
{
    if (!dock) {
        m_mainDocks.remove(static_cast<int>(id));
        return;
    }
    m_mainDocks.insert(static_cast<int>(id), dock);
}

DockWidget *DockManager::mainDock(MainDockId id) const
{
    return m_mainDocks.value(static_cast<int>(id));
}

void DockManager::showDock(DockWidget *dock, bool explicitUserAction)
{
    if (!dock)
        return;
    dock->show();

    const bool shouldRaise =
        (m_dockFocusPolicy == DockFocusPolicy::Always) ||
        (m_dockFocusPolicy == DockFocusPolicy::ExplicitOnly && explicitUserAction);
    if (shouldRaise)
        dock->raise();
}

void DockManager::createDocks(QMenu *docksMenu)
{
    auto makeDock = [&](const QString &title, QWidget *widget, DebugDockId id,
                        Qt::DockWidgetArea area, bool coreDock) -> DockWidget * {
        const QString uniqueName = QString::fromLatin1(dockObjectName(id));
        auto *dw = new KDockWidget(uniqueName, title, m_host);
        dw->applyThinTitlebar(true);
        dw->setWidget(widget);
        applyStandardDockFeatures(dw, !coreDock);
        DockBackend::addDockWidgetCompat(m_host, dw, area, nullptr, !coreDock);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
        if (!coreDock)
            dw->hide();
#endif

        QAction *action = dw->toggleViewAction();
        docksMenu->addAction(action);

        return dw;
    };

    /* Create widgets */
    m_disasmWidget = new DisassemblyWidget(m_host);
    m_registerWidget = new RegisterWidget(m_host);
    m_hexWidget = new HexViewWidget(m_host);
    m_breakpointWidget = new BreakpointWidget(m_host);
    m_watchpointWidget = new WatchpointWidget(m_host);
    m_portMonitorWidget = new PortMonitorWidget(m_host);
    m_stackWidget = new StackWidget(m_host);
    m_keyHistoryWidget = new KeyHistoryWidget(m_host);
    m_consoleWidget = new ConsoleWidget(m_host);
    m_memVisWidget = new MemoryVisualizerWidget(m_host);
    m_cycleCounterWidget = new CycleCounterWidget(m_host);
    m_timerMonitorWidget = new TimerMonitorWidget(m_host);
    m_lcdStateWidget = new LCDStateWidget(m_host);
    m_mmuViewerWidget = new MMUViewerWidget(m_host);

    m_disasmWidget->setIconFont(m_iconFont);

    /* Create docks */
    docksMenu->addSeparator();

    m_disasmDock = makeDock(tr("Disassembly"), m_disasmWidget,
                            DebugDockId::Disasm, Qt::RightDockWidgetArea, true);
    m_registerDock = makeDock(tr("Registers"), m_registerWidget,
                              DebugDockId::Registers, Qt::RightDockWidgetArea, true);
    m_stackDock = makeDock(tr("Stack"), m_stackWidget,
                           DebugDockId::Stack, Qt::RightDockWidgetArea, false);

    /* Tab Registers and Stack together */
    DockBackend::tabifyDockWidgetCompat(m_host, m_registerDock, m_stackDock);
    m_registerDock->raise();

    m_hexDock = makeDock(tr("Memory"), m_hexWidget,
                         DebugDockId::Memory, Qt::BottomDockWidgetArea, true);
    m_breakpointDock = makeDock(tr("Breakpoints"), m_breakpointWidget,
                                DebugDockId::Breakpoints, Qt::BottomDockWidgetArea, false);
    m_watchpointDock = makeDock(tr("Watchpoints"), m_watchpointWidget,
                                DebugDockId::Watchpoints, Qt::BottomDockWidgetArea, false);
    m_portMonitorDock = makeDock(tr("Port Monitor"), m_portMonitorWidget,
                                 DebugDockId::PortMonitor, Qt::BottomDockWidgetArea, false);
    m_keyHistoryDock = makeDock(tr("Key History"), m_keyHistoryWidget,
                                DebugDockId::KeyHistory, Qt::BottomDockWidgetArea, false);

    m_consoleDock = makeDock(tr("Console"), m_consoleWidget,
                             DebugDockId::Console, Qt::BottomDockWidgetArea, true);
    m_memVisDock = makeDock(tr("Memory Visualizer"), m_memVisWidget,
                            DebugDockId::MemVis, Qt::BottomDockWidgetArea, false);
    m_cycleCounterDock = makeDock(tr("Cycle Counter"), m_cycleCounterWidget,
                                  DebugDockId::CycleCounter, Qt::BottomDockWidgetArea, false);
    m_timerMonitorDock = makeDock(tr("Timer Monitor"), m_timerMonitorWidget,
                                  DebugDockId::TimerMonitor, Qt::BottomDockWidgetArea, false);
    m_lcdStateDock = makeDock(tr("LCD State"), m_lcdStateWidget,
                              DebugDockId::LCDState, Qt::BottomDockWidgetArea, false);
    m_mmuViewerDock = makeDock(tr("MMU Viewer"), m_mmuViewerWidget,
                               DebugDockId::MMUViewer, Qt::BottomDockWidgetArea, false);

    /* Set Material icons on toggle actions */
    refreshIcons();

    /* Tab together: Memory, Breakpoints, Watchpoints, Port Monitor, + new docks */
    DockBackend::tabifyDockWidgetCompat(m_host, m_hexDock, m_breakpointDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_breakpointDock, m_watchpointDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_watchpointDock, m_portMonitorDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_portMonitorDock, m_keyHistoryDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_keyHistoryDock, m_consoleDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_consoleDock, m_memVisDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_memVisDock, m_cycleCounterDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_cycleCounterDock, m_timerMonitorDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_timerMonitorDock, m_lcdStateDock);
    DockBackend::tabifyDockWidgetCompat(m_host, m_lcdStateDock, m_mmuViewerDock);
    m_hexDock->raise();

    /* -- Connect signals ----------------------------------- */

    /* Disassembly -> debugger commands */
    connect(m_disasmWidget, &DisassemblyWidget::debugCommand,
            this, &DockManager::debugCommand);

    /* Disassembly breakpoint toggle -> refresh breakpoint/watchpoint lists */
    connect(m_disasmWidget, &DisassemblyWidget::breakpointToggled,
            this, [this](uint32_t, bool) {
                m_breakpointWidget->refresh();
                m_watchpointWidget->refresh();
            });

    /* Disassembly address select -> navigate hex view */
    connect(m_disasmWidget, &DisassemblyWidget::addressSelected,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
            });

    /* Hex view -> navigate to disassembly */
    connect(m_hexWidget, &HexViewWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            });

    /* Breakpoint/Watchpoint double-click -> navigate disassembly */
    connect(m_breakpointWidget, &BreakpointWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            });
    connect(m_watchpointWidget, &WatchpointWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
            });

    /* Port monitor -> navigate to hex view */
    connect(m_portMonitorWidget, &PortMonitorWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
            });

    /* Stack -> navigate to disassembly (for return addresses) */
    connect(m_stackWidget, &StackWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            });

    /* Stack -> navigate to hex view */
    connect(m_stackWidget, &StackWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            });

    /* Register widget -> navigate to hex view / disassembly */
    connect(m_registerWidget, &RegisterWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
            });
    connect(m_registerWidget, &RegisterWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            });

    /* Console -> debugger commands */
    connect(m_consoleWidget, &ConsoleWidget::commandSubmitted,
            this, &DockManager::debugCommand);

    /* Key history: feed keypresses from QtKeypadBridge */
    connect(&qt_keypad_bridge, &QtKeypadBridge::keyStateChanged,
            m_keyHistoryWidget, &KeyHistoryWidget::addEntry);

    /* "New Memory View" action */
    m_docksMenu = docksMenu;
    docksMenu->addSeparator();
    QAction *newMemAction = docksMenu->addAction(tr("New Memory View"));
    connect(newMemAction, &QAction::triggered, this, &DockManager::addHexViewDock);

    /* Ctrl+G: Go To Address dialog */
    auto *gotoShortcut = new QShortcut(QKeySequence(tr("Ctrl+G")), m_host);
    connect(gotoShortcut, &QShortcut::activated, this, [this]() {
        GoToDialog dlg(m_host);
        if (dlg.exec() == QDialog::Accepted) {
            uint32_t addr = dlg.getAddress();
            if (dlg.getTarget() == GoToDialog::Disassembly) {
                m_disasmWidget->goToAddress(addr);
                showDock(m_disasmDock, true);
            } else {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
            }
        }
    });
}

void DockManager::addHexViewDock()
{
    m_hexViewCount++;
    auto *widget = new HexViewWidget(m_host);
    QString title = tr("Memory %1").arg(m_hexViewCount);
    QString objName = QStringLiteral("dockMemory%1").arg(m_hexViewCount);

    auto *dw = new KDockWidget(objName, title, m_host);
    dw->applyThinTitlebar(true);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw);
    DockBackend::addDockWidgetCompat(m_host, dw, Qt::BottomDockWidgetArea);

    if (m_hexDock)
        DockBackend::tabifyDockWidgetCompat(m_host, m_hexDock, dw);
    dw->raise();

    if (m_docksMenu) {
        QAction *action = dw->toggleViewAction();
        m_docksMenu->addAction(action);
    }

    m_extraHexWidgets.append(widget);
    m_extraHexDocks.append(dw);
}

void DockManager::resetLayout()
{
    /* Re-arrange debug docks with a core-visible default set. */
    DockWidget *debugDocks[] = {
        m_disasmDock, m_registerDock, m_stackDock,
        m_hexDock, m_breakpointDock, m_watchpointDock, m_portMonitorDock,
        m_keyHistoryDock, m_consoleDock, m_memVisDock, m_cycleCounterDock,
        m_timerMonitorDock, m_lcdStateDock, m_mmuViewerDock
    };

    /* Remove all debug docks first */
    for (DockWidget *dw : debugDocks)
    {
        if (dw)
            DockBackend::removeDockWidgetCompat(m_host, dw);
    }

    /* Right area: Disassembly on top, Registers/Stack tabbed below */
    if (m_disasmDock) {
        DockBackend::addDockWidgetCompat(m_host, m_disasmDock, Qt::RightDockWidgetArea);
        m_disasmDock->setVisible(true);
    }
    if (m_registerDock) {
        DockBackend::addDockWidgetCompat(m_host, m_registerDock, Qt::RightDockWidgetArea);
        m_registerDock->setVisible(true);
    }
    if (m_stackDock) {
        DockBackend::addDockWidgetCompat(m_host, m_stackDock, Qt::RightDockWidgetArea);
        m_stackDock->setVisible(true);
        if (m_registerDock)
            DockBackend::tabifyDockWidgetCompat(m_host, m_registerDock, m_stackDock);
        if (m_registerDock) m_registerDock->raise();
    }

    /* Bottom area groups:
     * - Memory: Memory, Memory Visualizer, MMU Viewer, extra Memory views
     * - Console: Console, Breakpoints, Watchpoints, Port Monitor, Key History, timers/stats
     */
    auto placeGroup = [this](DockWidget *anchor, const QList<DockWidget *> &tabs) -> DockWidget * {
        if (!anchor)
            return nullptr;
        DockBackend::addDockWidgetCompat(m_host, anchor, Qt::BottomDockWidgetArea);
        anchor->setVisible(true);
        for (DockWidget *dw : tabs) {
            if (!dw)
                continue;
            DockBackend::addDockWidgetCompat(m_host, dw, Qt::BottomDockWidgetArea, anchor);
            dw->setVisible(true);
            DockBackend::tabifyDockWidgetCompat(m_host, anchor, dw);
        }
        return anchor;
    };

    QList<DockWidget *> memoryTabs = { m_memVisDock, m_mmuViewerDock };
    for (DockWidget *extra : m_extraHexDocks)
        memoryTabs.append(extra);
    DockWidget *memoryRoot = placeGroup(m_hexDock, memoryTabs);
    DockWidget *debugToolsRoot = placeGroup(m_consoleDock,
                                            { m_breakpointDock, m_watchpointDock, m_portMonitorDock,
                                              m_keyHistoryDock, m_timerMonitorDock, m_lcdStateDock,
                                              m_cycleCounterDock });

    if (memoryRoot && debugToolsRoot)
        DockBackend::splitDockWidgetCompat(m_host, memoryRoot, debugToolsRoot, Qt::Horizontal);

    if (m_hexDock)
        m_hexDock->raise();

    /* Sizing hints */
    QList<DockWidget *> hTargets;
    QList<int> hSizes;
    if (m_disasmDock) { hTargets << m_disasmDock; hSizes << 400; }
    if (!hTargets.isEmpty())
        DockBackend::resizeDocksCompat(m_host, hTargets, hSizes, Qt::Horizontal);

    QList<DockWidget *> vTargets;
    QList<int> vSizes;
    if (memoryRoot) { vTargets << memoryRoot; vSizes << 200; }
    else if (debugToolsRoot) { vTargets << debugToolsRoot; vSizes << 200; }
    if (!vTargets.isEmpty())
        DockBackend::resizeDocksCompat(m_host, vTargets, vSizes, Qt::Vertical);

    auto hideByDefault = [](DockWidget *dock) {
        if (dock)
            dock->setVisible(false);
    };
    hideByDefault(m_stackDock);
    hideByDefault(m_breakpointDock);
    hideByDefault(m_watchpointDock);
    hideByDefault(m_portMonitorDock);
    hideByDefault(m_keyHistoryDock);
    hideByDefault(m_memVisDock);
    hideByDefault(m_cycleCounterDock);
    hideByDefault(m_timerMonitorDock);
    hideByDefault(m_lcdStateDock);
    hideByDefault(m_mmuViewerDock);
    for (DockWidget *extra : m_extraHexDocks)
        hideByDefault(extra);

    if (m_disasmDock) m_disasmDock->setVisible(true);
    if (m_registerDock) m_registerDock->setVisible(true);
    if (m_hexDock) m_hexDock->setVisible(true);
    if (m_consoleDock) m_consoleDock->setVisible(true);
}
