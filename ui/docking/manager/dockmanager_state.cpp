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

namespace {

ushort iconCodepoint(DebugDockIcon icon)
{
    using namespace MaterialIcons;
    switch (icon) {
    case DebugDockIcon::Code:         return CP::Code;
    case DebugDockIcon::List:         return CP::List;
    case DebugDockIcon::ViewColumn:   return CP::ViewColumn;
    case DebugDockIcon::Memory:       return CP::Memory;
    case DebugDockIcon::Bookmark:     return CP::Bookmark;
    case DebugDockIcon::Visibility:   return CP::Visibility;
    case DebugDockIcon::Monitor:      return CP::Monitor;
    case DebugDockIcon::History:      return CP::History;
    case DebugDockIcon::Terminal:     return CP::Terminal;
    case DebugDockIcon::GridOn:       return CP::GridOn;
    case DebugDockIcon::CycleCounter: return CP::CycleCounter;
    case DebugDockIcon::Timer:        return CP::Timer;
    case DebugDockIcon::Display:      return CP::Display;
    case DebugDockIcon::Layers:       return CP::Layers;
    case DebugDockIcon::None:         return 0;
    }
    return 0;
}

} // namespace

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

    for (const DebugDockRuntime &runtime : m_debugDocks)
        appendState(runtime.dock.data());
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
    const QColor fg = m_host->palette().color(QPalette::WindowText);
    auto setIcon = [&](DockWidget *dock, ushort cp) {
        if (dock && cp != 0)
            dock->toggleViewAction()->setIcon(MaterialIcons::fromCodepoint(m_iconFont, cp, fg));
    };
    for (const DebugDockRuntime &runtime : m_debugDocks)
        setIcon(runtime.dock.data(), iconCodepoint(runtime.registration.icon));
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
    for (const DebugDockRuntime &runtime : m_debugDocks) {
        DockWidget *dock = runtime.dock.data();
        if (!dock)
            continue;
        dock->setWindowTitle(tr(runtime.registration.titleKey));
    }
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
