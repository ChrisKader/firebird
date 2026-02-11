#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QLabel>
#include <QQuickWidget>
#include <QStringList>
#include <functional>
#include <memory>

#include "app/emuthread.h"
#include "dialogs/fbaboutdialog.h"
#include "ui/lcdwidget.h"
#include "ui/dockwidget.h"
#include "app/qmlbridge.h"

class DebugDockManager;
class NandBrowserWidget;
class HwConfigWidget;

namespace Ui {
class MainWindow;

}

/* QQuickWidget does not care about QEvent::Leave,
 * which results in MouseArea::containsMouse to get stuck when
 * the mouse leaves the widget without triggering a move outside
 * the MouseArea. Work around it by translating QEvent::Leave
 * to a MouseMove to (0/0). */
class QResizeEvent;
class QToolButton;
class QTableWidget;
class QMenu;
class QIcon;
class QAction;
class QTimer;
class QQuickWidgetLessBroken : public QQuickWidget
{
    Q_OBJECT

public:
    explicit QQuickWidgetLessBroken(QWidget *parent) : QQuickWidget(parent) {}
    virtual ~QQuickWidgetLessBroken() {}

protected:
    bool event(QEvent *event) override;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QMLBridge *qmlBridgeDep, EmuThread *emuThreadDep, QWidget *parent = 0);
    ~MainWindow();

public slots:
    //Miscellaneous
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent *e) override;
    void showStatusMsg(QString str);
    void kitDataChanged(QModelIndex, QModelIndex, QVector<int> roles);
    void kitAnythingChanged();
    void currentKitChanged(const Kit &kit);

    //Drag & Drop
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    //Menu "Emulator"
    void restart();
    void openConfiguration();
    void startKit();
    void startKitDiags();

    //Menu "Tools"
    void screenshot();
    void screenshotToFile();
    void recordGIF();
    void connectUSB();
    void usblinkChanged(bool state);
    void setExtLCD(bool state);
    void xmodemSend();
    void switchToMobileUI();
    void launchIdaInstantDebugging();
    void toggleFullscreen();
    void toggleAlwaysOnTop(bool checked);
    void toggleFocusPause(bool checked);

    //Menu "State"
    bool resume();
    void suspend();
    void resumeFromFile();
    void suspendToFile();
    void saveStateSlot(int slot);
    void loadStateSlot(int slot);

    //Menu "Flash"
    void saveFlash();
    void createFlash();

    //Menu "Docks"
    void setUIEditMode(bool e);
    void resetDockLayout();

    //Menu "About"
    void showAbout();

    //State
    void isBusy(bool busy);
    void started(bool success);
    void resumed(bool success);
    void suspended(bool success);
    void stopped();

    //Serial (forwarded to Console dock)
    void serialChar(const char c);

    //Debugging
    void debugInputRequested(bool b);
    void debuggerEntered(bool entered);
    void debugStr(QString str);
    void nlogStr(QString str);

    //File transfer
    void changeProgress(int value);
    void usblinkDownload(int progress);
    void usblinkProgress(int progress);

    //Tool bar (above screen)
    void showSpeed(double value);

signals:
    void debuggerCommand(QString input);
    void usblink_progress_changed(int progress);

public:
    static void usblink_progress_callback(int progress, void *data);
    void switchUIMode(bool mobile_ui);

private:
    QMLBridge *qmlBridge() const { return m_qmlBridge.data(); }
    EmuThread &emuThread() const;

    void setActive(bool b);

    void suspendToPath(QString path);
    bool resumeFromPath(QString path);

    void convertTabsToDocks();
    void retranslateDocks();

    void updateUIActionState(bool emulation_running);

    void refillKitMenus();
    void updateWindowTitle();
    void savePersistentUiState();
    void scheduleLayoutHistoryCapture();
    void captureLayoutHistorySnapshot();
    void updateLayoutHistoryActions();
    void undoLayoutChange();
    void redoLayoutChange();

    // This changes the current GUI language to the one given in parameter, if available.
    // The change is persistent (saved in settings) if it was successful.
    void switchTranslator(const QLocale &locale);

    // QMLBridge is used as settings storage,
    // so the settings have to be read from there
    // and EmuThread configured appropriately.
    void applyQMLBridgeSettings();
    void setDebuggerActive(bool active);
    void applyWidgetTheme();
    void applyStandardDockFeatures(DockWidget *dw) const;
    DockWidget *createMainDock(const QString &title,
                               QWidget *widget,
                               const QString &objectName,
                               Qt::DockWidgetArea area,
                               QMenu *docksMenu = nullptr,
                               const QIcon &icon = QIcon(),
                               bool hideTitlebar = true);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    Ui::MainWindow *ui = nullptr;
    QMainWindow *content_window = nullptr;
    QPointer<QMLBridge> m_qmlBridge;
    EmuThread *m_emuThread = nullptr;

    QTranslator appTranslator;

    // Used to show a status message permanently
    QLabel status_label;
    QLabel *status_bar_speed_label = nullptr;
    QLabel *status_bar_debug_label = nullptr;
    QWidget *status_bar_tray = nullptr;
    QToolButton *status_dark_button = nullptr;

    QSettings *settings = nullptr;


    // Second LCDWidget shown in an optional floating dock
    LCDWidget lcd{this};

    // The about dialog
    FBAboutDialog aboutDialog{this};

    // The QML engine shared by the keypad, config dialog and mobile UI
    QQmlEngine *qml_engine = nullptr;
    QFont material_icon_font;

    // The config dialog component, used to create the config_dialog
    QQmlComponent *config_component = nullptr;
    // The QML Config Dialog
    QObject *config_dialog = nullptr;

    // The flash dialog component, used to create the flash_dialog
    QQmlComponent *flash_dialog_component = nullptr;
    // The QML Config Dialog
    QObject *flash_dialog = nullptr;

    // The Mobile UI component, used to create the mobile_dialog
    QQmlComponent *mobileui_component = nullptr;
    // A Mobile UI instance
    QObject *mobileui_dialog = nullptr;

    // Used for autosuspend on close.
    // The close event has to be deferred until the suspend operation completed successfully.
    bool close_after_suspend = false;

    // Whether this MainWindow is in charge of communicating with EmuThread
    bool is_active = false;

    // Debugger toggle state/button
    bool debugger_active = false;
    QToolButton *debugger_toggle_button = nullptr;
    std::function<void()> updatePlayPauseButtonFn;

    // Debug subsystem (owns all 12 debug dock widgets)
    std::unique_ptr<DebugDockManager> m_debugDocks;

    // Serial line buffer for forwarding to Console dock
    QString m_serialLineBuf;
    bool m_serialPendingCR = false;

    // Window management
    bool focus_pause_enabled = false;
    bool focus_auto_paused = false;

    // Utility docks converted from legacy tabs
    DockWidget *m_dock_files = nullptr;
    DockWidget *m_dock_keypad = nullptr;
    DockWidget *m_dock_nand = nullptr;
    DockWidget *m_dock_hwconfig = nullptr;

    // LCD and Controls docks (extracted from ui->frame)
    DockWidget *m_dock_lcd = nullptr;
    DockWidget *m_dock_controls = nullptr;
    DockWidget *m_dock_ext_lcd = nullptr;

    // NAND browser & HW config widgets
    NandBrowserWidget *m_nandBrowser = nullptr;
    HwConfigWidget *m_hwConfig = nullptr;

    // LCD/Keypad link state
    bool m_lcdKeypadLinked = false;

    // Layout undo/redo history
    QAction *m_undoLayoutAction = nullptr;
    QAction *m_redoLayoutAction = nullptr;
    QTimer *m_layoutHistoryTimer = nullptr;
    QList<QByteArray> m_layoutUndoHistory;
    QList<QByteArray> m_layoutRedoHistory;
    bool m_layoutHistoryApplying = false;

};

#endif // MAINWINDOW_H
