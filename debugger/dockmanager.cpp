#include "debugger/dockmanager.h"

#include <QMainWindow>
#include <QMenu>
#include <QShortcut>
#include <QTimer>

#include "ui/dockwidget.h"
#include "ui/keypadbridge.h"
#include "ui/materialicons.h"
#include "debugger/disassembly/disassemblywidget.h"
#include "debugger/registers/registerwidget.h"
#include "debugger/hexview/hexviewwidget.h"
#include "debugger/breakpoints/breakpointwidget.h"
#include "debugger/watchpoints/watchpointwidget.h"
#include "debugger/portmonitor/portmonitorwidget.h"
#include "debugger/stack/stackwidget.h"
#include "debugger/keyhistory/keyhistorywidget.h"
#include "debugger/console/consolewidget.h"
#include "debugger/memvisualizer/memoryvisualizerwidget.h"
#include "debugger/cyclecounter/cyclecounterwidget.h"
#include "debugger/timermonitor/timermonitorwidget.h"
#include "debugger/lcdstate/lcdstatewidget.h"
#include "debugger/mmuviewer/mmuviewerwidget.h"
#include "debugger/gotodialog.h"
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

void applyStandardDockFeatures(QDockWidget *dw)
{
    if (!dw)
        return;
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    dw->setFeatures(QDockWidget::DockWidgetClosable |
                    QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
}

} // namespace

DebugDockManager::DebugDockManager(QMainWindow *host, const QFont &iconFont,
                                   QObject *parent)
    : QObject(parent), m_host(host), m_iconFont(iconFont)
{
}

void DebugDockManager::createDocks(QMenu *docksMenu)
{
    auto makeDock = [&](const QString &title, QWidget *widget, DebugDockId id,
                        Qt::DockWidgetArea area) -> DockWidget * {
        auto *dw = new DockWidget(title, m_host);
        dw->hideTitlebar(true);
        dw->setObjectName(QString::fromLatin1(dockObjectName(id)));
        dw->setWidget(widget);
        applyStandardDockFeatures(dw);
        m_host->addDockWidget(area, dw);

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
                            DebugDockId::Disasm, Qt::RightDockWidgetArea);
    m_registerDock = makeDock(tr("Registers"), m_registerWidget,
                              DebugDockId::Registers, Qt::RightDockWidgetArea);
    m_stackDock = makeDock(tr("Stack"), m_stackWidget,
                           DebugDockId::Stack, Qt::RightDockWidgetArea);

    /* Tab Registers and Stack together */
    m_host->tabifyDockWidget(m_registerDock, m_stackDock);
    m_registerDock->raise();

    m_hexDock = makeDock(tr("Memory"), m_hexWidget,
                         DebugDockId::Memory, Qt::BottomDockWidgetArea);
    m_breakpointDock = makeDock(tr("Breakpoints"), m_breakpointWidget,
                                DebugDockId::Breakpoints, Qt::BottomDockWidgetArea);
    m_watchpointDock = makeDock(tr("Watchpoints"), m_watchpointWidget,
                                DebugDockId::Watchpoints, Qt::BottomDockWidgetArea);
    m_portMonitorDock = makeDock(tr("Port Monitor"), m_portMonitorWidget,
                                 DebugDockId::PortMonitor, Qt::BottomDockWidgetArea);
    m_keyHistoryDock = makeDock(tr("Key History"), m_keyHistoryWidget,
                                DebugDockId::KeyHistory, Qt::BottomDockWidgetArea);

    m_consoleDock = makeDock(tr("Console"), m_consoleWidget,
                             DebugDockId::Console, Qt::BottomDockWidgetArea);
    m_memVisDock = makeDock(tr("Memory Visualizer"), m_memVisWidget,
                            DebugDockId::MemVis, Qt::BottomDockWidgetArea);
    m_cycleCounterDock = makeDock(tr("Cycle Counter"), m_cycleCounterWidget,
                                  DebugDockId::CycleCounter, Qt::BottomDockWidgetArea);
    m_timerMonitorDock = makeDock(tr("Timer Monitor"), m_timerMonitorWidget,
                                  DebugDockId::TimerMonitor, Qt::BottomDockWidgetArea);
    m_lcdStateDock = makeDock(tr("LCD State"), m_lcdStateWidget,
                              DebugDockId::LCDState, Qt::BottomDockWidgetArea);
    m_mmuViewerDock = makeDock(tr("MMU Viewer"), m_mmuViewerWidget,
                               DebugDockId::MMUViewer, Qt::BottomDockWidgetArea);

    /* Set Material icons on toggle actions */
    refreshIcons();

    /* Tab together: Memory, Breakpoints, Watchpoints, Port Monitor, + new docks */
    m_host->tabifyDockWidget(m_hexDock, m_breakpointDock);
    m_host->tabifyDockWidget(m_breakpointDock, m_watchpointDock);
    m_host->tabifyDockWidget(m_watchpointDock, m_portMonitorDock);
    m_host->tabifyDockWidget(m_portMonitorDock, m_keyHistoryDock);
    m_host->tabifyDockWidget(m_keyHistoryDock, m_consoleDock);
    m_host->tabifyDockWidget(m_consoleDock, m_memVisDock);
    m_host->tabifyDockWidget(m_memVisDock, m_cycleCounterDock);
    m_host->tabifyDockWidget(m_cycleCounterDock, m_timerMonitorDock);
    m_host->tabifyDockWidget(m_timerMonitorDock, m_lcdStateDock);
    m_host->tabifyDockWidget(m_lcdStateDock, m_mmuViewerDock);
    m_hexDock->raise();

    /* -- Connect signals ----------------------------------- */

    /* Disassembly -> debugger commands */
    connect(m_disasmWidget, &DisassemblyWidget::debugCommand,
            this, &DebugDockManager::debugCommand);

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
                m_hexDock->show();
                m_hexDock->raise();
            });

    /* Hex view -> navigate to disassembly */
    connect(m_hexWidget, &HexViewWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            });

    /* Breakpoint/Watchpoint double-click -> navigate disassembly */
    connect(m_breakpointWidget, &BreakpointWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            });
    connect(m_watchpointWidget, &WatchpointWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                m_hexDock->show();
                m_hexDock->raise();
            });

    /* Port monitor -> navigate to hex view */
    connect(m_portMonitorWidget, &PortMonitorWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                m_hexDock->show();
                m_hexDock->raise();
            });

    /* Stack -> navigate to disassembly (for return addresses) */
    connect(m_stackWidget, &StackWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            });

    /* Stack -> navigate to hex view */
    connect(m_stackWidget, &StackWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            });

    /* Register widget -> navigate to hex view / disassembly */
    connect(m_registerWidget, &RegisterWidget::goToAddress,
            this, [this](uint32_t addr) {
                m_hexWidget->goToAddress(addr);
                m_hexDock->show();
                m_hexDock->raise();
            });
    connect(m_registerWidget, &RegisterWidget::gotoDisassembly,
            this, [this](uint32_t addr) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            });

    /* Console -> debugger commands */
    connect(m_consoleWidget, &ConsoleWidget::commandSubmitted,
            this, &DebugDockManager::debugCommand);

    /* Key history: feed keypresses from QtKeypadBridge */
    connect(&qt_keypad_bridge, &QtKeypadBridge::keyStateChanged,
            m_keyHistoryWidget, &KeyHistoryWidget::addEntry);

    /* "New Memory View" action */
    m_docksMenu = docksMenu;
    docksMenu->addSeparator();
    QAction *newMemAction = docksMenu->addAction(tr("New Memory View"));
    connect(newMemAction, &QAction::triggered, this, &DebugDockManager::addHexViewDock);

    /* Ctrl+G: Go To Address dialog */
    auto *gotoShortcut = new QShortcut(QKeySequence(tr("Ctrl+G")), m_host);
    connect(gotoShortcut, &QShortcut::activated, this, [this]() {
        GoToDialog dlg(m_host);
        if (dlg.exec() == QDialog::Accepted) {
            uint32_t addr = dlg.getAddress();
            if (dlg.getTarget() == GoToDialog::Disassembly) {
                m_disasmWidget->goToAddress(addr);
                m_disasmDock->show();
                m_disasmDock->raise();
            } else {
                m_hexWidget->goToAddress(addr);
                m_hexDock->show();
                m_hexDock->raise();
            }
        }
    });
}

void DebugDockManager::addHexViewDock()
{
    m_hexViewCount++;
    auto *widget = new HexViewWidget(m_host);
    QString title = tr("Memory %1").arg(m_hexViewCount);
    QString objName = QStringLiteral("dockMemory%1").arg(m_hexViewCount);

    auto *dw = new DockWidget(title, m_host);
    dw->hideTitlebar(true);
    dw->setObjectName(objName);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw);
    m_host->addDockWidget(Qt::BottomDockWidgetArea, dw);

    if (m_hexDock)
        m_host->tabifyDockWidget(m_hexDock, dw);
    dw->raise();

    if (m_docksMenu) {
        QAction *action = dw->toggleViewAction();
        m_docksMenu->addAction(action);
    }

    m_extraHexWidgets.append(widget);
    m_extraHexDocks.append(dw);
}

void DebugDockManager::ensureExtraHexDocks(int count)
{
    if (count <= 0)
        return;
    while (extraHexDockCount() < count)
        addHexViewDock();
}

void DebugDockManager::refreshIcons()
{
    using namespace MaterialIcons;
    const QColor fg = m_host->palette().color(QPalette::WindowText);
    auto setIcon = [&](DockWidget *dw, ushort cp) {
        if (dw) dw->toggleViewAction()->setIcon(fromCodepoint(m_iconFont, cp, fg));
    };
    setIcon(m_disasmDock,       CP::Code);
    setIcon(m_registerDock,     CP::List);
    setIcon(m_stackDock,        CP::ViewColumn);
    setIcon(m_hexDock,          CP::Memory);
    setIcon(m_breakpointDock,   CP::Bookmark);
    setIcon(m_watchpointDock,   CP::Visibility);
    setIcon(m_portMonitorDock,  CP::Monitor);
    setIcon(m_keyHistoryDock,   CP::History);
    setIcon(m_consoleDock,      CP::Terminal);
    setIcon(m_memVisDock,       CP::GridOn);
    setIcon(m_cycleCounterDock, CP::CycleCounter);
    setIcon(m_timerMonitorDock, CP::Timer);
    setIcon(m_lcdStateDock,     CP::Display);
    setIcon(m_mmuViewerDock,    CP::Layers);
}

void DebugDockManager::refreshAll()
{
    auto refreshNow = [](auto *widget, bool enabled = true) {
        if (widget && enabled)
            widget->refresh();
    };
    auto dockVisible = [](DockWidget *dock) {
        return dock && dock->isVisible();
    };

    /* Refresh high-priority widgets immediately (disassembly, registers). */
    refreshNow(m_disasmWidget, dockVisible(m_disasmDock));
    refreshNow(m_registerWidget, dockVisible(m_registerDock));

    /* Stagger remaining widgets across separate event-loop iterations
     * so no single callback blocks the UI for too long.  Each widget
     * gets its own QTimer::singleShot(0, ...) with increasing delay. */
    auto defer = [this](int ms, auto fn) {
        QTimer::singleShot(ms, this, fn);
    };

    /* Lightweight widgets first (tables with few rows, no MMIO reads). */
    defer(0, [this]() {
        if (m_breakpointWidget && m_breakpointDock && m_breakpointDock->isVisible())
            m_breakpointWidget->refresh();
        if (m_watchpointWidget && m_watchpointDock && m_watchpointDock->isVisible())
            m_watchpointWidget->refresh();
    });

    defer(0, [this]() {
        if (m_hexWidget && m_hexDock && m_hexDock->isVisible())
            m_hexWidget->refresh();
        const int n = qMin(m_extraHexWidgets.size(), m_extraHexDocks.size());
        for (int i = 0; i < n; i++) {
            HexViewWidget *hw = m_extraHexWidgets.at(i);
            DockWidget *dock = m_extraHexDocks.at(i);
            if (hw && dock && dock->isVisible())
                hw->refresh();
        }
    });

    /* Heavier widgets each get their own iteration. */
    const auto deferRefresh = [&](auto *widget) {
        defer(0, [widget]() {
            if (widget)
                widget->refresh();
        });
    };
    const auto deferRefreshIfVisible = [&](auto *widget, DockWidget *dock) {
        if (!dock || !dock->isVisible())
            return;
        deferRefresh(widget);
    };
    deferRefreshIfVisible(m_stackWidget, m_stackDock);
    deferRefreshIfVisible(m_portMonitorWidget, m_portMonitorDock);
    deferRefreshIfVisible(m_timerMonitorWidget, m_timerMonitorDock);
    deferRefreshIfVisible(m_lcdStateWidget, m_lcdStateDock);
    deferRefreshIfVisible(m_mmuViewerWidget, m_mmuViewerDock);
    deferRefreshIfVisible(m_memVisWidget, m_memVisDock);
    deferRefreshIfVisible(m_cycleCounterWidget, m_cycleCounterDock);
}

void DebugDockManager::retranslate()
{
    if (m_disasmDock) m_disasmDock->setWindowTitle(tr("Disassembly"));
    if (m_registerDock) m_registerDock->setWindowTitle(tr("Registers"));
    if (m_hexDock) m_hexDock->setWindowTitle(tr("Memory"));
    if (m_breakpointDock) m_breakpointDock->setWindowTitle(tr("Breakpoints"));
    if (m_watchpointDock) m_watchpointDock->setWindowTitle(tr("Watchpoints"));
    if (m_portMonitorDock) m_portMonitorDock->setWindowTitle(tr("Port Monitor"));
    if (m_stackDock) m_stackDock->setWindowTitle(tr("Stack"));
    if (m_keyHistoryDock) m_keyHistoryDock->setWindowTitle(tr("Key History"));
    if (m_consoleDock) m_consoleDock->setWindowTitle(tr("Console"));
    if (m_memVisDock) m_memVisDock->setWindowTitle(tr("Memory Visualizer"));
    if (m_cycleCounterDock) m_cycleCounterDock->setWindowTitle(tr("Cycle Counter"));
    if (m_timerMonitorDock) m_timerMonitorDock->setWindowTitle(tr("Timer Monitor"));
    if (m_lcdStateDock) m_lcdStateDock->setWindowTitle(tr("LCD State"));
    if (m_mmuViewerDock) m_mmuViewerDock->setWindowTitle(tr("MMU Viewer"));
}

void DebugDockManager::raise()
{
    m_autoShownDocks.clear();

    auto autoShow = [this](DockWidget *dock) {
        if (!dock) return;
        if (!dock->isVisible()) {
            dock->setVisible(true);
            m_autoShownDocks.insert(dock);
        }
    };

    autoShow(m_disasmDock);
    autoShow(m_registerDock);
    autoShow(m_stackDock);
    autoShow(m_hexDock);
    autoShow(m_breakpointDock);
    autoShow(m_consoleDock);

    if (m_disasmDock) m_disasmDock->raise();
}

void DebugDockManager::hideAutoShown()
{
    for (DockWidget *dock : m_autoShownDocks)
    {
        if (dock && dock->isVisible())
            dock->setVisible(false);
    }
    m_autoShownDocks.clear();
}

void DebugDockManager::setEditMode(bool enabled)
{
    const auto dockChildren = m_host->findChildren<DockWidget *>();
    for (DockWidget *dw : dockChildren)
        dw->hideTitlebar(!enabled);
}

void DebugDockManager::resetLayout()
{
    /* Show all debug docks and re-arrange them to a sensible default. */
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
            m_host->removeDockWidget(dw);
    }

    /* Right area: Disassembly on top, Registers/Stack tabbed below */
    if (m_disasmDock) {
        m_host->addDockWidget(Qt::RightDockWidgetArea, m_disasmDock);
        m_disasmDock->setVisible(true);
    }
    if (m_registerDock) {
        m_host->addDockWidget(Qt::RightDockWidgetArea, m_registerDock);
        m_registerDock->setVisible(true);
    }
    if (m_stackDock) {
        m_host->addDockWidget(Qt::RightDockWidgetArea, m_stackDock);
        m_stackDock->setVisible(true);
        if (m_registerDock)
            m_host->tabifyDockWidget(m_registerDock, m_stackDock);
        if (m_registerDock) m_registerDock->raise();
    }

    /* Bottom area: Memory, Breakpoints, Watchpoints, Port Monitor, etc. tabbed */
    DockWidget *bottomDocks[] = {
        m_hexDock, m_breakpointDock, m_watchpointDock, m_portMonitorDock,
        m_keyHistoryDock, m_consoleDock, m_memVisDock, m_cycleCounterDock,
        m_timerMonitorDock, m_lcdStateDock, m_mmuViewerDock
    };
    DockWidget *firstBottom = nullptr;
    for (DockWidget *dw : bottomDocks) {
        if (!dw) continue;
        m_host->addDockWidget(Qt::BottomDockWidgetArea, dw);
        dw->setVisible(true);
        if (firstBottom)
            m_host->tabifyDockWidget(firstBottom, dw);
        else
            firstBottom = dw;
    }
    if (m_hexDock) m_hexDock->raise();

    /* Sizing hints */
    QList<QDockWidget *> hTargets;
    QList<int> hSizes;
    if (m_disasmDock) { hTargets << m_disasmDock; hSizes << 400; }
    if (!hTargets.isEmpty())
        m_host->resizeDocks(hTargets, hSizes, Qt::Horizontal);

    QList<QDockWidget *> vTargets;
    QList<int> vSizes;
    if (firstBottom) { vTargets << firstBottom; vSizes << 200; }
    if (!vTargets.isEmpty())
        m_host->resizeDocks(vTargets, vSizes, Qt::Vertical);
}
