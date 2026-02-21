#ifndef DOCKMANAGER_H
#define DOCKMANAGER_H

#include <QObject>
#include <QFont>
#include <QSet>
#include <QList>
#include <QHash>
#include <QVector>
#include <QJsonObject>
#include <QPointer>
#include <cstdint>

#include "ui/docking/manager/debugdockregistration.h"
#include "ui/docking/widgets/dockwidget.h"

class QMainWindow;
class QMenu;
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

class DockManager : public QObject
{
    Q_OBJECT
public:
    enum class MainDockId {
        Files = 0,
        Keypad = 1,
        NandBrowser = 2,
        HardwareConfig = 3,
        Screen = 4,
        Controls = 5,
        ExternalScreen = 6,
    };

    enum class DockFocusPolicy {
        Always = 0,
        ExplicitOnly = 1,
        Never = 2,
    };

    explicit DockManager(QMainWindow *host, const QFont &iconFont,
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
    QJsonObject serializeDockStates() const;
    void restoreDockStates(const QJsonObject &stateRoot);
    void setDockFocusPolicy(DockFocusPolicy policy);
    DockFocusPolicy dockFocusPolicy() const { return m_dockFocusPolicy; }
    void registerMainDock(MainDockId id, DockWidget *dock);
    DockWidget *mainDock(MainDockId id) const;

    DisassemblyWidget *disassembly() const;
    HexViewWidget *hexView() const;
    ConsoleWidget *console() const;
    DockWidget *consoleDock() const;
    WatchpointWidget *watchpoints() const;

signals:
    void debugCommand(QString cmd);

private:
    struct DebugDockRuntime {
        DebugDockRegistration registration;
        QPointer<QWidget> widget;
        QPointer<DockWidget> dock;
    };

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

    QPointer<QMainWindow> m_host;
    QFont m_iconFont;

    QPointer<DisassemblyWidget> m_disasmWidget;
    QPointer<RegisterWidget> m_registerWidget;
    QPointer<HexViewWidget> m_hexWidget;
    QPointer<BreakpointWidget> m_breakpointWidget;
    QPointer<WatchpointWidget> m_watchpointWidget;
    QPointer<PortMonitorWidget> m_portMonitorWidget;
    QPointer<StackWidget> m_stackWidget;
    QPointer<KeyHistoryWidget> m_keyHistoryWidget;
    QPointer<ConsoleWidget> m_consoleWidget;
    QPointer<MemoryVisualizerWidget> m_memVisWidget;
    QPointer<CycleCounterWidget> m_cycleCounterWidget;
    QPointer<TimerMonitorWidget> m_timerMonitorWidget;
    QPointer<LCDStateWidget> m_lcdStateWidget;
    QPointer<MMUViewerWidget> m_mmuViewerWidget;

    QPointer<DockWidget> m_disasmDock;
    QPointer<DockWidget> m_registerDock;
    QPointer<DockWidget> m_hexDock;
    QPointer<DockWidget> m_breakpointDock;
    QPointer<DockWidget> m_watchpointDock;
    QPointer<DockWidget> m_portMonitorDock;
    QPointer<DockWidget> m_stackDock;
    QPointer<DockWidget> m_keyHistoryDock;
    QPointer<DockWidget> m_consoleDock;
    QPointer<DockWidget> m_memVisDock;
    QPointer<DockWidget> m_cycleCounterDock;
    QPointer<DockWidget> m_timerMonitorDock;
    QPointer<DockWidget> m_lcdStateDock;
    QPointer<DockWidget> m_mmuViewerDock;

    QSet<DockWidget *> m_autoShownDocks;
    QVector<DebugDockRuntime> m_debugDocks;
    QList<QPointer<HexViewWidget>> m_extraHexWidgets;
    QList<QPointer<DockWidget>> m_extraHexDocks;
    QPointer<QMenu> m_docksMenu;
    int m_hexViewCount = 1;
    uint32_t m_dirtyFlags = DIRTY_ALL;
    DockFocusPolicy m_dockFocusPolicy = DockFocusPolicy::Always;
    QHash<int, QPointer<DockWidget>> m_mainDocks;

    void bindRegistration(DebugDockKind kind, QWidget *widget, DockWidget *dock);
    void showDock(DockWidget *dock, bool explicitUserAction);
};

#endif // DOCKMANAGER_H
