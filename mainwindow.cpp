#include <QFileDialog>
#include <QStandardPaths>
#include <QTextBlock>
#include <QMessageBox>
#include <QGraphicsItem>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFrame>
#include <QMenuBar>
#include <QLabel>
#include <QActionGroup>
#include <QMimeData>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QShortcut>
#include <QTimer>
#include <QKeySequence>
#include <QProcess>
#include <QQmlComponent>
#include <QToolBar>
#include <QWidgetAction>
#include <QIcon>
#include <QToolButton>
#include <QRegularExpression>
#include <QHeaderView>
#include <QColor>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QApplication>
#include <QPalette>
#include <QAbstractItemView>
#include <QEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QUrl>
#include <QFontDatabase>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDateTime>
#include <QAbstractButton>

#include <array>
#include <climits>
#include <utility>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/MainWindow.h>
    #include <kddockwidgets/DockWidget.h>
    #include <kddockwidgets/KDDockWidgets.h>
    #include <kddockwidgets/LayoutSaver.h>
    #include <kddockwidgets/qtcommon/View.h>
    #include <kddockwidgets/core/DockWidget.h>
    #include <kddockwidgets/core/TitleBar.h>
    #include <kddockwidgets/core/View.h>
#endif

#include "core/debug.h"
#include "core/debug_api.h"
#include "core/emu.h"
#include "core/flash.h"
#include "core/gif.h"
#include "core/misc.h"
#include "core/mem.h"
#include "core/usblink_queue.h"

#include "ui/dockwidget.h"
#include "ui/kdockwidget.h"
#include "mainwindow.h"
#include "mainwindow/layout_persistence.h"
#include "ui/widgettheme.h"
#include "ui/materialicons.h"
#include "ui_mainwindow.h"
#include "app/qmlbridge.h"
#include "app/powercontrol.h"
#include "app/baselinelayout.h"
#include "ui/framebuffer.h"
#include "ui/keypadbridge.h"
#include "debugger/dockmanager.h"

#include "debugger/disassembly/disassemblywidget.h"
#include "debugger/hexview/hexviewwidget.h"
#include "debugger/console/consolewidget.h"
#include "debugger/nandbrowser/nandbrowserwidget.h"
#include "debugger/hwconfig/hwconfigwidget.h"

enum class MainDockId {
    LCD,
    ExternalLCD,
    Controls,
    NandBrowser,
    HwConfig,
};

static const char *mainDockObjectName(MainDockId id)
{
    switch (id) {
    case MainDockId::LCD:         return "dockLCD";
    case MainDockId::ExternalLCD: return "dockExternalLCD";
    case MainDockId::Controls:    return "dockControls";
    case MainDockId::NandBrowser: return "dockNandBrowser";
    case MainDockId::HwConfig:    return "dockHwConfig";
    }
    return "dockUnknown";
}

static constexpr const char *kSettingHwBatteryOverride = "hwBatteryOverride";
static constexpr const char *kSettingHwChargingOverride = "hwChargingOverride";
static constexpr const char *kSettingHwBrightnessOverride = "hwBrightnessOverride";
static constexpr const char *kSettingHwKeypadTypeOverride = "hwKeypadTypeOverride";
static constexpr const char *kSettingHwBatteryMvOverride = "hwBatteryMvOverride";
static constexpr const char *kSettingHwChargerStateOverride = "hwChargerStateOverride";
static constexpr const char *kSettingHwBatteryPresentOverride = "hwBatteryPresentOverride";
static constexpr const char *kSettingHwUsbCableConnectedOverride = "hwUsbCableConnectedOverride";
static constexpr const char *kSettingHwUsbOtgOverride = "hwUsbOtgOverride";
static constexpr const char *kSettingHwDockAttachedOverride = "hwDockAttachedOverride";
static constexpr const char *kSettingHwVbusMvOverride = "hwVbusMvOverride";
static constexpr const char *kSettingHwVsledMvOverride = "hwVsledMvOverride";
static constexpr const char *kSettingWindowLayoutJson = "windowLayoutJson";
static constexpr const char *kSettingDockLayoutJson = "dockLayoutJson";
static constexpr const char *kSettingLayoutProfile = "layoutProfile";
static constexpr const char *kSettingDebugDockStateJson = "debugDockStateJson";
static constexpr const char *kSettingDockFocusPolicy = "dockFocusPolicy";
static constexpr int kMaxLayoutHistoryEntries = 10;

struct HwOverrides {
    int batteryRaw = -1;
    int charging = -1;
    int brightness = -1;
    int keypadType = -1;
    int batteryMv = -1;
    int chargerState = -1;
    int batteryPresent = -1;
    int usbCableConnected = -1;
    int usbOtgCable = -1;
    int dockAttached = -1;
    int vbusMv = -1;
    int vsledMv = -1;
};

static HwOverrides readHwOverridesFromSettings(QSettings *settings)
{
    HwOverrides overrides;
    if (!settings)
        return overrides;
    const auto readValue = [settings](const char *key) {
        return settings->value(QString::fromLatin1(key), -1).toInt();
    };
    overrides.batteryRaw = readValue(kSettingHwBatteryOverride);
    overrides.charging = readValue(kSettingHwChargingOverride);
    overrides.brightness = readValue(kSettingHwBrightnessOverride);
    overrides.keypadType = readValue(kSettingHwKeypadTypeOverride);
    overrides.batteryMv = readValue(kSettingHwBatteryMvOverride);
    overrides.chargerState = readValue(kSettingHwChargerStateOverride);
    overrides.batteryPresent = readValue(kSettingHwBatteryPresentOverride);
    overrides.usbCableConnected = readValue(kSettingHwUsbCableConnectedOverride);
    overrides.usbOtgCable = readValue(kSettingHwUsbOtgOverride);
    overrides.dockAttached = readValue(kSettingHwDockAttachedOverride);
    overrides.vbusMv = readValue(kSettingHwVbusMvOverride);
    overrides.vsledMv = readValue(kSettingHwVsledMvOverride);
    return overrides;
}

static void writeHwOverridesToSettings(QSettings *settings, const HwOverrides &overrides)
{
    if (!settings)
        return;
    const std::array<std::pair<const char *, int>, 12> values = {{
        { kSettingHwBatteryOverride, overrides.batteryRaw },
        { kSettingHwChargingOverride, overrides.charging },
        { kSettingHwBrightnessOverride, overrides.brightness },
        { kSettingHwKeypadTypeOverride, overrides.keypadType },
        { kSettingHwBatteryMvOverride, overrides.batteryMv },
        { kSettingHwChargerStateOverride, overrides.chargerState },
        { kSettingHwBatteryPresentOverride, overrides.batteryPresent },
        { kSettingHwUsbCableConnectedOverride, overrides.usbCableConnected },
        { kSettingHwUsbOtgOverride, overrides.usbOtgCable },
        { kSettingHwDockAttachedOverride, overrides.dockAttached },
        { kSettingHwVbusMvOverride, overrides.vbusMv },
        { kSettingHwVsledMvOverride, overrides.vsledMv },
    }};
    for (const auto &entry : values)
        settings->setValue(QString::fromLatin1(entry.first), entry.second);
}

static bool likelyCx2StartupKit(QMLBridge *bridge)
{
    if (!bridge)
        return false;

    KitModel *model = bridge->getKitModel();
    if (!model || model->rowCount() <= 0)
        return false;

    int kitId = bridge->getCurrentKitId();
    if (kitId < 0)
        kitId = static_cast<int>(bridge->getDefaultKit());

    int row = bridge->kitIndexForID(static_cast<unsigned int>(kitId));
    if (row < 0)
        row = 0;

    const QString type = model->getDataRow(row, KitModel::TypeRole).toString();
    return type.contains(QStringLiteral("CX II"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("CX2"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("CX 2"), Qt::CaseInsensitive);
}

/* WidgetTheme, applyPaletteColors, setWidgetBackground now in widgettheme.h/cpp */

void MainWindow::restoreStartupLayoutFromSettings()
{
    /* Dock/window initialization order is significant:
     * 1) create all main/debug docks (including dynamic extra hex docks),
     * 2) restore geometry/dock layout against those concrete dock objects,
     * 3) apply post-restore links/theme behavior.
     * Reordering these steps can silently break layout restoration. */
    convertTabsToDocks();
    if (m_debugDocks) {
        const int extraHexDocks = qMax(0, settings->value(QStringLiteral("debugExtraHexDockCount"), 0).toInt());
        m_debugDocks->ensureExtraHexDocks(extraHexDocks);
    }
    retranslateDocks();
    if (m_dock_ext_lcd)
        m_dock_ext_lcd->restoreGeometry(settings->value(QStringLiteral("extLCDGeometry")).toByteArray());
    setExtLCD(settings->value(QStringLiteral("extLCDVisible")).toBool());
    restoreGeometry(settings->value(QStringLiteral("windowGeometry")).toByteArray());

    // Restore dock layout from named profiles; if unavailable, use built-in C++ default baseline.
    bool restored = false;
    bool usedBuiltInDefaultBaseline = false;
    QString appliedStartupProfile;
    const QString startupProfile = settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed();
    QJsonObject restoredDebugDockState;
    QJsonObject restoredCoreDockConnections;
    const QString autoProfile = startupProfile.isEmpty() ? QStringLiteral("default") : startupProfile;
    if (!autoProfile.isEmpty()) {
        QString profileError;
        if (restoreLayoutProfile(content_window,
                                 autoProfile,
                                 &profileError,
                                 &restoredDebugDockState,
                                 &restoredCoreDockConnections)) {
            restored = true;
        } else {
            qDebug("profile restore failed (%s): %s",
                   autoProfile.toUtf8().constData(),
                   profileError.toUtf8().constData());
        }
    }
    if (!restored) {
        qDebug("profile unavailable or invalid -- applying built-in default layout");
        resetDockLayout();
        usedBuiltInDefaultBaseline = true;
        restored = true;
        appliedStartupProfile = QStringLiteral("default");
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), appliedStartupProfile);
    } else {
        appliedStartupProfile = autoProfile;
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), appliedStartupProfile);
    }

    if (m_debugDocks) {
        if (!restoredDebugDockState.isEmpty()) {
            m_debugDocks->restoreDockStates(restoredDebugDockState);
        } else if (!usedBuiltInDefaultBaseline) {
            QJsonParseError debugStateErr = {};
            const QString savedDebugState = settings->value(QString::fromLatin1(kSettingDebugDockStateJson)).toString();
            const QJsonDocument debugStateDoc = QJsonDocument::fromJson(savedDebugState.toUtf8(), &debugStateErr);
            if (debugStateErr.error == QJsonParseError::NoError && debugStateDoc.isObject())
                m_debugDocks->restoreDockStates(debugStateDoc.object());
        }
    }

    restoreCoreDockConnections(restoredCoreDockConnections);

    m_layoutUndoHistory.clear();
    m_layoutRedoHistory.clear();
    captureLayoutHistorySnapshot();

    // KDD layout restore can run before final dock geometry settles at startup.
    // Re-apply the selected profile once on the next event loop tick so relaunch
    // matches explicit "Load layout profile" behavior.
    if (!appliedStartupProfile.isEmpty()) {
        QTimer::singleShot(0, this, [this, appliedStartupProfile]() {
            QString profileError;
            QJsonObject debugDockState;
            QJsonObject coreDockConnections;
            if (restoreLayoutProfile(content_window,
                                     appliedStartupProfile,
                                     &profileError,
                                     &debugDockState,
                                     &coreDockConnections)) {
                if (m_debugDocks && !debugDockState.isEmpty())
                    m_debugDocks->restoreDockStates(debugDockState);
                restoreCoreDockConnections(coreDockConnections);
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                captureLayoutHistorySnapshot();
                return;
            }

            if (appliedStartupProfile == QLatin1String("default")) {
                resetDockLayout();
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                captureLayoutHistorySnapshot();
                return;
            }

            qDebug("deferred profile restore failed (%s): %s",
                   appliedStartupProfile.toUtf8().constData(),
                   profileError.toUtf8().constData());
        });
    }

    m_lcdKeypadLinked = settings->value(QStringLiteral("lcdKeypadLinked"), false).toBool();
}

void MainWindow::restoreHardwareOverridesFromSettings()
{
    // Battery/charger overrides are coupled: never restore a forced charger state
    // unless battery override itself is active.
    const HwOverrides hw = readHwOverridesFromSettings(settings);
    auto clampTriState = [](int value) -> int8_t {
        if (value < 0)
            return -1;
        return value ? 1 : 0;
    };
    const bool forceCx2SafeBaseline = emulate_cx2 || likelyCx2StartupKit(qmlBridge());
    const int8_t batteryPresent = forceCx2SafeBaseline ? 1 : clampTriState(hw.batteryPresent);
    const int8_t usbCable = forceCx2SafeBaseline ? 0 : clampTriState(hw.usbCableConnected);
    const int8_t usbOtg = forceCx2SafeBaseline ? 0 : clampTriState(hw.usbOtgCable);
    const int8_t dockAttached = forceCx2SafeBaseline ? 0 : clampTriState(hw.dockAttached);

    hw_override_set_battery_present(batteryPresent);
    hw_override_set_usb_cable_connected(usbCable);
    hw_override_set_usb_otg_cable(usbOtg);
    hw_override_set_dock_attached(dockAttached);

    int restoredBatteryMv = forceCx2SafeBaseline ? -1 : hw.batteryMv;
    /* Old settings may contain non-mV payloads (for example bool/int flags)
     * in the battery-mV key. Reject out-of-range values so CX II falls back
     * to the model default instead of clamping to a fake 3000mV low battery. */
    if (restoredBatteryMv >= 0
            && (restoredBatteryMv < 3000 || restoredBatteryMv > 4200))
        restoredBatteryMv = -1;

    int restoredVbusMv = forceCx2SafeBaseline ? 0 : hw.vbusMv;
    int restoredVsledMv = forceCx2SafeBaseline ? 0 : hw.vsledMv;
    if (restoredVbusMv > 5500)
        restoredVbusMv = 5500;
    if (restoredVsledMv > 5500)
        restoredVsledMv = 5500;
    /* Normalize persisted rail overrides so "disconnected" truly means no
     * external power at boot. This avoids stale non-zero rail values causing
     * charging state without any cable/dock attachment. */
    if (usbOtg > 0 || usbCable <= 0)
        restoredVbusMv = 0;
    if (dockAttached <= 0)
        restoredVsledMv = 0;
    else if (restoredVsledMv < 0)
        restoredVsledMv = 0;
    hw_override_set_vbus_mv(restoredVbusMv);
    hw_override_set_vsled_mv(restoredVsledMv);
    const bool batteryOverrideActive = (restoredBatteryMv >= 0);
    if (batteryOverrideActive) {
        /* Legacy raw battery override is ignored for CX II power model. */
        hw_override_set_adc_battery_level(-1);
        hw_override_set_battery_mv(restoredBatteryMv);
        /* Never restore legacy forced charging state on CX II.
         * Charging status should come from physical rails/events only. */
        hw_override_set_adc_charging(-1);
        hw_override_set_charger_state(CHARGER_AUTO);
    } else {
        hw_override_set_adc_battery_level(-1);
        hw_override_set_battery_mv(-1);
        hw_override_set_adc_charging(-1);
        hw_override_set_charger_state(CHARGER_AUTO);
    }
    hw_override_set_lcd_contrast(static_cast<int16_t>(hw.brightness));
    hw_override_set_adc_keypad_type(static_cast<int16_t>(hw.keypadType));
    PowerControl::refreshPowerState();
    if (m_hwConfig)
        m_hwConfig->syncOverridesFromGlobals();
}

MainWindow::~MainWindow()
{
    /* config_dialog and flash_dialog are created by QQmlComponent::create()
     * without a parent, so they must be explicitly deleted.  Use delete
     * instead of deleteLater -- the event loop may not process deferred
     * deletes before the QML engine (a child of this window) is destroyed,
     * which would cause a use-after-free.
     *
     * mobileui_component and the other QQmlComponents are children of this
     * window and will be auto-deleted by ~QWidget(), so we must NOT delete
     * them here (that would double-free). */
    delete mobileui_dialog;
    mobileui_dialog = nullptr;

    delete config_dialog;
    config_dialog = nullptr;

    delete flash_dialog;
    flash_dialog = nullptr;

    savePersistentUiState();
    delete settings;
    delete ui;
}

void MainWindow::savePersistentUiState()
{
    if (!settings || m_persistentUiStateSaved)
        return;

    // Save external LCD dock geometry
    settings->setValue(QStringLiteral("extLCDGeometry"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->saveGeometry() : QByteArray());
    settings->setValue(QStringLiteral("extLCDVisible"),
                       m_dock_ext_lcd ? m_dock_ext_lcd->isVisible() : false);

    // Save dock layout and geometry
    const QByteArray layoutData = serializeDockLayout(content_window);
    qDebug("Saving dock layout: %lld bytes", (long long)layoutData.size());
    settings->setValue(QString::fromLatin1(kSettingDockLayoutJson),
                       QString::fromLatin1(layoutData.toBase64()));
    QJsonObject layoutJson = makeDockLayoutJson(content_window);
    QJsonObject debugDockState;
    const QJsonObject coreDockConnections = serializeCoreDockConnections();
    if (m_debugDocks) {
        debugDockState = m_debugDocks->serializeDockStates();
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
        settings->setValue(QString::fromLatin1(kSettingDebugDockStateJson),
                           QString::fromUtf8(QJsonDocument(debugDockState).toJson(QJsonDocument::Compact)));
    }
    layoutJson.insert(QStringLiteral("coreDockConnections"), coreDockConnections);
    settings->setValue(QString::fromLatin1(kSettingWindowLayoutJson),
                       QString::fromUtf8(QJsonDocument(layoutJson).toJson(QJsonDocument::Compact)));
    settings->setValue(QStringLiteral("windowGeometry"), saveGeometry());
    if (m_debugDocks)
        settings->setValue(QStringLiteral("debugExtraHexDockCount"), m_debugDocks->extraHexDockCount());
    QString activeProfile = settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed();
    if (activeProfile.isEmpty())
        activeProfile = QStringLiteral("default");
    settings->setValue(QString::fromLatin1(kSettingLayoutProfile), activeProfile);
    QString profileError;
    if (!saveLayoutProfile(content_window,
                           activeProfile,
                           debugDockState,
                           coreDockConnections,
                           &profileError)) {
        qDebug("save layout profile '%s' failed: %s",
               activeProfile.toUtf8().constData(),
               profileError.toUtf8().constData());
    }

    settings->setValue(QStringLiteral("lcdKeypadLinked"), m_lcdKeypadLinked);

    // Save HW config overrides
    const HwOverrides hw = {
        static_cast<int>(hw_override_get_adc_battery_level()),
        static_cast<int>(hw_override_get_adc_charging()),
        static_cast<int>(hw_override_get_lcd_contrast()),
        static_cast<int>(hw_override_get_adc_keypad_type()),
        hw_override_get_battery_mv(),
        static_cast<int>(hw_override_get_charger_state()),
        static_cast<int>(hw_override_get_battery_present()),
        static_cast<int>(hw_override_get_usb_cable_connected()),
        static_cast<int>(hw_override_get_usb_otg_cable()),
        static_cast<int>(hw_override_get_dock_attached()),
        hw_override_get_vbus_mv(),
        hw_override_get_vsled_mv(),
    };
    writeHwOverridesToSettings(settings, hw);

    settings->sync();
    m_persistentUiStateSaved = true;
}

void MainWindow::scheduleLayoutHistoryCapture()
{
    if (m_layoutHistoryApplying || !m_layoutHistoryTimer)
        return;
    m_layoutHistoryTimer->start();
}

void MainWindow::captureLayoutHistorySnapshot()
{
    if (m_layoutHistoryApplying || !content_window)
        return;

    const QByteArray state = serializeDockLayout(content_window);
    if (state.isEmpty())
        return;
    if (!m_layoutUndoHistory.isEmpty() && m_layoutUndoHistory.last() == state) {
        updateLayoutHistoryActions();
        return;
    }

    m_layoutUndoHistory.append(state);
    while (m_layoutUndoHistory.size() > kMaxLayoutHistoryEntries)
        m_layoutUndoHistory.removeFirst();
    m_layoutRedoHistory.clear();
    updateLayoutHistoryActions();
}

void MainWindow::updateLayoutHistoryActions()
{
    if (m_undoLayoutAction)
        m_undoLayoutAction->setEnabled(m_layoutUndoHistory.size() > 1);
    if (m_redoLayoutAction)
        m_redoLayoutAction->setEnabled(!m_layoutRedoHistory.isEmpty());
}

void MainWindow::undoLayoutChange()
{
    if (!content_window || m_layoutUndoHistory.size() < 2)
        return;

    const QByteArray current = m_layoutUndoHistory.takeLast();
    const QByteArray target = m_layoutUndoHistory.last();

    m_layoutHistoryApplying = true;
    const bool restored = restoreDockLayout(content_window, target);
    m_layoutHistoryApplying = false;

    if (restored) {
        m_layoutRedoHistory.append(current);
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
    } else {
        m_layoutUndoHistory.append(current);
    }
    updateLayoutHistoryActions();
}

void MainWindow::redoLayoutChange()
{
    if (!content_window || m_layoutRedoHistory.isEmpty())
        return;

    const QByteArray target = m_layoutRedoHistory.takeLast();

    m_layoutHistoryApplying = true;
    const bool restored = restoreDockLayout(content_window, target);
    m_layoutHistoryApplying = false;

    if (restored) {
        m_layoutUndoHistory.append(target);
        while (m_layoutUndoHistory.size() > kMaxLayoutHistoryEntries)
            m_layoutUndoHistory.removeFirst();
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
    } else {
        m_layoutRedoHistory.append(target);
    }
    updateLayoutHistoryActions();
}

void MainWindow::convertTabsToDocks()
{
    /* Legacy name kept for compatibility with existing call sites.
     * This function is the authoritative dock construction routine for
     * desktop UI mode and runs before layout restore. */
    /* STEP 1: Build dock-management menu and layout actions. */
    // Create "Docks" menu to make closing and opening docks more intuitive
    QMenu *docks_menu = new QMenu(tr("Docks"), this);
    ui->menubar->insertMenu(ui->menuAbout->menuAction(), docks_menu);

    QMenu *edit_menu = new QMenu(tr("&Edit"), this);
    ui->menubar->insertMenu(ui->menuTools->menuAction(), edit_menu);

    m_undoLayoutAction = edit_menu->addAction(tr("Undo Layout"));
    m_undoLayoutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Z")));
    connect(m_undoLayoutAction, &QAction::triggered, this, &MainWindow::undoLayoutChange);

    m_redoLayoutAction = edit_menu->addAction(tr("Redo Layout"));
    m_redoLayoutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+Z")));
    connect(m_redoLayoutAction, &QAction::triggered, this, &MainWindow::redoLayoutChange);
    updateLayoutHistoryActions();

    QAction *editmode_toggle = new QAction(tr("Enable UI edit mode"), this);
    editmode_toggle->setCheckable(true);
    editmode_toggle->setChecked(settings->value(QStringLiteral("uiEditModeEnabled"), true).toBool());
    connect(editmode_toggle, &QAction::toggled, this, &MainWindow::setUIEditMode);

    docks_menu->addAction(editmode_toggle);

    QAction *resetLayoutAction = new QAction(tr("Reset Layout"), this);
    docks_menu->addAction(resetLayoutAction);

    QMenu *layouts_menu = docks_menu->addMenu(tr("Layouts"));

    const auto saveLayoutProfileAction = [this](const QString &profileName) {
        const QJsonObject debugDockState = m_debugDocks ? m_debugDocks->serializeDockStates()
                                                        : QJsonObject();
        const QJsonObject coreDockConnections = serializeCoreDockConnections();
        QString error;
        if (!saveLayoutProfile(content_window,
                               profileName,
                               debugDockState,
                               coreDockConnections,
                               &error)) {
            QMessageBox::warning(this, tr("Save layout failed"),
                                 tr("Could not save layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }
        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        showStatusMsg(tr("Saved layout profile '%1'").arg(profileName));
    };

    const auto loadLayoutProfileAction = [this](const QString &profileName) {
        QString error;
        QJsonObject debugDockState;
        QJsonObject coreDockConnections;
        if (!restoreLayoutProfile(content_window,
                                  profileName,
                                  &error,
                                  &debugDockState,
                                  &coreDockConnections)) {
            if (profileName == QLatin1String("default")) {
                resetDockLayout();
                settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
                if (m_debugDocks)
                    m_debugDocks->refreshIcons();
                showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
                return;
            }
            QMessageBox::warning(this, tr("Load layout failed"),
                                 tr("Could not load layout profile '%1': %2")
                                     .arg(profileName, error));
            return;
        }

        settings->setValue(QString::fromLatin1(kSettingLayoutProfile), profileName);
        if (m_debugDocks && !debugDockState.isEmpty())
            m_debugDocks->restoreDockStates(debugDockState);
        restoreCoreDockConnections(coreDockConnections);
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
        showStatusMsg(tr("Loaded layout profile '%1'").arg(profileName));
    };

    const auto resetToLastSavedLayoutAction = [this, loadLayoutProfileAction]() {
        QString profileName = settings
                ? settings->value(QString::fromLatin1(kSettingLayoutProfile)).toString().trimmed()
                : QString();
        if (profileName.isEmpty())
            profileName = QStringLiteral("default");
        loadLayoutProfileAction(profileName);
    };
    connect(resetLayoutAction, &QAction::triggered, this,
            [resetToLastSavedLayoutAction]() { resetToLastSavedLayoutAction(); });

    QAction *loadDefaultLayoutAction = layouts_menu->addAction(tr("Load Default"));
    QAction *loadDebugLayoutAction = layouts_menu->addAction(tr("Load Debugging"));
    QAction *loadWidescreenLayoutAction = layouts_menu->addAction(tr("Load Widescreen"));
    QAction *loadCustomLayoutAction = layouts_menu->addAction(tr("Load Custom"));
    layouts_menu->addSeparator();
    QAction *resetToBaselineLayoutAction = layouts_menu->addAction(tr("Reset to Baseline"));
    layouts_menu->addSeparator();
    QAction *saveDefaultLayoutAction = layouts_menu->addAction(tr("Save As Default"));
    QAction *saveDebugLayoutAction = layouts_menu->addAction(tr("Save As Debugging"));
    QAction *saveWidescreenLayoutAction = layouts_menu->addAction(tr("Save As Widescreen"));
    QAction *saveCustomLayoutAction = layouts_menu->addAction(tr("Save As Custom"));
    layouts_menu->addSeparator();
    QAction *openLayoutFolderAction = layouts_menu->addAction(tr("Open Layout Folder"));

    connect(loadDefaultLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("default")); });
    connect(loadDebugLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("debugging")); });
    connect(loadWidescreenLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("widescreen")); });
    connect(loadCustomLayoutAction, &QAction::triggered, this,
            [loadLayoutProfileAction]() { loadLayoutProfileAction(QStringLiteral("custom")); });
    connect(resetToBaselineLayoutAction, &QAction::triggered, this, [this]() {
        resetDockLayout();
        if (m_debugDocks)
            m_debugDocks->refreshIcons();
        showStatusMsg(tr("Reset layout to baseline"));
    });
    connect(saveDefaultLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("default")); });
    connect(saveDebugLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("debugging")); });
    connect(saveWidescreenLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("widescreen")); });
    connect(saveCustomLayoutAction, &QAction::triggered, this,
            [saveLayoutProfileAction]() { saveLayoutProfileAction(QStringLiteral("custom")); });
    connect(openLayoutFolderAction, &QAction::triggered, this, [this]() {
        QString error;
        if (!ensureLayoutProfilesDir(&error)) {
            QMessageBox::warning(this, tr("Open layout folder failed"),
                                 tr("Could not open layout folder: %1").arg(error));
            return;
        }
        const QString dirPath = layoutProfilesDirPath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath))) {
            QMessageBox::warning(this, tr("Open layout folder failed"),
                                 tr("Could not open layout folder: %1").arg(dirPath));
        }
    });

    connect(layouts_menu, &QMenu::aboutToShow, this,
            [loadDefaultLayoutAction, loadDebugLayoutAction,
             loadWidescreenLayoutAction, loadCustomLayoutAction]() {
        loadDefaultLayoutAction->setEnabled(true);
        loadDebugLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("debugging"))));
        loadWidescreenLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("widescreen"))));
        loadCustomLayoutAction->setEnabled(
            QFile::exists(layoutProfilePath(QStringLiteral("custom"))));
    });

    QMenu *focusMenu = docks_menu->addMenu(tr("Dock Focus Policy"));
    QActionGroup *focusGroup = new QActionGroup(focusMenu);
    focusGroup->setExclusive(true);

    QAction *focusAlwaysAction = focusMenu->addAction(tr("Always Raise"));
    focusAlwaysAction->setCheckable(true);
    focusAlwaysAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::Always));
    focusGroup->addAction(focusAlwaysAction);

    QAction *focusExplicitAction = focusMenu->addAction(tr("Raise on Explicit Actions"));
    focusExplicitAction->setCheckable(true);
    focusExplicitAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::ExplicitOnly));
    focusGroup->addAction(focusExplicitAction);

    QAction *focusNeverAction = focusMenu->addAction(tr("Never Raise Automatically"));
    focusNeverAction->setCheckable(true);
    focusNeverAction->setData(static_cast<int>(DebugDockManager::DockFocusPolicy::Never));
    focusGroup->addAction(focusNeverAction);

    auto applyDockFocusPolicy = [this](int value) {
        DebugDockManager::DockFocusPolicy policy = DebugDockManager::DockFocusPolicy::Always;
        if (value == static_cast<int>(DebugDockManager::DockFocusPolicy::ExplicitOnly))
            policy = DebugDockManager::DockFocusPolicy::ExplicitOnly;
        else if (value == static_cast<int>(DebugDockManager::DockFocusPolicy::Never))
            policy = DebugDockManager::DockFocusPolicy::Never;
        settings->setValue(QString::fromLatin1(kSettingDockFocusPolicy), static_cast<int>(policy));
        if (m_debugDocks)
            m_debugDocks->setDockFocusPolicy(policy);
    };
    connect(focusGroup, &QActionGroup::triggered, this, [applyDockFocusPolicy](QAction *action) {
        if (!action)
            return;
        applyDockFocusPolicy(action->data().toInt());
    });

    docks_menu->addSeparator();

    /* STEP 2: Convert hidden legacy tabs into regular docks. */
    struct TabDockPair
    {
        QWidget *tab = nullptr;
        DockWidget *dock = nullptr;
    };

    QVector<TabDockPair> dock_pairs;
    while (ui->tabWidget->count())
    {
        QWidget *tab = ui->tabWidget->widget(0);
        const QString tab_title = ui->tabWidget->tabText(0);
        const QIcon tab_icon = ui->tabWidget->tabIcon(0);
        ui->tabWidget->removeTab(0);

        DockWidget *dw = createMainDock(tab_title,
                                        tab,
                                        tab->objectName(), /* stable saveState identity */
                                        Qt::RightDockWidgetArea,
                                        docks_menu,
                                        tab_icon,
                                        true,
                                        tab != ui->tab,
                                        tab == ui->tab);
        dock_pairs.append({tab, dw});
    }

    DockWidget *dock_files = nullptr;
    DockWidget *dock_keypad = nullptr;

    /* Place converted legacy tab pages as regular docks (no dedicated sidebar column). */
    for (const auto &pair : dock_pairs)
    {
        QWidget *tab = pair.tab;
        DockWidget *dw = pair.dock;

        if (tab == ui->tabFiles)         dock_files = dw;
        else if (tab == ui->tab)         dock_keypad = dw;
    }

    /* Keep pointers for layout reset/re-link behavior. */
    m_dock_files = dock_files;
    m_dock_keypad = dock_keypad;

    /* STEP 3: Create utility docks that were not tab pages. */
    /* Create NAND Browser dock */
    m_nandBrowser = new NandBrowserWidget(content_window);
    m_dock_nand = createMainDock(tr("NAND Browser"),
                                 m_nandBrowser,
                                 QString::fromLatin1(mainDockObjectName(MainDockId::NandBrowser)),
                                 Qt::RightDockWidgetArea,
                                 docks_menu,
                                 QIcon(),
                                 true,
                                 true,
                                 false);

    /* Create Hardware Configuration dock */
    m_hwConfig = new HwConfigWidget(content_window);
    m_dock_hwconfig = createMainDock(tr("Hardware Config"),
                                     m_hwConfig,
                                     QString::fromLatin1(mainDockObjectName(MainDockId::HwConfig)),
                                     Qt::RightDockWidgetArea,
                                     docks_menu,
                                     QIcon(),
                                     true,
                                     true,
                                     false);

    /* External LCD as an optional floating dock (instead of a separate window). */
    m_dock_ext_lcd = createMainDock(tr("Screen (External)"),
                                    &lcd,
                                    QString::fromLatin1(mainDockObjectName(MainDockId::ExternalLCD)),
                                    Qt::RightDockWidgetArea,
                                    docks_menu,
                                    QIcon(),
                                    false,
                                    true,
                                    false);
    m_dock_ext_lcd->setFloating(true);
    m_dock_ext_lcd->hide();
    connect(m_dock_ext_lcd, &DockWidget::visibilityChanged, this, [this](bool visible) {
        if (ui && ui->actionLCD_Window)
            ui->actionLCD_Window->setChecked(visible);
    });

    /* Add LCD and Controls dock toggle actions to the Docks menu */
    if (m_dock_lcd) {
        docks_menu->addAction(m_dock_lcd->toggleViewAction());
    }
    if (m_dock_controls) {
        docks_menu->addAction(m_dock_controls->toggleViewAction());
    }

    /* STEP 4: Wire post-dock-creation links that depend on dock objects. */
    /* -- LCD/Keypad Link ---------------------------------------- */
    if (m_dock_keypad) {
        /* QQuickWidget's Shape.CurveRenderer loses GPU state when the
         * widget is reparented during dock/undock.  Reload the QML
         * source to recreate all Shape items with fresh resources. */
        connect(m_dock_keypad, &DockWidget::topLevelChanged, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                auto src = ui->keypadWidget->source();
                ui->keypadWidget->setSource(QUrl());
                ui->keypadWidget->setSource(src);
            });
        });
    }

    // Keep default corner behavior so all docks behave like regular Qt docks.

    /* STEP 5: Create debugger docks and finalize initial dock visibility. */
    /* Create the CEmu-style debugger docks via DebugDockManager */
    m_debugDocks = std::make_unique<DebugDockManager>(content_window, material_icon_font, this);
    m_debugDocks->createDocks(docks_menu);
    connect(m_debugDocks.get(), &DebugDockManager::debugCommand,
            this, &MainWindow::debuggerCommand);

    int savedFocusPolicy = settings->value(QString::fromLatin1(kSettingDockFocusPolicy),
                                           static_cast<int>(DebugDockManager::DockFocusPolicy::Always)).toInt();
    if (savedFocusPolicy < static_cast<int>(DebugDockManager::DockFocusPolicy::Always) ||
        savedFocusPolicy > static_cast<int>(DebugDockManager::DockFocusPolicy::Never)) {
        savedFocusPolicy = static_cast<int>(DebugDockManager::DockFocusPolicy::Always);
    }
    applyDockFocusPolicy(savedFocusPolicy);
    for (QAction *action : focusGroup->actions()) {
        if (action && action->data().toInt() == savedFocusPolicy) {
            action->setChecked(true);
            break;
        }
    }

    setUIEditMode(editmode_toggle->isChecked());

    if (!m_layoutHistoryTimer) {
        m_layoutHistoryTimer = new QTimer(this);
        m_layoutHistoryTimer->setSingleShot(true);
        m_layoutHistoryTimer->setInterval(150);
        connect(m_layoutHistoryTimer, &QTimer::timeout, this, &MainWindow::captureLayoutHistorySnapshot);
    }
    const auto dockChildren = content_window->findChildren<DockWidget *>();
    for (DockWidget *dock : dockChildren) {
        connect(dock, &DockWidget::dockLocationChanged, this,
                [this](Qt::DockWidgetArea) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                });
        connect(dock, &DockWidget::topLevelChanged, this,
                [this](bool) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                    applyConnectedCoreDocks(nullptr, false);
                });
        connect(dock, &DockWidget::visibilityChanged, this,
                [this](bool) {
                    scheduleLayoutHistoryCapture();
                    scheduleCoreDockConnectOverlayRefresh();
                });
    }

    for (DockWidget *dock : coreGroupableDocks()) {
        if (!dock)
            continue;
        dock->installEventFilter(this);
    }
    scheduleCoreDockConnectOverlayRefresh();

    ui->tabWidget->setHidden(true);
}
