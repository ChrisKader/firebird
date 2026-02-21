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

namespace {

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

void DockManager::bindRegistration(DebugDockKind kind, QWidget *widget, DockWidget *dock)
{
    switch (kind) {
    case DebugDockKind::Disasm:
        m_disasmWidget = qobject_cast<DisassemblyWidget *>(widget);
        m_disasmDock = dock;
        break;
    case DebugDockKind::Registers:
        m_registerWidget = qobject_cast<RegisterWidget *>(widget);
        m_registerDock = dock;
        break;
    case DebugDockKind::Stack:
        m_stackWidget = qobject_cast<StackWidget *>(widget);
        m_stackDock = dock;
        break;
    case DebugDockKind::Memory:
        m_hexWidget = qobject_cast<HexViewWidget *>(widget);
        m_hexDock = dock;
        break;
    case DebugDockKind::Breakpoints:
        m_breakpointWidget = qobject_cast<BreakpointWidget *>(widget);
        m_breakpointDock = dock;
        break;
    case DebugDockKind::Watchpoints:
        m_watchpointWidget = qobject_cast<WatchpointWidget *>(widget);
        m_watchpointDock = dock;
        break;
    case DebugDockKind::PortMonitor:
        m_portMonitorWidget = qobject_cast<PortMonitorWidget *>(widget);
        m_portMonitorDock = dock;
        break;
    case DebugDockKind::KeyHistory:
        m_keyHistoryWidget = qobject_cast<KeyHistoryWidget *>(widget);
        m_keyHistoryDock = dock;
        break;
    case DebugDockKind::Console:
        m_consoleWidget = qobject_cast<ConsoleWidget *>(widget);
        m_consoleDock = dock;
        break;
    case DebugDockKind::MemVis:
        m_memVisWidget = qobject_cast<MemoryVisualizerWidget *>(widget);
        m_memVisDock = dock;
        break;
    case DebugDockKind::CycleCounter:
        m_cycleCounterWidget = qobject_cast<CycleCounterWidget *>(widget);
        m_cycleCounterDock = dock;
        break;
    case DebugDockKind::TimerMonitor:
        m_timerMonitorWidget = qobject_cast<TimerMonitorWidget *>(widget);
        m_timerMonitorDock = dock;
        break;
    case DebugDockKind::LCDState:
        m_lcdStateWidget = qobject_cast<LCDStateWidget *>(widget);
        m_lcdStateDock = dock;
        break;
    case DebugDockKind::MMUViewer:
        m_mmuViewerWidget = qobject_cast<MMUViewerWidget *>(widget);
        m_mmuViewerDock = dock;
        break;
    }
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
    auto makeDock = [&](const DebugDockRegistration &registration,
                        QWidget *widget) -> DockWidget * {
        auto *dw = new KDockWidget(registration.objectName, tr(registration.titleKey), m_host);
        dw->applyThinTitlebar(true);
        dw->setWidget(widget);
        applyStandardDockFeatures(dw, !registration.coreDock);
        DockBackend::addDockWidgetCompat(m_host, dw, registration.defaultArea, nullptr, !registration.coreDock);
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
        if (!registration.coreDock)
            dw->hide();
#endif

        QAction *action = dw->toggleViewAction();
        docksMenu->addAction(action);

        return dw;
    };

    /* Create docks */
    m_debugDocks.clear();
    docksMenu->addSeparator();
    const QList<DebugDockRegistration> registrations = buildDebugDockRegistrations();
    m_debugDocks.reserve(registrations.size());
    for (const DebugDockRegistration &registration : registrations) {
        if (!registration.createWidget)
            continue;
        QWidget *widget = registration.createWidget(m_host);
        if (registration.initializeWidget)
            registration.initializeWidget(widget, m_iconFont);

        DockWidget *dock = makeDock(registration, widget);
        m_debugDocks.push_back(DebugDockRuntime{registration, widget, dock});
        bindRegistration(registration.kind, widget, dock);
    }

    auto tabifyIfPresent = [this](DockWidget *first, DockWidget *second) {
        if (first && second)
            DockBackend::tabifyDockWidgetCompat(m_host, first, second);
    };

    /* Tab Registers and Stack together */
    tabifyIfPresent(m_registerDock, m_stackDock);
    if (m_registerDock)
        m_registerDock->raise();

    /* Set Material icons on toggle actions */
    refreshIcons();

    /* Tab together: Memory, Breakpoints, Watchpoints, Port Monitor, + new docks */
    tabifyIfPresent(m_hexDock, m_breakpointDock);
    tabifyIfPresent(m_breakpointDock, m_watchpointDock);
    tabifyIfPresent(m_watchpointDock, m_portMonitorDock);
    tabifyIfPresent(m_portMonitorDock, m_keyHistoryDock);
    tabifyIfPresent(m_keyHistoryDock, m_consoleDock);
    tabifyIfPresent(m_consoleDock, m_memVisDock);
    tabifyIfPresent(m_memVisDock, m_cycleCounterDock);
    tabifyIfPresent(m_cycleCounterDock, m_timerMonitorDock);
    tabifyIfPresent(m_timerMonitorDock, m_lcdStateDock);
    tabifyIfPresent(m_lcdStateDock, m_mmuViewerDock);
    if (m_hexDock)
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
    QList<DebugDockRuntime> rightDocks;
    QList<DebugDockRuntime> memoryDocks;
    QList<DebugDockRuntime> toolsDocks;

    for (const DebugDockRuntime &runtime : m_debugDocks) {
        DockWidget *dock = runtime.dock.data();
        if (!dock)
            continue;

        DockBackend::removeDockWidgetCompat(m_host, dock);

        switch (runtime.registration.resetGroup) {
        case DebugDockGroup::Right:
            rightDocks.append(runtime);
            break;
        case DebugDockGroup::BottomMemory:
            memoryDocks.append(runtime);
            break;
        case DebugDockGroup::BottomTools:
            toolsDocks.append(runtime);
            break;
        }
    }

    DockWidget *lastRightDock = nullptr;
    for (const DebugDockRuntime &runtime : rightDocks) {
        DockWidget *dock = runtime.dock.data();
        if (!dock)
            continue;
        DockBackend::addDockWidgetCompat(m_host, dock, Qt::RightDockWidgetArea);
        dock->setVisible(true);
        if (runtime.registration.tabWithPreviousInReset && lastRightDock)
            DockBackend::tabifyDockWidgetCompat(m_host, lastRightDock, dock);
        lastRightDock = dock;
    }
    if (m_registerDock)
        m_registerDock->raise();

    auto placeGroup = [this](const QList<DebugDockRuntime> &entries) -> DockWidget * {
        DockWidget *anchor = nullptr;
        for (const DebugDockRuntime &entry : entries) {
            DockWidget *dock = entry.dock.data();
            if (!dock)
                continue;
            if (!anchor) {
                anchor = dock;
                DockBackend::addDockWidgetCompat(m_host, anchor, Qt::BottomDockWidgetArea);
                anchor->setVisible(true);
                continue;
            }
            DockBackend::addDockWidgetCompat(m_host, dock, Qt::BottomDockWidgetArea, anchor);
            dock->setVisible(true);
            DockBackend::tabifyDockWidgetCompat(m_host, anchor, dock);
        }
        return anchor;
    };

    DockWidget *memoryRoot = placeGroup(memoryDocks);
    for (DockWidget *extra : m_extraHexDocks) {
        if (!extra || !memoryRoot)
            continue;
        DockBackend::addDockWidgetCompat(m_host, extra, Qt::BottomDockWidgetArea, memoryRoot);
        extra->setVisible(true);
        DockBackend::tabifyDockWidgetCompat(m_host, memoryRoot, extra);
    }
    DockWidget *debugToolsRoot = placeGroup(toolsDocks);

    if (memoryRoot && debugToolsRoot)
        DockBackend::splitDockWidgetCompat(m_host, memoryRoot, debugToolsRoot, Qt::Horizontal);

    if (m_hexDock)
        m_hexDock->raise();

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

    for (const DebugDockRuntime &runtime : m_debugDocks) {
        DockWidget *dock = runtime.dock.data();
        if (!dock)
            continue;
        dock->setVisible(runtime.registration.visibleByDefault);
    }
    for (DockWidget *extra : m_extraHexDocks) {
        if (extra)
            extra->setVisible(false);
    }
}
