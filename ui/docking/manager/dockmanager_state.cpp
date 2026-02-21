#include "ui/docking/manager/dockmanager.h"

#include <QJsonArray>
#include <QMainWindow>
#include <QTimer>

#include "ui/docking/state/dockstate.h"
#include "ui/theme/materialicons.h"
#include "ui/widgets/disassembly/disassemblywidget.h"
#include "ui/widgets/registers/registerwidget.h"
#include "ui/widgets/hexview/hexviewwidget.h"
#include "ui/widgets/breakpoints/breakpointwidget.h"
#include "ui/widgets/watchpoints/watchpointwidget.h"
#include "ui/widgets/portmonitor/portmonitorwidget.h"
#include "ui/widgets/stack/stackwidget.h"
#include "ui/widgets/console/consolewidget.h"
#include "ui/widgets/memvisualizer/memoryvisualizerwidget.h"
#include "ui/widgets/cyclecounter/cyclecounterwidget.h"
#include "ui/widgets/timermonitor/timermonitorwidget.h"
#include "ui/widgets/lcdstate/lcdstatewidget.h"
#include "ui/widgets/mmuviewer/mmuviewerwidget.h"

void DockManager::ensureExtraHexDocks(int count)
{
    if (count <= 0)
        return;
    while (extraHexDockCount() < count)
        addHexViewDock();
}

QJsonObject DockManager::serializeDockStates() const
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

void DockManager::restoreDockStates(const QJsonObject &stateRoot)
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

void DockManager::refreshIcons()
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

void DockManager::markDirty(uint32_t flags)
{
    m_dirtyFlags |= flags;
}

void DockManager::refreshAll()
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

void DockManager::retranslate()
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

void DockManager::raise()
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

void DockManager::hideAutoShown()
{
    for (DockWidget *dock : m_autoShownDocks)
    {
        if (dock && dock->isVisible())
            dock->setVisible(false);
    }
    m_autoShownDocks.clear();
}

void DockManager::setEditMode(bool enabled)
{
    const auto dockChildren = m_host->findChildren<DockWidget *>();
    for (DockWidget *dw : dockChildren)
        dw->hideTitlebar(!enabled);
}
