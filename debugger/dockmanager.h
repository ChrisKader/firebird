#ifndef DEBUGDOCKMANAGER_H
#define DEBUGDOCKMANAGER_H

#include <QObject>
#include <QFont>
#include <QSet>
#include <QList>
#include <cstdint>

class QMainWindow;
class QMenu;
class DockWidget;
class DisassemblyWidget;
class RegisterWidget;
class HexViewWidget;
class BreakpointWidget;
class WatchpointWidget;
class PortMonitorWidget;
class StackWidget;
class KeyHistoryWidget;
class ConsoleWidget;
class MemoryVisualizerWidget;
class CycleCounterWidget;
class TimerMonitorWidget;
class LCDStateWidget;
class MMUViewerWidget;

class DebugDockManager : public QObject
{
    Q_OBJECT
public:
    explicit DebugDockManager(QMainWindow *host, const QFont &iconFont,
                              QObject *parent = nullptr);

    void createDocks(QMenu *docksMenu);
    void addHexViewDock();
    void resetLayout();
    void raise();
    void hideAutoShown();
    void refreshAll();
    void markDirty(uint32_t flags = 0xFFFFFFFFu);
    void refreshIcons();
    void retranslate();
    void setEditMode(bool enabled);
    void ensureExtraHexDocks(int count);
    int extraHexDockCount() const { return m_hexViewCount > 1 ? (m_hexViewCount - 1) : 0; }

    DisassemblyWidget *disassembly() const { return m_disasmWidget; }
    HexViewWidget *hexView() const { return m_hexWidget; }
    ConsoleWidget *console() const { return m_consoleWidget; }
    DockWidget *consoleDock() const { return m_consoleDock; }
    WatchpointWidget *watchpoints() const { return m_watchpointWidget; }

signals:
    void debugCommand(QString cmd);

private:
    enum DirtyFlags : uint32_t {
        DIRTY_DISASM  = 1u << 0,
        DIRTY_REGS    = 1u << 1,
        DIRTY_MEMORY  = 1u << 2,
        DIRTY_BREAKS  = 1u << 3,
        DIRTY_IO      = 1u << 4,
        DIRTY_STATS   = 1u << 5,
        DIRTY_STACK   = 1u << 6,
        DIRTY_ALL     = 0xFFFFFFFFu,
    };

    QMainWindow *m_host;
    QFont m_iconFont;

    DisassemblyWidget *m_disasmWidget = nullptr;
    RegisterWidget *m_registerWidget = nullptr;
    HexViewWidget *m_hexWidget = nullptr;
    BreakpointWidget *m_breakpointWidget = nullptr;
    WatchpointWidget *m_watchpointWidget = nullptr;
    PortMonitorWidget *m_portMonitorWidget = nullptr;
    StackWidget *m_stackWidget = nullptr;
    KeyHistoryWidget *m_keyHistoryWidget = nullptr;
    ConsoleWidget *m_consoleWidget = nullptr;
    MemoryVisualizerWidget *m_memVisWidget = nullptr;
    CycleCounterWidget *m_cycleCounterWidget = nullptr;
    TimerMonitorWidget *m_timerMonitorWidget = nullptr;
    LCDStateWidget *m_lcdStateWidget = nullptr;
    MMUViewerWidget *m_mmuViewerWidget = nullptr;

    DockWidget *m_disasmDock = nullptr;
    DockWidget *m_registerDock = nullptr;
    DockWidget *m_hexDock = nullptr;
    DockWidget *m_breakpointDock = nullptr;
    DockWidget *m_watchpointDock = nullptr;
    DockWidget *m_portMonitorDock = nullptr;
    DockWidget *m_stackDock = nullptr;
    DockWidget *m_keyHistoryDock = nullptr;
    DockWidget *m_consoleDock = nullptr;
    DockWidget *m_memVisDock = nullptr;
    DockWidget *m_cycleCounterDock = nullptr;
    DockWidget *m_timerMonitorDock = nullptr;
    DockWidget *m_lcdStateDock = nullptr;
    DockWidget *m_mmuViewerDock = nullptr;

    QSet<DockWidget *> m_autoShownDocks;
    QList<HexViewWidget *> m_extraHexWidgets;
    QList<DockWidget *> m_extraHexDocks;
    QMenu *m_docksMenu = nullptr;
    int m_hexViewCount = 1;
    uint32_t m_dirtyFlags = DIRTY_ALL;
};

#endif // DEBUGDOCKMANAGER_H
