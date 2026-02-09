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
#include "debugger/gotodialog.h"
#include <QFileDialog>
#include <QMessageBox>

DebugDockManager::DebugDockManager(QMainWindow *host, const QFont &iconFont,
                                   QObject *parent)
    : QObject(parent), m_host(host), m_iconFont(iconFont)
{
}

void DebugDockManager::createDocks(QMenu *docksMenu)
{
    auto makeDock = [&](const QString &title, QWidget *widget, const QString &objName,
                        Qt::DockWidgetArea area) -> DockWidget * {
        auto *dw = new DockWidget(title, m_host);
        dw->hideTitlebar(true);
        dw->setObjectName(objName);
        dw->setWidget(widget);
        dw->setAllowedAreas(Qt::AllDockWidgetAreas);
        dw->setFeatures(QDockWidget::DockWidgetClosable |
                        QDockWidget::DockWidgetMovable |
                        QDockWidget::DockWidgetFloatable);
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

    m_disasmWidget->setIconFont(m_iconFont);

    /* Create docks */
    docksMenu->addSeparator();

    m_disasmDock = makeDock(tr("Disassembly"), m_disasmWidget,
                            QStringLiteral("dockDisasm"), Qt::RightDockWidgetArea);
    m_registerDock = makeDock(tr("Registers"), m_registerWidget,
                              QStringLiteral("dockRegisters"), Qt::RightDockWidgetArea);
    m_stackDock = makeDock(tr("Stack"), m_stackWidget,
                            QStringLiteral("dockStack"), Qt::RightDockWidgetArea);

    /* Tab Registers and Stack together */
    m_host->tabifyDockWidget(m_registerDock, m_stackDock);
    m_registerDock->raise();

    m_hexDock = makeDock(tr("Memory"), m_hexWidget,
                         QStringLiteral("dockMemory"), Qt::BottomDockWidgetArea);
    m_breakpointDock = makeDock(tr("Breakpoints"), m_breakpointWidget,
                                QStringLiteral("dockBreakpoints"), Qt::BottomDockWidgetArea);
    m_watchpointDock = makeDock(tr("Watchpoints"), m_watchpointWidget,
                                QStringLiteral("dockWatchpoints"), Qt::BottomDockWidgetArea);
    m_portMonitorDock = makeDock(tr("Port Monitor"), m_portMonitorWidget,
                                 QStringLiteral("dockPortMonitor"), Qt::BottomDockWidgetArea);
    m_keyHistoryDock = makeDock(tr("Key History"), m_keyHistoryWidget,
                                QStringLiteral("dockKeyHistory"), Qt::BottomDockWidgetArea);

    m_consoleDock = makeDock(tr("Console"), m_consoleWidget,
                              QStringLiteral("dockConsole"), Qt::BottomDockWidgetArea);
    m_memVisDock = makeDock(tr("Memory Visualizer"), m_memVisWidget,
                             QStringLiteral("dockMemVis"), Qt::BottomDockWidgetArea);
    m_cycleCounterDock = makeDock(tr("Cycle Counter"), m_cycleCounterWidget,
                                   QStringLiteral("dockCycleCounter"), Qt::BottomDockWidgetArea);
    m_timerMonitorDock = makeDock(tr("Timer Monitor"), m_timerMonitorWidget,
                                   QStringLiteral("dockTimerMonitor"), Qt::BottomDockWidgetArea);
    m_lcdStateDock = makeDock(tr("LCD State"), m_lcdStateWidget,
                               QStringLiteral("dockLCDState"), Qt::BottomDockWidgetArea);

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
    dw->setAllowedAreas(Qt::AllDockWidgetAreas);
    dw->setFeatures(QDockWidget::DockWidgetClosable |
                    QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
    m_host->addDockWidget(Qt::BottomDockWidgetArea, dw);

    if (m_hexDock)
        m_host->tabifyDockWidget(m_hexDock, dw);
    dw->raise();

    if (m_docksMenu) {
        QAction *action = dw->toggleViewAction();
        m_docksMenu->addAction(action);
    }

    m_extraHexWidgets.append(widget);
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
}

void DebugDockManager::refreshAll()
{
    /* Refresh high-priority widgets immediately (disassembly, registers) */
    if (m_disasmWidget) m_disasmWidget->refresh();
    if (m_registerWidget) m_registerWidget->refresh();

    /* Stagger remaining widgets across separate event-loop iterations
     * so no single callback blocks the UI for too long.  Each widget
     * gets its own QTimer::singleShot(0, ...) with increasing delay. */
    auto defer = [this](int ms, auto fn) {
        QTimer::singleShot(ms, this, fn);
    };

    /* Lightweight widgets first (tables with few rows, no MMIO reads) */
    defer(0, [this]() {
        if (m_breakpointWidget) m_breakpointWidget->refresh();
        if (m_watchpointWidget) m_watchpointWidget->refresh();
    });

    defer(0, [this]() {
        if (m_hexWidget) m_hexWidget->refresh();
        for (HexViewWidget *hw : m_extraHexWidgets)
            if (hw) hw->refresh();
    });

    /* Heavier widgets each get their own iteration */
    defer(0, [this]() { if (m_stackWidget) m_stackWidget->refresh(); });
    defer(0, [this]() { if (m_portMonitorWidget) m_portMonitorWidget->refresh(); });
    defer(0, [this]() { if (m_timerMonitorWidget) m_timerMonitorWidget->refresh(); });
    defer(0, [this]() { if (m_lcdStateWidget) m_lcdStateWidget->refresh(); });
    defer(0, [this]() { if (m_memVisWidget) m_memVisWidget->refresh(); });
    defer(0, [this]() { if (m_cycleCounterWidget) m_cycleCounterWidget->refresh(); });
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
        m_timerMonitorDock, m_lcdStateDock
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
        m_timerMonitorDock, m_lcdStateDock
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
