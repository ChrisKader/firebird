#include "debugger/dockmanager.h"

#include <QMainWindow>
#include <QMenu>
#include <QShortcut>
#include <QTimer>
#include <QJsonArray>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/KDDockWidgets.h>
#endif

#include "ui/dockwidget.h"
#include "ui/dockstate.h"
#include "ui/kdockwidget.h"
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

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
KDDockWidgets::QtWidgets::MainWindow *asKDDMainWindow(QMainWindow *window)
{
    return dynamic_cast<KDDockWidgets::QtWidgets::MainWindow *>(window);
}

KDDockWidgets::Location toKDDLocation(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return KDDockWidgets::Location_OnLeft;
    case Qt::TopDockWidgetArea: return KDDockWidgets::Location_OnTop;
    case Qt::RightDockWidgetArea: return KDDockWidgets::Location_OnRight;
    case Qt::BottomDockWidgetArea: return KDDockWidgets::Location_OnBottom;
    default: return KDDockWidgets::Location_OnRight;
    }
}
#endif

void addDockWidgetCompat(QMainWindow *window,
                         DockWidget *dock,
                         Qt::DockWidgetArea area,
                         DockWidget *relativeTo = nullptr,
                         bool startHidden = false)
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (auto *kdd = asKDDMainWindow(window)) {
        KDDockWidgets::InitialOption initial;
        if (dock->widget()) {
            const QSize hinted = dock->widget()->sizeHint();
            if (hinted.isValid() && hinted.width() > 0 && hinted.height() > 0)
                initial.preferredSize = hinted;
        }
        if (startHidden)
            initial.visibility = KDDockWidgets::InitialVisibilityOption::StartHidden;
        kdd->addDockWidget(dock, toKDDLocation(area), relativeTo, initial);
        return;
    }
#else
    Q_UNUSED(relativeTo);
    Q_UNUSED(startHidden);
    window->addDockWidget(area, dock);
#endif
}

void tabifyDockWidgetCompat(QMainWindow *window, DockWidget *first, DockWidget *second)
{
    if (!window || !first || !second)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    if (asKDDMainWindow(window)) {
        first->addDockWidgetAsTab(second);
        return;
    }
#else
    window->tabifyDockWidget(first, second);
#endif
}

void removeDockWidgetCompat(QMainWindow *window, DockWidget *dock)
{
    if (!window || !dock)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    Q_UNUSED(window);
    dock->close();
#else
    window->removeDockWidget(dock);
#endif
}

void splitDockWidgetCompat(QMainWindow *window, DockWidget *first, DockWidget *second, Qt::Orientation orientation)
{
    if (!window || !first || !second)
        return;
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    Q_UNUSED(window);
    KDDockWidgets::InitialOption initial;
    if (second->widget()) {
        const QSize hinted = second->widget()->sizeHint();
        if (hinted.isValid() && hinted.width() > 0 && hinted.height() > 0)
            initial.preferredSize = hinted;
    }
    first->addDockWidgetToContainingWindow(
        second,
        orientation == Qt::Horizontal ? KDDockWidgets::Location_OnRight
                                      : KDDockWidgets::Location_OnBottom,
        first,
        initial);
#else
    window->splitDockWidget(first, second, orientation);
#endif
}

void resizeDocksCompat(QMainWindow *window,
                       const QList<DockWidget *> &docks,
                       const QList<int> &sizes,
                       Qt::Orientation orientation)
{
    Q_UNUSED(window);
    Q_UNUSED(docks);
    Q_UNUSED(sizes);
    Q_UNUSED(orientation);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
    if (!window)
        return;
    QList<QDockWidget *> qDocks;
    qDocks.reserve(docks.size());
    for (DockWidget *dock : docks)
        qDocks.append(dock);
    window->resizeDocks(qDocks, sizes, orientation);
#endif
}

} // namespace

DebugDockManager::DebugDockManager(QMainWindow *host, const QFont &iconFont,
                                   QObject *parent)
    : QObject(parent), m_host(host), m_iconFont(iconFont)
{
}

DisassemblyWidget *DebugDockManager::disassembly() const
{
    return m_disasmWidget.data();
}

HexViewWidget *DebugDockManager::hexView() const
{
    return m_hexWidget.data();
}

ConsoleWidget *DebugDockManager::console() const
{
    return m_consoleWidget.data();
}

DockWidget *DebugDockManager::consoleDock() const
{
    return m_consoleDock.data();
}

WatchpointWidget *DebugDockManager::watchpoints() const
{
    return m_watchpointWidget.data();
}

void DebugDockManager::setDockFocusPolicy(DockFocusPolicy policy)
{
    m_dockFocusPolicy = policy;
}

void DebugDockManager::showDock(DockWidget *dock, bool explicitUserAction)
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

void DebugDockManager::createDocks(QMenu *docksMenu)
{
    auto makeDock = [&](const QString &title, QWidget *widget, DebugDockId id,
                        Qt::DockWidgetArea area, bool coreDock) -> DockWidget * {
        const QString uniqueName = QString::fromLatin1(dockObjectName(id));
        auto *dw = new KDockWidget(uniqueName, title, m_host);
        dw->applyThinTitlebar(true);
        dw->setWidget(widget);
        applyStandardDockFeatures(dw, !coreDock);
        addDockWidgetCompat(m_host, dw, area, nullptr, !coreDock);
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
    tabifyDockWidgetCompat(m_host, m_registerDock, m_stackDock);
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
    tabifyDockWidgetCompat(m_host, m_hexDock, m_breakpointDock);
    tabifyDockWidgetCompat(m_host, m_breakpointDock, m_watchpointDock);
    tabifyDockWidgetCompat(m_host, m_watchpointDock, m_portMonitorDock);
    tabifyDockWidgetCompat(m_host, m_portMonitorDock, m_keyHistoryDock);
    tabifyDockWidgetCompat(m_host, m_keyHistoryDock, m_consoleDock);
    tabifyDockWidgetCompat(m_host, m_consoleDock, m_memVisDock);
    tabifyDockWidgetCompat(m_host, m_memVisDock, m_cycleCounterDock);
    tabifyDockWidgetCompat(m_host, m_cycleCounterDock, m_timerMonitorDock);
    tabifyDockWidgetCompat(m_host, m_timerMonitorDock, m_lcdStateDock);
    tabifyDockWidgetCompat(m_host, m_lcdStateDock, m_mmuViewerDock);
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
                showDock(m_disasmDock, true);
            } else {
                m_hexWidget->goToAddress(addr);
                showDock(m_hexDock, true);
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

    auto *dw = new KDockWidget(objName, title, m_host);
    dw->applyThinTitlebar(true);
    dw->setWidget(widget);
    applyStandardDockFeatures(dw);
    addDockWidgetCompat(m_host, dw, Qt::BottomDockWidgetArea);

    if (m_hexDock)
        tabifyDockWidgetCompat(m_host, m_hexDock, dw);
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

QJsonObject DebugDockManager::serializeDockStates() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("firebird.debug.dockstate.v1"));

    QJsonArray docks;
    const auto appendState = [&docks](DockWidget *dock) {
        if (!dock)
            return;
        DockStateSerializable *serializable = dynamic_cast<DockStateSerializable *>(dock->widget());
        if (!serializable)
            return;
        QJsonObject item;
        item.insert(QStringLiteral("dockId"), dock->objectName());
        item.insert(QStringLiteral("customState"), serializable->serializeState());
        docks.append(item);
    };

    appendState(m_disasmDock);
    appendState(m_registerDock);
    appendState(m_hexDock);
    appendState(m_breakpointDock);
    appendState(m_watchpointDock);
    appendState(m_portMonitorDock);
    appendState(m_stackDock);
    appendState(m_keyHistoryDock);
    appendState(m_consoleDock);
    appendState(m_memVisDock);
    appendState(m_cycleCounterDock);
    appendState(m_timerMonitorDock);
    appendState(m_lcdStateDock);
    appendState(m_mmuViewerDock);
    for (DockWidget *dock : m_extraHexDocks)
        appendState(dock);

    root.insert(QStringLiteral("docks"), docks);
    return root;
}

void DebugDockManager::restoreDockStates(const QJsonObject &stateRoot)
{
    auto restoreOne = [this](const QString &dockId, const QJsonObject &customState) {
        if (dockId.isEmpty())
            return;
        DockWidget *dock = m_host ? m_host->findChild<DockWidget *>(dockId) : nullptr;
        if (!dock)
            return;
        DockStateSerializable *serializable = dynamic_cast<DockStateSerializable *>(dock->widget());
        if (!serializable)
            return;
        serializable->restoreState(customState);
    };

    const QJsonArray items = stateRoot.value(QStringLiteral("docks")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        restoreOne(item.value(QStringLiteral("dockId")).toString(),
                   item.value(QStringLiteral("customState")).toObject());
    }
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

void DebugDockManager::markDirty(uint32_t flags)
{
    m_dirtyFlags |= flags;
}

void DebugDockManager::refreshAll()
{
    if (m_dirtyFlags == 0)
        return;

    const uint32_t dirty = m_dirtyFlags;
    const auto shouldRefresh = [dirty](uint32_t flag) {
        return (dirty & flag) != 0;
    };
    auto refreshNow = [](auto *widget, bool enabled = true) {
        if (widget && enabled)
            widget->refresh();
    };
    auto dockVisible = [](DockWidget *dock) {
        return dock && dock->isVisible();
    };

    /* Refresh high-priority widgets immediately (disassembly, registers). */
    refreshNow(m_disasmWidget.data(), shouldRefresh(DIRTY_DISASM) && dockVisible(m_disasmDock));
    refreshNow(m_registerWidget.data(), shouldRefresh(DIRTY_REGS) && dockVisible(m_registerDock));

    /* Stagger remaining widgets across separate event-loop iterations
     * so no single callback blocks the UI for too long.  Each widget
     * gets its own QTimer::singleShot(0, ...) with increasing delay. */
    auto defer = [this](int ms, auto fn) {
        QTimer::singleShot(ms, this, fn);
    };

    /* Lightweight widgets first (tables with few rows, no MMIO reads). */
    defer(0, [this, dirty]() {
        if ((dirty & DIRTY_BREAKS) && m_breakpointWidget && m_breakpointDock && m_breakpointDock->isVisible())
            m_breakpointWidget->refresh();
        if ((dirty & DIRTY_BREAKS) && m_watchpointWidget && m_watchpointDock && m_watchpointDock->isVisible())
            m_watchpointWidget->refresh();
    });

    defer(0, [this, dirty]() {
        if ((dirty & DIRTY_MEMORY) && m_hexWidget && m_hexDock && m_hexDock->isVisible())
            m_hexWidget->refresh();
        if ((dirty & DIRTY_MEMORY) == 0)
            return;
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
    const auto deferRefreshIfVisible = [&](auto *widget, DockWidget *dock, uint32_t flag) {
        if ((dirty & flag) == 0)
            return;
        if (!dock || !dock->isVisible())
            return;
        deferRefresh(widget);
    };
    deferRefreshIfVisible(m_stackWidget.data(), m_stackDock, DIRTY_STACK);
    deferRefreshIfVisible(m_portMonitorWidget.data(), m_portMonitorDock, DIRTY_IO);
    deferRefreshIfVisible(m_timerMonitorWidget.data(), m_timerMonitorDock, DIRTY_IO);
    deferRefreshIfVisible(m_lcdStateWidget.data(), m_lcdStateDock, DIRTY_IO);
    deferRefreshIfVisible(m_mmuViewerWidget.data(), m_mmuViewerDock, DIRTY_IO);
    deferRefreshIfVisible(m_memVisWidget.data(), m_memVisDock, DIRTY_STATS);
    deferRefreshIfVisible(m_cycleCounterWidget.data(), m_cycleCounterDock, DIRTY_STATS);

    m_dirtyFlags = 0;
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
            showDock(dock, false);
            m_autoShownDocks.insert(dock);
        }
    };

    autoShow(m_disasmDock);
    autoShow(m_registerDock);
    autoShow(m_hexDock);
    autoShow(m_consoleDock);

    showDock(m_disasmDock, false);
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
            removeDockWidgetCompat(m_host, dw);
    }

    /* Right area: Disassembly on top, Registers/Stack tabbed below */
    if (m_disasmDock) {
        addDockWidgetCompat(m_host, m_disasmDock, Qt::RightDockWidgetArea);
        m_disasmDock->setVisible(true);
    }
    if (m_registerDock) {
        addDockWidgetCompat(m_host, m_registerDock, Qt::RightDockWidgetArea);
        m_registerDock->setVisible(true);
    }
    if (m_stackDock) {
        addDockWidgetCompat(m_host, m_stackDock, Qt::RightDockWidgetArea);
        m_stackDock->setVisible(true);
        if (m_registerDock)
            tabifyDockWidgetCompat(m_host, m_registerDock, m_stackDock);
        if (m_registerDock) m_registerDock->raise();
    }

    /* Bottom area groups:
     * - Memory: Memory, Memory Visualizer, MMU Viewer, extra Memory views
     * - System: Port Monitor, Timer Monitor, LCD State, Cycle Counter
     * - Debug tools: Console, Breakpoints, Watchpoints, Key History
     */
    auto placeGroup = [this](DockWidget *anchor, const QList<DockWidget *> &tabs) -> DockWidget * {
        if (!anchor)
            return nullptr;
        addDockWidgetCompat(m_host, anchor, Qt::BottomDockWidgetArea);
        anchor->setVisible(true);
        for (DockWidget *dw : tabs) {
            if (!dw)
                continue;
            addDockWidgetCompat(m_host, dw, Qt::BottomDockWidgetArea, anchor);
            dw->setVisible(true);
            tabifyDockWidgetCompat(m_host, anchor, dw);
        }
        return anchor;
    };

    QList<DockWidget *> memoryTabs = { m_memVisDock, m_mmuViewerDock };
    for (DockWidget *extra : m_extraHexDocks)
        memoryTabs.append(extra);
    DockWidget *memoryRoot = placeGroup(m_hexDock, memoryTabs);
    DockWidget *systemRoot = placeGroup(m_portMonitorDock,
                                        { m_timerMonitorDock, m_lcdStateDock, m_cycleCounterDock });
    DockWidget *debugToolsRoot = placeGroup(m_consoleDock,
                                            { m_breakpointDock, m_watchpointDock, m_keyHistoryDock });

    if (memoryRoot && systemRoot)
        splitDockWidgetCompat(m_host, memoryRoot, systemRoot, Qt::Horizontal);
    if (systemRoot && debugToolsRoot)
        splitDockWidgetCompat(m_host, systemRoot, debugToolsRoot, Qt::Horizontal);
    else if (memoryRoot && debugToolsRoot)
        splitDockWidgetCompat(m_host, memoryRoot, debugToolsRoot, Qt::Horizontal);

    if (m_hexDock)
        m_hexDock->raise();

    /* Sizing hints */
    QList<DockWidget *> hTargets;
    QList<int> hSizes;
    if (m_disasmDock) { hTargets << m_disasmDock; hSizes << 400; }
    if (!hTargets.isEmpty())
        resizeDocksCompat(m_host, hTargets, hSizes, Qt::Horizontal);

    QList<DockWidget *> vTargets;
    QList<int> vSizes;
    if (memoryRoot) { vTargets << memoryRoot; vSizes << 200; }
    else if (systemRoot) { vTargets << systemRoot; vSizes << 200; }
    else if (debugToolsRoot) { vTargets << debugToolsRoot; vSizes << 200; }
    if (!vTargets.isEmpty())
        resizeDocksCompat(m_host, vTargets, vSizes, Qt::Vertical);

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
