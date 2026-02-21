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

#include "core/debug/debug.h"
#include "core/debug/debug_api.h"
#include "core/emu.h"
#include "core/storage/flash.h"
#include "core/gif.h"
#include "core/peripherals/misc.h"
#include "core/memory/mem.h"
#include "core/usb/usblink_queue.h"

#include "ui/docking/widgets/dockwidget.h"
#include "ui/docking/widgets/kdockwidget.h"
#include "mainwindow.h"
#include "mainwindow/layout_persistence.h"
#include "ui/theme/widgettheme.h"
#include "ui/theme/materialicons.h"
#include "ui_mainwindow.h"
#include "app/qmlbridge.h"
#include "core/power/powercontrol.h"
#include "mainwindow/docks/baselinelayout.h"
#include "ui/screen/framebuffer.h"
#include "ui/input/keypadbridge.h"
#include "ui/docking/manager/dockmanager.h"

#include "ui/widgets/disassembly/disassemblywidget.h"
#include "ui/widgets/hexview/hexviewwidget.h"
#include "ui/widgets/console/consolewidget.h"
#include "ui/widgets/nandbrowser/nandbrowserwidget.h"
#include "ui/widgets/hwconfig/hwconfigwidget.h"

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
    if (m_dockManager) {
        const int extraHexDocks = qMax(0, settings->value(QStringLiteral("debugExtraHexDockCount"), 0).toInt());
        m_dockManager->ensureExtraHexDocks(extraHexDocks);
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

    if (m_dockManager) {
        if (!restoredDebugDockState.isEmpty()) {
            m_dockManager->restoreDockStates(restoredDebugDockState);
        } else if (!usedBuiltInDefaultBaseline) {
            QJsonParseError debugStateErr = {};
            const QString savedDebugState = settings->value(QString::fromLatin1(kSettingDebugDockStateJson)).toString();
            const QJsonDocument debugStateDoc = QJsonDocument::fromJson(savedDebugState.toUtf8(), &debugStateErr);
            if (debugStateErr.error == QJsonParseError::NoError && debugStateDoc.isObject())
                m_dockManager->restoreDockStates(debugStateDoc.object());
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
                if (m_dockManager && !debugDockState.isEmpty())
                    m_dockManager->restoreDockStates(debugDockState);
                restoreCoreDockConnections(coreDockConnections);
                if (m_dockManager)
                    m_dockManager->refreshIcons();
                captureLayoutHistorySnapshot();
                return;
            }

            if (appliedStartupProfile == QLatin1String("default")) {
                resetDockLayout();
                if (m_dockManager)
                    m_dockManager->refreshIcons();
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

    // UI state is persisted from the canonical shutdown path in closeEvent().
    if (!m_persistentUiStateSaved)
        qWarning("MainWindow destroyed without closeEvent persistence path");
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
    if (m_dockManager) {
        debugDockState = m_dockManager->serializeDockStates();
        layoutJson.insert(QStringLiteral("debugDockState"), debugDockState);
        settings->setValue(QString::fromLatin1(kSettingDebugDockStateJson),
                           QString::fromUtf8(QJsonDocument(debugDockState).toJson(QJsonDocument::Compact)));
    }
    layoutJson.insert(QStringLiteral("coreDockConnections"), coreDockConnections);
    settings->setValue(QString::fromLatin1(kSettingWindowLayoutJson),
                       QString::fromUtf8(QJsonDocument(layoutJson).toJson(QJsonDocument::Compact)));
    settings->setValue(QStringLiteral("windowGeometry"), saveGeometry());
    if (m_dockManager)
        settings->setValue(QStringLiteral("debugExtraHexDockCount"), m_dockManager->extraHexDockCount());
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
        if (m_dockManager)
            m_dockManager->refreshIcons();
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
        if (m_dockManager)
            m_dockManager->refreshIcons();
    } else {
        m_layoutRedoHistory.append(target);
    }
    updateLayoutHistoryActions();
}
