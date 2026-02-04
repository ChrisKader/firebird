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
#include <QMimeData>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QShortcut>
#include <QTimer>
#include <QKeySequence>
#include <QProcess>
#include <QQmlComponent>
#include <QToolBar>
#include <QWidgetAction>
#include <QIcon>
#include <QToolButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QRegularExpression>
#include <QHeaderView>
#include <QColor>

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
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>

#include "core/debug.h"
#include "core/emu.h"
#include "core/flash.h"
#include "core/gif.h"
#include "core/misc.h"
#include "core/usblink_queue.h"

#include "dockwidget.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qmlbridge.h"
#include "qtframebuffer.h"
#include "qtkeypadbridge.h"

MainWindow *main_window;
// Change this if you change the UI
static const constexpr int WindowStateVersion = 1;

namespace
{
struct WidgetTheme
{
    QColor window;
    QColor surface;
    QColor surfaceAlt;
    QColor dock;
    QColor dockTitle;
    QColor border;
    QColor accent;
    QColor text;
    QColor textMuted;
    QColor selection;
    QColor selectionText;
    QColor statusBg;
};

const WidgetTheme &currentWidgetTheme()
{
    static const WidgetTheme dark_theme{
        QColor(QStringLiteral("#181818")),
        QColor(QStringLiteral("#1e1e1e")),
        QColor(QStringLiteral("#202020")),
        QColor(QStringLiteral("#252526")),
        QColor(QStringLiteral("#1b1b1c")),
        QColor(QStringLiteral("#333333")),
        QColor(QStringLiteral("#007acc")),
        QColor(QStringLiteral("#d4d4d4")),
        QColor(QStringLiteral("#858585")),
        QColor(QStringLiteral("#264f78")),
        QColor(QStringLiteral("#ffffff")),
        QColor(QStringLiteral("#202020"))};

    static const WidgetTheme light_theme{
        QColor(QStringLiteral("#f5f5f5")),
        QColor(QStringLiteral("#ffffff")),
        QColor(QStringLiteral("#ededed")),
        QColor(QStringLiteral("#f2f2f2")),
        QColor(QStringLiteral("#e6e6e6")),
        QColor(QStringLiteral("#c4c4c4")),
        QColor(QStringLiteral("#0066b8")),
        QColor(QStringLiteral("#1f1f1f")),
        QColor(QStringLiteral("#5e5e5e")),
        QColor(QStringLiteral("#cce6ff")),
        QColor(QStringLiteral("#1a1a1a")),
        QColor(QStringLiteral("#e9e9e9"))};

    const bool use_dark = !the_qml_bridge ? true : the_qml_bridge->getDarkTheme();
    return use_dark ? dark_theme : light_theme;
}

void applyPaletteColors(QPalette &pal, const WidgetTheme &theme)
{
    pal.setColor(QPalette::Window, theme.window);
    pal.setColor(QPalette::WindowText, theme.text);
    pal.setColor(QPalette::Base, theme.surface);
    pal.setColor(QPalette::AlternateBase, theme.surfaceAlt);
    pal.setColor(QPalette::Text, theme.text);
    pal.setColor(QPalette::Button, theme.surfaceAlt);
    pal.setColor(QPalette::ButtonText, theme.text);
    pal.setColor(QPalette::Highlight, theme.selection);
    pal.setColor(QPalette::HighlightedText, theme.selectionText);
    pal.setColor(QPalette::ToolTipBase, theme.dock);
    pal.setColor(QPalette::ToolTipText, theme.text);
    pal.setColor(QPalette::PlaceholderText, theme.textMuted);
}

void setWidgetBackground(QWidget *w, const QColor &color, const QColor &text = QColor())
{
    if (!w)
        return;
    QPalette p = w->palette();
    p.setColor(QPalette::Window, color);
    p.setColor(QPalette::Base, color);
    if (text.isValid())
    {
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::ButtonText, text);
    }
    w->setAutoFillBackground(true);
    w->setPalette(p);
}
} // namespace

void MainWindow::mousePressEvent(QMouseEvent *event)
{
#ifdef Q_OS_MAC
    // Allow dragging the window when clicking in the header area
    // (adjust the height as needed - currently top 40 px)
    if (event->button() == Qt::LeftButton && event->position().y() < 40)
    {
        m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    else if (event->button() == Qt::LeftButton)
    {
        // Reset any stale drag state when clicking elsewhere
        m_dragStartPos = QPoint();
    }
#endif
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
#ifdef Q_OS_MAC
    if (event->buttons() & Qt::LeftButton && !m_dragStartPos.isNull())
    {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
        return;
    }
#endif
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
#ifdef Q_OS_MAC
    if (event->button() == Qt::LeftButton)
        m_dragStartPos = QPoint();
#endif
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
#ifdef Q_OS_MAC
    // Apply rounded corners to the frameless window on macOS
    const int radius = 12;
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width(), height()), radius, radius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
#endif
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow)
{
#ifdef Q_OS_MAC
    // Remove native title bar and frame so we can draw our own
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, false);
#endif

    ui->setupUi(this);
    stack_table = ui->stackTable;

    // Make the central content fill the full area between header and status bar
    if (ui->mainLayout)
    {
        ui->mainLayout->setContentsMargins(0, 0, 0, 0);
        ui->mainLayout->setSpacing(0);
    }

    // Load a Material-style icon font with a fallback to the TTF variant if the OTF fails.
    auto loadIconFont = [](const QString &path) -> QFont
    {
        const int fontId = QFontDatabase::addApplicationFont(path);
        if (fontId < 0)
        {
            qWarning() << "Failed to load icon font from" << path;
            return {};
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty())
        {
            qWarning() << "Icon font has no families after load:" << path;
            return {};
        }

        QFont font(families.first());
        font.setPixelSize(18);
        qDebug() << "Loaded icon font" << path << "family" << families.first();
        return font;
    };

    QFont materialIconFont = loadIconFont(QStringLiteral(":/fonts/MaterialIconsRound-Regular.otf"));
    if (materialIconFont.family().isEmpty())
        materialIconFont = loadIconFont(QStringLiteral(":/fonts/MaterialSymbolsRounded.ttf"));
    material_icon_font = materialIconFont;

    auto applyMaterialBugIcon = [materialIconFont](QToolButton *button)
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setFont(materialIconFont);
        button->setText(QString(QChar(0xE868))); // "bug_report"
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    };

    auto applyMaterialGlyph = [materialIconFont](QToolButton *button, ushort codepoint, const QString &toolTip = QString())
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setIcon(QIcon());
        button->setFont(materialIconFont);
        button->setText(QString(QChar(codepoint)));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        if (!toolTip.isEmpty())
            button->setToolTip(toolTip);
    };

    auto applyMaterialGlyphPush = [materialIconFont](QPushButton *button, ushort codepoint, const QString &toolTip = QString())
    {
        if (!button || materialIconFont.family().isEmpty())
            return;
        button->setIcon(QIcon());
        button->setFont(materialIconFont);
        button->setText(QString(QChar(codepoint)));
        if (!toolTip.isEmpty())
            button->setToolTip(toolTip);
    };

    auto applyThemeGlyph = [applyMaterialGlyph](QToolButton *button, bool darkEnabled)
    {
        // Material Symbols codepoints for dark_mode / light_mode
        const ushort glyph = darkEnabled ? 0xE51C : 0xE518;
        applyMaterialGlyph(button, glyph, darkEnabled ? QObject::tr("Switch to light mode") : QObject::tr("Switch to dark mode"));
    };

    // Add a compact "Debugger" toggle button into the custom header bar,
    // similar to VS Code's header controls. (Currently disabled)

    // Wire debugger toolbar button inside the Debugger dock (vertical bar).
    if (ui->tabDebugger)
    {
        if (auto *bar = ui->tabDebugger->findChild<QWidget *>(QStringLiteral("debuggerButtonBar")))
        {
            if (auto *toggle = bar->findChild<QToolButton *>(QStringLiteral("debuggerToggleButton")))
            {
                debugger_toggle_button = toggle;
                toggle->setCheckable(true);
                applyMaterialBugIcon(toggle); // Ensure the action text doesn't get elided to "..."
                toggle->setEnabled(ui->actionDebugger->isEnabled());
                connect(ui->actionDebugger, &QAction::changed, toggle, [this, toggle, applyMaterialBugIcon]()
                        {
                    applyMaterialBugIcon(toggle);
                    toggle->setEnabled(ui->actionDebugger->isEnabled()); });
                connect(toggle, &QToolButton::clicked, this, [this]()
                        {
                    if (!debugger_active) {
                        ui->actionDebugger->trigger();
                    } else {
                        debugStr(QStringLiteral("> c\n"));
                        emit debuggerCommand(QStringLiteral("c"));
                        setDebuggerActive(false);
                    } });
            }
            if (auto *clear = bar->findChild<QToolButton *>(QStringLiteral("debuggerClearButton")))
            {
                applyMaterialGlyph(clear, 0xE14C, tr("Clear debug output"));
                connect(clear, &QToolButton::clicked, this, [this]()
                        {
                    ui->debugConsole->clear();
                });
            }
        }
    }

    if (ui->tabSerial)
    {
        if (auto *clear = ui->tabSerial->findChild<QToolButton *>(QStringLiteral("serialClearButton")))
        {
            applyMaterialGlyph(clear, 0xE14C, tr("Clear serial output"));
            connect(clear, &QToolButton::clicked, this, [this]()
                    {
                ui->serialConsole->clear();
            });
        }
    }

    // Apply Material glyphs to main control buttons if the font is available.
    applyMaterialGlyph(ui->buttonPlayPause, 0xE037, tr("Start"));
    applyMaterialGlyph(ui->buttonReset, 0xE5D5, tr("Reset"));
    applyMaterialGlyph(ui->buttonScreenshot, 0xE412, tr("Screenshot"));
    applyMaterialGlyph(ui->buttonUSB, 0xE1E0, tr("Connect USB"));
    if (ui->horizontalLayout_7)
        ui->horizontalLayout_7->setAlignment(Qt::AlignHCenter);
    QSize controlSize = ui->buttonPlayPause->sizeHint();
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonReset->sizeHint().width()));
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonScreenshot->sizeHint().width()));
    controlSize.setWidth(qMax(controlSize.width(), ui->buttonUSB->sizeHint().width()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonReset->sizeHint().height()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonScreenshot->sizeHint().height()));
    controlSize.setHeight(qMax(controlSize.height(), ui->buttonUSB->sizeHint().height()));
    ui->buttonSpeed->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->buttonSpeed->setFixedSize(controlSize);
    applyMaterialGlyphPush(ui->buttonSpeed, 0xE9E4, tr("Toggle turbo mode"));
    ui->buttonSpeed->setCheckable(true);

    // Unified play/pause/start toggle.
    updatePlayPauseButtonFn = [this, applyMaterialGlyph]()
    {
        const bool running = ui->actionPause->isEnabled();
        const bool paused = ui->actionPause->isChecked();
        const bool playing = running && !paused;
        ushort glyph = 0xE037; // play
        QString tip = tr("Start");
        if (running)
        {
            if (paused)
            {
                glyph = 0xE037;
                tip = tr("Resume");
            }
            else
            {
                glyph = 0xE034;
                tip = tr("Pause");
            }
        }
        else
        {
            tip = ui->actionRestart->text().isEmpty() ? tr("Start") : ui->actionRestart->text().remove(QLatin1Char('&'));
        }
        applyMaterialGlyph(ui->buttonPlayPause, glyph, tip);
        ui->buttonPlayPause->setChecked(playing);
        ui->buttonPlayPause->setEnabled(ui->actionRestart->isEnabled() || running);
    };
    updatePlayPauseButtonFn();
    connect(ui->actionPause, &QAction::toggled, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->actionPause, &QAction::changed, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->actionRestart, &QAction::changed, this, [this]()
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });
    connect(ui->buttonPlayPause, &QToolButton::clicked, this, [this]()
            {
        const bool running = ui->actionPause->isEnabled();
        if (!running) {
            ui->actionRestart->trigger();
        } else {
            ui->actionPause->trigger();
        } });
    connect(&emu_thread, &EmuThread::paused, this, [this](bool)
            { if (updatePlayPauseButtonFn) updatePlayPauseButtonFn(); });

    // Create an inner QMainWindow that will host all docks and the LCD frame.
    // This lets the custom header bar sit above everything else (similar to VS Code),
    // while docks live around the central emulator surface without overlapping the header.
    content_window = new QMainWindow(this);
    content_window->setObjectName(QStringLiteral("contentWindow"));
    content_window->setDockOptions(QMainWindow::AllowTabbedDocks |
                                   QMainWindow::AllowNestedDocks |
                                   QMainWindow::AnimatedDocks);

    // Move the existing frame (LCD + controls) into the inner main window as its central widget.
    ui->mainLayout->removeWidget(ui->frame);
    ui->frame->setParent(content_window);
    content_window->setCentralWidget(ui->frame);
    ui->mainLayout->addWidget(content_window);

    // Turn the header bar into a fixed toolbar that lives above the dock/central area,
    // similar to VS Code's in-window title / command bar.
    if (ui->headerBar)
    {
        // Detach from the central layout so QMainWindow can manage it as a toolbar
        ui->mainLayout->removeWidget(ui->headerBar);
        ui->headerBar->setParent(nullptr);

        auto *headerToolBar = new QToolBar(this);
        headerToolBar->setObjectName(QStringLiteral("headerToolBar"));
        headerToolBar->setMovable(false);
        headerToolBar->setFloatable(false);
        headerToolBar->setAllowedAreas(Qt::TopToolBarArea);
        headerToolBar->setIconSize(QSize(16, 16));
        headerToolBar->setContentsMargins(0, 0, 0, 0);

        auto *headerAction = new QWidgetAction(headerToolBar);
        headerAction->setDefaultWidget(ui->headerBar);
        headerToolBar->addAction(headerAction);

        addToolBar(Qt::TopToolBarArea, headerToolBar);
    }

    // The outer MainWindow no longer hosts docks directly; keep it frameless/themed only.
    setDockOptions(QMainWindow::DockOptions());
    setUnifiedTitleAndToolBarOnMac(false);

    applyWidgetTheme();

    if (ui->statusBar)
    {
    status_bar_tray = new QWidget(ui->statusBar);
    auto *statusLayout = new QHBoxLayout(status_bar_tray);
    statusLayout->setContentsMargins(6, 0, 6, 0);
    statusLayout->setSpacing(6);

    status_label.setContentsMargins(0, 0, 0, 0);
    statusLayout->addWidget(&status_label, 0, Qt::AlignVCenter);

    statusLayout->addStretch(1);

    status_bar_speed_label = new QLabel(status_bar_tray);
    status_bar_speed_label->setObjectName(QStringLiteral("statusSpeedLabel"));
    status_bar_speed_label->setContentsMargins(0, 0, 0, 0);
    status_bar_speed_label->setMinimumWidth(90);
    status_bar_speed_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    status_bar_speed_label->setText(tr("Speed: -- %"));
    statusLayout->addWidget(status_bar_speed_label, 0, Qt::AlignVCenter);

    status_dark_button = new QToolButton(status_bar_tray);
    status_dark_button->setObjectName(QStringLiteral("statusDarkModeButton"));
    status_dark_button->setCheckable(false);
    status_dark_button->setAutoRaise(true);
    status_dark_button->setFocusPolicy(Qt::NoFocus);
    status_dark_button->setContentsMargins(0, 0, 0, 0);
    status_dark_button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    const int sbHeight = ui->statusBar->sizeHint().height();
    status_dark_button->setFixedHeight(sbHeight - 2);
    status_dark_button->setMinimumWidth(sbHeight - 2);
    statusLayout->addWidget(status_dark_button, 0, Qt::AlignVCenter);

    ui->statusBar->addPermanentWidget(status_bar_tray, 1);
    }

    // Register QtKeypadBridge for the virtual keyboard functionality
    ui->keypadWidget->installEventFilter(&qt_keypad_bridge);
    ui->lcdView->installEventFilter(&qt_keypad_bridge);
    lcd.installEventFilter(&qt_keypad_bridge);

    ui->keypadWidget->setAttribute(Qt::WA_AcceptTouchEvents);

    qml_engine = ui->keypadWidget->engine();
    qml_engine->addImportPath(QStringLiteral("qrc:/qml/qml"));
    ui->keypadWidget->setSource(QUrl(QStringLiteral("qrc:/qml/qml/ScrollingKeypad.qml")));

    // Create config dialog component
    config_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/FBConfigDialog.qml")), this);
    if (!config_component->isReady())
        qCritical() << "Could not create QML config dialog:" << config_component->errorString();

    // Create flash dialog UI component
    flash_dialog_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/FlashDialog.qml")), this);
    if (!flash_dialog_component->isReady())
        qCritical() << "Could not create flash dialog component:" << flash_dialog_component->errorString();

    // Create mobile UI component
    mobileui_component = new QQmlComponent(qml_engine, QUrl(QStringLiteral("qrc:/qml/qml/MobileUI.qml")), this);
    if (!mobileui_component->isReady())
        qCritical() << "Could not create mobile UI component:" << mobileui_component->errorString();

    if (!the_qml_bridge)
        throw std::runtime_error("Can't continue without QMLBridge");

    QAction *darkAction = findChild<QAction *>(QStringLiteral("actionDarkMode"));
    if (!darkAction && ui->menuTools)
    {
        darkAction = new QAction(tr("Dark mode"), this);
        darkAction->setObjectName(QStringLiteral("actionDarkMode"));
        darkAction->setCheckable(true);
        if (ui->menuLanguage)
            ui->menuTools->insertAction(ui->menuLanguage->menuAction(), darkAction);
        else
            ui->menuTools->addAction(darkAction);
    }
    const bool darkModeEnabled = the_qml_bridge->getDarkTheme();
    if (darkAction)
    {
        darkAction->setChecked(darkModeEnabled);
        connect(darkAction, &QAction::toggled, this, [](bool darkEnabled)
                { the_qml_bridge->setDarkTheme(darkEnabled); });
    }

    if (status_dark_button)
    {
        applyThemeGlyph(status_dark_button, darkModeEnabled);
        status_dark_button->setStyleSheet(QStringLiteral(
            "QToolButton { border: 0px; background: transparent; padding: 0 6px; outline: 0px; }"
            "QToolButton:hover { background: transparent; }"
            "QToolButton:pressed { background: transparent; }"
            "QToolButton:focus { outline: 0px; }"));
        connect(status_dark_button, &QToolButton::clicked, this, [darkAction]()
                {
            const bool next = !the_qml_bridge->getDarkTheme();
            if (darkAction)
                darkAction->setChecked(next);
            else if (the_qml_bridge)
                the_qml_bridge->setDarkTheme(next); });
    }

    connect(the_qml_bridge, &QMLBridge::darkThemeChanged, this, [this, darkAction]()
            {
        const bool dark = the_qml_bridge->getDarkTheme();
        if (darkAction && darkAction->isChecked() != dark)
            darkAction->setChecked(dark);
        applyWidgetTheme(); });
    if (status_dark_button)
    {
        connect(the_qml_bridge, &QMLBridge::darkThemeChanged, status_dark_button, [this, applyThemeGlyph]()
                {
            const bool dark = the_qml_bridge->getDarkTheme();
            applyThemeGlyph(status_dark_button, dark); });
    }

    connect(ui->buttonWindowClose, &QToolButton::clicked, this, &QWidget::close);
    connect(ui->buttonWindowMinimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(ui->buttonWindowMaximize, &QToolButton::clicked, this, &QWidget::showMaximized);

    // Emu -> GUI (QueuedConnection as they're different threads)
    connect(&emu_thread, SIGNAL(serialChar(char)), this, SLOT(serialChar(char)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debugStr(QString)), this, SLOT(debugStr(QString)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(isBusy(bool)), this, SLOT(isBusy(bool)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(statusMsg(QString)), this, SLOT(showStatusMsg(QString)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debugInputRequested(bool)), this, SLOT(debugInputRequested(bool)), Qt::QueuedConnection);
    connect(&emu_thread, SIGNAL(debuggerEntered(bool)), this, SLOT(debuggerEntered(bool)), Qt::QueuedConnection);

    // GUI -> Emu (no QueuedConnection possible, watch out!)
    connect(this, SIGNAL(debuggerCommand(QString)), &emu_thread, SLOT(debuggerInput(QString)));

    // Menu "Emulator"
    connect(ui->buttonReset, SIGNAL(clicked(bool)), &emu_thread, SLOT(reset()));
    connect(ui->actionReset, SIGNAL(triggered()), &emu_thread, SLOT(reset()));
    connect(ui->actionRestart, SIGNAL(triggered()), this, SLOT(restart()));
    connect(ui->actionDebugger, SIGNAL(triggered()), &emu_thread, SLOT(enterDebugger()));
    connect(ui->actionLaunch_IDA, SIGNAL(triggered()), this, SLOT(launchIdaInstantDebugging()));
    connect(ui->actionConfiguration, SIGNAL(triggered()), this, SLOT(openConfiguration()));
    connect(ui->actionPause, SIGNAL(toggled(bool)), &emu_thread, SLOT(setPaused(bool)));
    connect(ui->buttonSpeed, SIGNAL(clicked(bool)), &emu_thread, SLOT(setTurboMode(bool)));

    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, SIGNAL(activated()), &emu_thread, SLOT(toggleTurbo()));

    // Menu "Tools"
    connect(ui->buttonScreenshot, SIGNAL(clicked()), this, SLOT(screenshot()));
    connect(ui->actionScreenshot, SIGNAL(triggered()), this, SLOT(screenshot()));
    connect(ui->actionRecord_GIF, SIGNAL(triggered()), this, SLOT(recordGIF()));
    connect(ui->actionConnect, SIGNAL(triggered()), this, SLOT(connectUSB()));
    connect(ui->buttonUSB, SIGNAL(clicked(bool)), this, SLOT(connectUSB()));
    connect(ui->actionLCD_Window, SIGNAL(triggered(bool)), this, SLOT(setExtLCD(bool)));
    connect(&lcd, SIGNAL(closed()), ui->actionLCD_Window, SLOT(toggle()));
    connect(ui->actionXModem, SIGNAL(triggered()), this, SLOT(xmodemSend()));
    connect(ui->actionSwitch_to_Mobile_UI, SIGNAL(triggered()), this, SLOT(switchToMobileUI()));
    connect(ui->actionLeavePTT, &QAction::triggered, the_qml_bridge, &QMLBridge::sendExitPTT);
    ui->actionConnect->setShortcut(QKeySequence(Qt::Key_F10));
    ui->actionConnect->setAutoRepeat(false);

    // Menu "State"
    connect(ui->actionResume, SIGNAL(triggered()), this, SLOT(resume()));
    connect(ui->actionSuspend, SIGNAL(triggered()), this, SLOT(suspend()));
    connect(ui->actionResume_from_file, SIGNAL(triggered()), this, SLOT(resumeFromFile()));
    connect(ui->actionSuspend_to_file, SIGNAL(triggered()), this, SLOT(suspendToFile()));

    // Menu "Flash"
    connect(ui->actionSave, SIGNAL(triggered()), this, SLOT(saveFlash()));
    connect(ui->actionCreate_flash, SIGNAL(triggered()), this, SLOT(createFlash()));

    // Menu "About"
    connect(ui->actionAbout_Firebird, SIGNAL(triggered(bool)), this, SLOT(showAbout()));
    connect(ui->actionAbout_Qt, SIGNAL(triggered(bool)), qApp, SLOT(aboutQt()));

    // Lang switch
    QStringList translations = QDir(QStringLiteral(":/i18n/i18n/")).entryList();
    translations << QStringLiteral("en_US.qm"); // Equal to no translation
    for (auto &languageCode : translations)
    {
        languageCode.chop(3); // Chop off file extension
        QLocale locale(languageCode);
        QAction *action = new QAction(locale.nativeLanguageName(), ui->menuLanguage);
        connect(action, &QAction::triggered, this, [this, languageCode]
                { this->switchTranslator(QLocale(languageCode)); });
        ui->menuLanguage->addAction(action);
    }

    // Debugging
    connect(ui->lineEdit, SIGNAL(returnPressed()), this, SLOT(debugCommand()));

    // File transfer
    connect(ui->refreshButton, SIGNAL(clicked(bool)), ui->usblinkTree, SLOT(reloadFilebrowser()));
    connect(ui->usblinkTree, SIGNAL(downloadProgress(int)), this, SLOT(usblinkDownload(int)), Qt::QueuedConnection);
    connect(ui->usblinkTree, SIGNAL(uploadProgress(int)), this, SLOT(changeProgress(int)), Qt::QueuedConnection);
    connect(this, SIGNAL(usblink_progress_changed(int)), this, SLOT(changeProgress(int)), Qt::QueuedConnection);

    // QMLBridge
    KitModel *model = the_qml_bridge->getKitModel();
    connect(model, SIGNAL(anythingChanged()), this, SLOT(kitAnythingChanged()));
    connect(model, SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)), this, SLOT(kitDataChanged(QModelIndex, QModelIndex, QVector<int>)));
    connect(the_qml_bridge, SIGNAL(currentKitChanged(const Kit &)), this, SLOT(currentKitChanged(const Kit &)));

    // Set up monospace fonts
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setStyleHint(QFont::Monospace);
    ui->debugConsole->setFont(monospace);
    ui->serialConsole->setFont(monospace);
    if (stack_table)
    {
        stack_table->setFont(monospace);
        stack_table->setColumnCount(2);
        stack_table->setHorizontalHeaderLabels({tr("Address"), tr("Instruction")});
        stack_table->horizontalHeader()->setStretchLastSection(true);
        stack_table->horizontalHeader()->setHighlightSections(false);
        stack_table->verticalHeader()->setVisible(false);
        stack_table->setSelectionMode(QAbstractItemView::NoSelection);
        stack_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        stack_table->setAlternatingRowColors(true);
        stack_table->setFocusPolicy(Qt::NoFocus);
    }

    // Without this line, Qt prints warning messages...
    qRegisterMetaType<QVector<int>>();

#ifdef Q_OS_ANDROID
    // On android the settings file is deleted everytime you update or uninstall,
    // so choose a better, safer, location
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    settings = new QSettings(path + QStringLiteral("/nspire_emu_thread.ini"), QSettings::IniFormat);
#else
    settings = new QSettings();
#endif

    QString prefLang = settings->value(QStringLiteral("preferred_lang"), QStringLiteral("none")).toString();
    if (prefLang != QStringLiteral("none"))
        switchTranslator(QLocale(prefLang));
    else if (appTranslator.load(QLocale::system(), QStringLiteral(":/i18n/i18n/")))
        qApp->installTranslator(&appTranslator);

    updateUIActionState(false);

    // Load settings
    convertTabsToDocks();
    retranslateDocks();
    setExtLCD(settings->value(QStringLiteral("extLCDVisible")).toBool());
    lcd.restoreGeometry(settings->value(QStringLiteral("extLCDGeometry")).toByteArray());
    restoreGeometry(settings->value(QStringLiteral("windowGeometry")).toByteArray());
    content_window->restoreState(settings->value(QStringLiteral("windowState")).toByteArray(), WindowStateVersion);

    refillKitMenus();

    ui->lcdView->setFocus();

    // Ensure dock buttons/theme are refreshed after docks are created.
    applyWidgetTheme();

    // Select default Kit
    bool defaultKitFound = the_qml_bridge->useDefaultKit();

    if (the_qml_bridge->getKitModel()->allKitsEmpty())
    {
        // Do not show the window before MainWindow gets shown,
        // otherwise it won't be in focus.
        QTimer::singleShot(0, this, SIGNAL(openConfiguration()));

        switchUIMode(true);

        return;
    }

    if (settings->value(QStringLiteral("lastUIMode"), 1).toUInt() == 1)
        switchUIMode(true);
    else
    {
        switchUIMode(false);
        show();
    }

    if (!the_qml_bridge->getAutostart())
    {
        showStatusMsg(tr("Start the emulation via Emulation->Start."));
        return;
    }

    // Autostart handling
    if (!defaultKitFound)
        showStatusMsg(tr("Default Kit not found"));
    else
    {
        bool resumed = false;
        if (!the_qml_bridge->getSnapshotPath().isEmpty())
            resumed = resume();

        if (!resumed)
        {
            // Boot up normally
            if (!emu_thread.boot1.isEmpty() && !emu_thread.flash.isEmpty())
                restart();
            else
                showStatusMsg(tr("Start the emulation via Emulation->Start."));
        }
    }
}

void MainWindow::applyWidgetTheme()
{
    const WidgetTheme &theme = currentWidgetTheme();

    QPalette pal = qApp->palette();
    applyPaletteColors(pal, theme);
    qApp->setPalette(pal);
    setPalette(pal);

    setWidgetBackground(this, theme.window, theme.text);
    setWidgetBackground(content_window, theme.window, theme.text);
    setWidgetBackground(ui->frame, theme.surface, theme.text);
    setWidgetBackground(ui->headerBar, theme.surfaceAlt, theme.text);

    if (auto *frame = qobject_cast<QFrame *>(ui->frame))
    {
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setFrameShadow(QFrame::Plain);
        frame->setLineWidth(1);
        frame->setMidLineWidth(0);
        frame->setStyleSheet(QStringLiteral(
            "QFrame#frame {"
            " border: none;"
            " }"));
    }

    if (ui->lcdView)
    {
        ui->lcdView->setStyleSheet(QStringLiteral(
            "QWidget#lcdView {"
            " border: 1px solid %1;"
            " background: %2;"
            " }").arg(theme.border.name(), theme.surface.name()));
    }

    if (ui->menubar)
    {
        QPalette menuPal = ui->menubar->palette();
        applyPaletteColors(menuPal, theme);
        ui->menubar->setPalette(menuPal);
    }

    const auto toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars)
    {
        QPalette barPal = toolbar->palette();
        applyPaletteColors(barPal, theme);
        toolbar->setPalette(barPal);
        toolbar->setAutoFillBackground(true);
    }

    if (ui->statusBar)
    {
        QPalette statusPal = ui->statusBar->palette();
        statusPal.setColor(QPalette::Window, theme.statusBg);
        statusPal.setColor(QPalette::WindowText, theme.textMuted);
        statusPal.setColor(QPalette::Text, theme.textMuted);
        statusPal.setColor(QPalette::ButtonText, theme.textMuted);
        ui->statusBar->setAutoFillBackground(true);
        ui->statusBar->setPalette(statusPal);
        ui->statusBar->setStyleSheet(QStringLiteral(
            "QStatusBar {"
            " background: %1;"
            " color: %2;"
            " border-top: 1px solid %3;"
            "}"
            "QStatusBar::item {"
            " border: none;"
            " }")
                                         .arg(theme.statusBg.name(), theme.textMuted.name(), theme.border.name()));
    }

    auto styleToolButtons = [&](QObject *root) {
        if (!root)
            return;
        const QString normalBg = theme.surfaceAlt.name();
        const QString pressedBg = theme.surface.name();
        const QString borderColor = theme.border.name();
        const QString textColor = theme.text.name();
        const QString style = QStringLiteral(
            "QToolButton {"
            " background:%1;"
            " border:1px solid %2;"
            " border-radius:3px;"
            " padding:4px 6px;"
            " color:%4;"
            "}"
            "QToolButton:pressed, QToolButton:checked {"
            " background:%3;"
            "}"
            "QToolButton:hover {"
            " background:%3;"
            "}")
                                      .arg(normalBg, borderColor, pressedBg, textColor);
        const auto buttons = root->findChildren<QToolButton *>();
        for (QToolButton *btn : buttons)
        {
            if (btn == status_dark_button)
                continue; // Status bar uses its own minimal styling
            btn->setStyleSheet(style);
            btn->setAutoRaise(false);
        }
    };

    styleToolButtons(this);
    styleToolButtons(ui->headerBar);

    if (ui->buttonSpeed)
    {
        ui->buttonSpeed->setFlat(false);
        ui->buttonSpeed->setAutoDefault(false);
        ui->buttonSpeed->setDefault(false);
        ui->buttonSpeed->setStyleSheet(QStringLiteral(
            "QPushButton#buttonSpeed {"
            " background: %1;"
            " border: 1px solid %2;"
            " border-radius: 3px;"
            " padding: 4px 6px;"
            " color: %3;"
            "}"
            "QPushButton#buttonSpeed:hover {"
            " background: %4;"
            "}"
            "QPushButton#buttonSpeed:pressed {"
            " background: %5;"
            "}"
            "QPushButton#buttonSpeed:checked {"
            " background: %6;"
            " color: %7;"
            " border-color: %6;"
            "}"
            "QPushButton#buttonSpeed:checked:hover {"
            " background: %6;"
            "}"
            "QPushButton#buttonSpeed:checked:pressed {"
            " background: %5;"
            " color: %7;"
            " border-color: %6;"
            "}")
                                           .arg(theme.surfaceAlt.name(),
                                                theme.border.name(),
                                                theme.text.name(),
                                                theme.surface.name(),
                                                theme.surfaceAlt.name(),
                                                theme.accent.name(),
                                                theme.selectionText.name()));

        // Align speed button size with the other control buttons after styling.
        QSize targetSize = ui->buttonPlayPause->sizeHint();
        targetSize.setWidth(qMax(targetSize.width(), ui->buttonReset->sizeHint().width()));
        targetSize.setWidth(qMax(targetSize.width(), ui->buttonScreenshot->sizeHint().width()));
        targetSize.setWidth(qMax(targetSize.width(), ui->buttonUSB->sizeHint().width()));
        targetSize.setHeight(qMax(targetSize.height(), ui->buttonReset->sizeHint().height()));
        targetSize.setHeight(qMax(targetSize.height(), ui->buttonScreenshot->sizeHint().height()));
        targetSize.setHeight(qMax(targetSize.height(), ui->buttonUSB->sizeHint().height()));
        ui->buttonSpeed->setFixedSize(targetSize);
    }

    const auto docks = findChildren<DockWidget *>();
    for (DockWidget *dock : docks)
    {
        setWidgetBackground(dock, theme.dock, theme.text);
        dock->setStyleSheet(QStringLiteral(
            "QDockWidget {"
            " border: 1px solid %1;"
            "}"
            "QDockWidget::title {"
            " background: %2;"
            " margin: 0;"
            " padding: 0;"
            " }")
                                .arg(theme.border.name(), theme.dockTitle.name()));
        if (auto *title = dock->findChild<QWidget *>(QStringLiteral("dockTitleBar")))
        {
            setWidgetBackground(title, theme.dockTitle, theme.text);
        }
        if (auto *label = dock->findChild<QLabel *>(QStringLiteral("dockTitleLabel")))
        {
            QPalette labelPal = label->palette();
            labelPal.setColor(QPalette::WindowText, theme.text);
            labelPal.setColor(QPalette::Text, theme.text);
            label->setPalette(labelPal);
        }
        dock->applyButtonStyle(material_icon_font);
    }

    if (ui->tabDebugger)
        setWidgetBackground(ui->tabDebugger, theme.surface, theme.text);
    if (ui->tabSerial)
        setWidgetBackground(ui->tabSerial, theme.surface, theme.text);
    if (ui->tabFiles)
        setWidgetBackground(ui->tabFiles, theme.surface, theme.text);

    const auto itemViews = findChildren<QAbstractItemView *>();
    for (QAbstractItemView *view : itemViews)
    {
        QPalette viewPal = view->palette();
        viewPal.setColor(QPalette::Base, theme.surface);
        viewPal.setColor(QPalette::AlternateBase, theme.surfaceAlt);
        viewPal.setColor(QPalette::Text, theme.text);
        viewPal.setColor(QPalette::Highlight, theme.selection);
        viewPal.setColor(QPalette::HighlightedText, theme.selectionText);
        view->setPalette(viewPal);
        view->setAutoFillBackground(true);
    }

    if (stack_table)
        stack_table->setAlternatingRowColors(true);
}

MainWindow::~MainWindow()
{
    if (config_dialog)
        config_dialog->deleteLater();

    if (flash_dialog)
        flash_dialog->deleteLater();

    if (mobileui_component)
        mobileui_component->deleteLater();

    // Save external LCD geometry
    settings->setValue(QStringLiteral("extLCDGeometry"), lcd.saveGeometry());
    settings->setValue(QStringLiteral("extLCDVisible"), lcd.isVisible());

    // Save MainWindow state and geometry
    settings->setValue(QStringLiteral("windowState"), content_window->saveState(WindowStateVersion));
    settings->setValue(QStringLiteral("windowGeometry"), saveGeometry());

    delete settings;
    delete ui;
}

void MainWindow::switchTranslator(const QLocale &locale)
{
    qApp->removeTranslator(&appTranslator);
    // For English, nothing to load after removing the translator.
    if (locale.name() == QStringLiteral("en_US") || (appTranslator.load(locale, QStringLiteral(":/i18n/i18n/")) && qApp->installTranslator(&appTranslator)))
    {
        settings->setValue(QStringLiteral("preferred_lang"), locale.name());
    }
    else
        QMessageBox::warning(this, tr("Language change"), tr("No translation available for this language :("));
}

void MainWindow::changeEvent(QEvent *event)
{
    const auto eventType = event->type();
    if (eventType == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        updateWindowTitle();
        retranslateDocks();
    }
    else if (eventType == QEvent::LocaleChange)
        switchTranslator(QLocale::system());

    QMainWindow::changeEvent(event);
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QMimeData *mime_data = e->mimeData();
    if (!mime_data->hasUrls())
        return;

    for (auto &&url : mime_data->urls())
    {
        auto local = QDir::toNativeSeparators(url.toLocalFile());
        auto remote = the_qml_bridge->getUSBDir() + QLatin1Char('/') + QFileInfo(local).fileName();
        usblink_queue_put_file(local.toStdString(), remote.toStdString(), usblink_progress_callback, this);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() == false)
        return e->ignore();

    for (QUrl &url : e->mimeData()->urls())
    {
        static const QStringList valid_suffixes = {QStringLiteral("tns"),
                                                   QStringLiteral("tno"), QStringLiteral("tnc"),
                                                   QStringLiteral("tco"), QStringLiteral("tcc"),
                                                   QStringLiteral("tco2"), QStringLiteral("tcc2"),
                                                   QStringLiteral("tct2")};

        QFileInfo file(url.fileName());
        if (!valid_suffixes.contains(file.suffix().toLower()))
            return e->ignore();
    }

    e->accept();
}

void MainWindow::serialChar(const char c)
{
    ui->serialConsole->moveCursor(QTextCursor::End);

    static char previous = 0;
    enum EscapeState { EscapeNone, EscapeStart, EscapeCSI };
    static EscapeState escape_state = EscapeNone;
    static QByteArray escape_buffer;
    static bool format_initialized = false;
    static QTextCharFormat base_format;
    static QTextCharFormat current_format;

    if (!format_initialized)
    {
        base_format = ui->serialConsole->currentCharFormat();
        current_format = base_format;
        format_initialized = true;
    }

    auto applySgr = [&](const QList<int> &params)
    {
        if (params.isEmpty())
        {
            current_format = base_format;
            return;
        }

        for (int code : params)
        {
            if (code == 0)
            {
                current_format = base_format;
            }
            else if (code == 1)
            {
                current_format.setFontWeight(QFont::Bold);
            }
            else if (code == 22)
            {
                current_format.setFontWeight(base_format.fontWeight());
            }
            else if (code == 39)
            {
                current_format.setForeground(base_format.foreground());
            }
            else if (code >= 30 && code <= 37)
            {
                static const QColor colors[] = {
                    Qt::black, Qt::red, Qt::green, Qt::yellow,
                    Qt::blue, Qt::magenta, Qt::cyan, Qt::lightGray};
                current_format.setForeground(colors[code - 30]);
            }
            else if (code >= 90 && code <= 97)
            {
                static const QColor bright_colors[] = {
                    Qt::darkGray, Qt::red, Qt::green, Qt::yellow,
                    Qt::blue, Qt::magenta, Qt::cyan, Qt::white};
                current_format.setForeground(bright_colors[code - 90]);
            }
        }
    };

    if (escape_state == EscapeStart)
    {
        if (c == '[')
        {
            escape_state = EscapeCSI;
            escape_buffer.clear();
        }
        else if (c >= 0x40 && c <= 0x7E)
        {
            escape_state = EscapeNone; // Short escape, ignore
        }
        else
        {
            escape_state = EscapeNone;
        }
        previous = 0;
        return;
    }
    if (escape_state == EscapeCSI)
    {
        if (c >= 0x40 && c <= 0x7E)
        {
            if (c == 'm')
            {
                // Parse SGR parameters separated by ';'
                QList<int> params;
                if (escape_buffer.isEmpty())
                    params.append(0);
                else
                {
                    for (const QByteArray &part : escape_buffer.split(';'))
                        params.append(part.isEmpty() ? 0 : part.toInt());
                }
                applySgr(params);
            }
            escape_state = EscapeNone;
            escape_buffer.clear();
            previous = 0;
            return;
        }
        escape_buffer.append(c);
        return;
    }
    if (c == '\x1B') // ESC starts an ANSI sequence
    {
        escape_state = EscapeStart;
        previous = 0;
        return;
    }

    switch (c)
    {
    case 0:

    case '\r':
        previous = c;
        break;

    case '\b':
        ui->serialConsole->textCursor().deletePreviousChar();
        break;

    default:
        if (previous == '\r' && c != '\n')
        {
            ui->serialConsole->moveCursor(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
            ui->serialConsole->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
            ui->serialConsole->textCursor().removeSelectedText();
            previous = 0;
        }
        {
            QTextCursor cursor = ui->serialConsole->textCursor();
            cursor.insertText(QString(QChar::fromLatin1(c)), current_format);
            ui->serialConsole->setTextCursor(cursor);
        }
    }
}

void MainWindow::debugInputRequested(bool b)
{
    setDebuggerActive(b);
    switchUIMode(false);

    ui->lineEdit->setEnabled(b);

    if (b)
    {
        raiseDebugger();
        ui->lineEdit->setFocus();
    }
}

void MainWindow::debuggerEntered(bool entered)
{
    if (!gdb_connected)
        return;

    setDebuggerActive(entered);
    ui->lineEdit->setEnabled(entered);
    if (entered)
    {
        raiseDebugger();
        ui->lineEdit->setFocus();
    }
}

void MainWindow::debugStr(QString str)
{
    ui->debugConsole->moveCursor(QTextCursor::End);
    ui->debugConsole->insertPlainText(str);
}

void MainWindow::debugCommand()
{
    emit debugStr(QStringLiteral("> %1\n").arg(ui->lineEdit->text()));
    emit debuggerCommand(ui->lineEdit->text());
    ui->lineEdit->clear();
}

void MainWindow::requestDisassembly()
{
    disasm_entries.clear();
    refreshDisassemblyTable();
    emit debuggerCommand(QStringLiteral("u"));
}

bool MainWindow::appendDisassemblyLine(const QString &line)
{
    // Be permissive: accept lines like "00011c8c: e3510000    cmp r3,00000000" (with tabs/spaces)
    QString cleaned = line.trimmed();
    if (cleaned.startsWith(QLatin1Char('>')))
        cleaned.remove(0, 1);
    int colon = cleaned.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;

    QString addr = cleaned.left(colon).trimmed();
    QString text = cleaned.mid(colon + 1).trimmed();

    bool ok = false;
    addr.toUInt(&ok, 16);
    if (!ok)
        return false;

    DisasmEntry entry;
    entry.address = addr.toUpper();
    entry.text = text;
    if (entry.text.contains(QStringLiteral("<<")))
    {
        entry.is_current = true;
        entry.text.replace(QStringLiteral("<<"), QStringLiteral(" "));
        entry.text = entry.text.trimmed();
    }

    if (disasm_entries.size() > 200)
        disasm_entries.removeFirst();
    disasm_entries.push_back(entry);
    refreshDisassemblyTable();
    return true;
}

void MainWindow::refreshDisassemblyTable()
{
    if (!stack_table)
        return;

    stack_table->setUpdatesEnabled(false);
    stack_table->setRowCount(disasm_entries.size());
    stack_table->setColumnCount(2);
    stack_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    stack_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    int row = 0;
    const WidgetTheme &theme = currentWidgetTheme();
    for (const auto &entry : disasm_entries)
    {
        auto *addrItem = new QTableWidgetItem(entry.address);
        auto *textItem = new QTableWidgetItem(entry.text);
        addrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        textItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        addrItem->setForeground(theme.text);
        textItem->setForeground(theme.text);
        stack_table->setItem(row, 0, addrItem);
        stack_table->setItem(row, 1, textItem);
        if (entry.is_current)
        {
            addrItem->setBackground(theme.selection);
            textItem->setBackground(theme.selection);
            addrItem->setForeground(theme.selectionText);
            textItem->setForeground(theme.selectionText);
        }
        ++row;
    }
    stack_table->resizeColumnsToContents();
    stack_table->setUpdatesEnabled(true);
}

void MainWindow::setDebuggerActive(bool active)
{
    debugger_active = active;
    if (debugger_toggle_button)
    {
        debugger_toggle_button->setCheckable(true);
        debugger_toggle_button->setChecked(active);
        debugger_toggle_button->setToolTip(active ? tr("Continue (send 'c')") : tr("Enter debugger"));
    }
}

void MainWindow::usblinkDownload(int progress)
{
    usblinkProgress(progress);

    if (progress < 0)
        QMessageBox::warning(this, tr("Download failed"), tr("Could not download file."));
}

void MainWindow::usblinkProgress(int progress)
{
    if (progress < 0 || progress > 100)
        progress = 0; // No error handling here

    emit usblink_progress_changed(progress);
}

void MainWindow::usblink_progress_callback(int progress, void *)
{
    // TODO: Don't do a full refresh
    // Also refresh on error, in case of multiple transfers
    if ((progress == 100 || progress < 0) && usblink_queue_size() == 1)
        main_window->ui->usblinkTree->wantToReload(); // Reload the file explorer after uploads finished

    if (progress < 0 || progress > 100)
        progress = 0; // No error handling here

    emit main_window->usblink_progress_changed(progress);
}

void MainWindow::switchUIMode(bool mobile_ui)
{
    if (!mobileui_dialog && mobile_ui)
        mobileui_dialog = mobileui_component->create();

    if (mobileui_dialog)
        mobileui_dialog->setProperty("visible", mobile_ui);
    else if (mobile_ui)
    {
        qWarning() << "Could not create mobile UI!";
        return; // Do not switch the UI mode as the mobile UI could not be created
    }

    the_qml_bridge->setActive(mobile_ui);
    this->setActive(!mobile_ui);

    settings->setValue(QStringLiteral("lastUIMode"), mobile_ui ? 1 : 0);
}

void MainWindow::setActive(bool b)
{
    // There is no UniqueQueuedConnection, so we need to avoid duplicate connections
    // manually
    if (b == is_active)
        return;

    is_active = b;

    if (b)
    {
        connect(&emu_thread, SIGNAL(speedChanged(double)), this, SLOT(showSpeed(double)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(turboModeChanged(bool)), ui->buttonSpeed, SLOT(setChecked(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(usblinkChanged(bool)), this, SLOT(usblinkChanged(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(started(bool)), this, SLOT(started(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(paused(bool)), ui->actionPause, SLOT(setChecked(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(resumed(bool)), this, SLOT(resumed(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(suspended(bool)), this, SLOT(suspended(bool)), Qt::QueuedConnection);
        connect(&emu_thread, SIGNAL(stopped()), this, SLOT(stopped()), Qt::QueuedConnection);

        // We might have missed a few events
        updateUIActionState(emu_thread.isRunning());
        ui->buttonSpeed->setChecked(turbo_mode);
        usblinkChanged(usblink_connected);
    }
    else
    {
        disconnect(&emu_thread, SIGNAL(speedChanged(double)), this, SLOT(showSpeed(double)));
        disconnect(&emu_thread, SIGNAL(turboModeChanged(bool)), ui->buttonSpeed, SLOT(setChecked(bool)));
        disconnect(&emu_thread, SIGNAL(usblinkChanged(bool)), this, SLOT(usblinkChanged(bool)));
        disconnect(&emu_thread, SIGNAL(started(bool)), this, SLOT(started(bool)));
        disconnect(&emu_thread, SIGNAL(paused(bool)), ui->actionPause, SLOT(setChecked(bool)));
        disconnect(&emu_thread, SIGNAL(resumed(bool)), this, SLOT(resumed(bool)));
        disconnect(&emu_thread, SIGNAL(suspended(bool)), this, SLOT(suspended(bool)));
        disconnect(&emu_thread, SIGNAL(stopped()), this, SLOT(stopped()));

        // Close the config dialog
        if (config_dialog)
            config_dialog->setProperty("visible", false);
    }

    setVisible(b);
}

void MainWindow::suspendToPath(QString path)
{
    emu_thread.suspend(path);
}

bool MainWindow::resumeFromPath(QString path)
{
    if (!emu_thread.resume(path))
    {
        QMessageBox::warning(this, tr("Could not resume"), tr("Try to restart this app."));
        return false;
    }

    return true;
}

void MainWindow::changeProgress(int value)
{
    ui->progressBar->setValue(value);
}

void MainWindow::updateUIActionState(bool emulation_running)
{
    ui->actionReset->setEnabled(emulation_running);
    ui->actionPause->setEnabled(emulation_running);
    ui->actionRestart->setText(emulation_running ? tr("Re&start") : tr("&Start"));
    ui->actionRestart->setToolTip(emulation_running ? tr("Restart") : tr("Start"));
    ui->buttonPlayPause->setToolTip(emulation_running ? tr("Restart") : tr("Start"));

    ui->actionScreenshot->setEnabled(emulation_running);
    ui->actionRecord_GIF->setEnabled(emulation_running);
    ui->actionConnect->setEnabled(emulation_running);
    ui->actionDebugger->setEnabled(emulation_running);
    ui->actionXModem->setEnabled(emulation_running);
    ui->actionLeavePTT->setEnabled(emulation_running);

    ui->actionSuspend->setEnabled(emulation_running);
    ui->actionSuspend_to_file->setEnabled(emulation_running);
    ui->actionSave->setEnabled(emulation_running);

    ui->buttonSpeed->setEnabled(true);
}

void MainWindow::raiseDebugger()
{
    if (dock_debugger)
    {
        dock_debugger->setVisible(true);
        dock_debugger->raise();
    }
}

void MainWindow::convertTabsToDocks()
{
    // Create "Docks" menu to make closing and opening docks more intuitive
    QMenu *docks_menu = new QMenu(tr("Docks"), this);
    ui->menubar->insertMenu(ui->menuAbout->menuAction(), docks_menu);

    QAction *editmode_toggle = new QAction(tr("Enable UI edit mode"), this);
    editmode_toggle->setCheckable(true);
    editmode_toggle->setChecked(settings->value(QStringLiteral("uiEditModeEnabled"), true).toBool());
    connect(editmode_toggle, SIGNAL(toggled(bool)), this, SLOT(setUIEditMode(bool)));

    docks_menu->addAction(editmode_toggle);

    docks_menu->addSeparator();

    // Convert the hidden tab pages into movable/floatable dock widgets around the central panel.
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

        DockWidget *dw = new DockWidget(tab_title, content_window);
        dw->hideTitlebar(true); // Create with hidden titlebar to not resize the window on startup
        dw->setWindowIcon(tab_icon);
        // This is used for storing window state, so must not be translated at this point
        dw->setObjectName(tab_title);
        dw->setAllowedAreas(Qt::AllDockWidgetAreas);
        dw->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

        // Fill "Docks" menu
        QAction *action = dw->toggleViewAction();
        action->setIcon(dw->windowIcon());
        docks_menu->addAction(action);

        if (tab == ui->tabDebugger)
            dock_debugger = dw;

        dw->setWidget(tab);

        dock_pairs.append({tab, dw});
    }

    DockWidget *dock_files = nullptr;
    DockWidget *dock_serial = nullptr;
    DockWidget *dock_keypad = nullptr;
    DockWidget *dock_debug_anchor = nullptr;

    // Default layout: file transfer on the left, debugger/serial stacked on the right, keypad along the bottom.
    for (const auto &pair : dock_pairs)
    {
        QWidget *tab = pair.tab;
        DockWidget *dw = pair.dock;

        if (tab == ui->tabDebugger)
        {
            content_window->addDockWidget(Qt::RightDockWidgetArea, dw);
            dock_debug_anchor = dw;
        }
        else if (tab == ui->tabSerial)
        {
            content_window->addDockWidget(Qt::RightDockWidgetArea, dw);
            dock_serial = dw;
            if (dock_debug_anchor && dock_debug_anchor != dw)
                content_window->tabifyDockWidget(dock_debug_anchor, dw);
            else if (!dock_debug_anchor)
                dock_debug_anchor = dw;
        }
        else if (tab == ui->tabFiles)
        {
            content_window->addDockWidget(Qt::LeftDockWidgetArea, dw);
            dock_files = dw;
        }
        else if (tab == ui->tab)
        {
            content_window->addDockWidget(Qt::BottomDockWidgetArea, dw);
            dock_keypad = dw;
        }
        else
        {
            content_window->addDockWidget(Qt::RightDockWidgetArea, dw);
            if (dock_debug_anchor && dock_debug_anchor != dw)
                content_window->tabifyDockWidget(dock_debug_anchor, dw);
            if (!dock_debug_anchor)
                dock_debug_anchor = dw;
        }
    }

    if (dock_debugger && dock_serial && dock_debugger != dock_serial)
        content_window->tabifyDockWidget(dock_debugger, dock_serial);

    if (dock_files && dock_keypad)
        content_window->tabifyDockWidget(dock_files, dock_keypad);
    else if (dock_files && dock_serial)
        content_window->tabifyDockWidget(dock_files, dock_serial);

    // Hint Qt about corner preferences so the left/bottom docks stay grouped, keeping the LCD central.
    content_window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    content_window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    content_window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    content_window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // Bias initial space to keep the LCD focal and leave reasonable dock columns.
    QList<QDockWidget *> horiz_targets;
    QList<int> horiz_sizes;
    if (dock_files)
    {
        horiz_targets << dock_files;
        horiz_sizes << 260;
    }
    if (dock_debugger)
    {
        horiz_targets << dock_debugger;
        horiz_sizes << 320;
    }
    if (!horiz_targets.isEmpty())
        content_window->resizeDocks(horiz_targets, horiz_sizes, Qt::Horizontal);

    QList<QDockWidget *> vert_targets;
    QList<int> vert_sizes;
    if (dock_keypad)
    {
        vert_targets << dock_keypad;
        vert_sizes << 240;
    }
    if (!vert_targets.isEmpty())
        content_window->resizeDocks(vert_targets, vert_sizes, Qt::Vertical);

    setUIEditMode(editmode_toggle->isChecked());

    ui->tabWidget->setHidden(true);
}

void MainWindow::retranslateDocks()
{
    // The docks are not handled by mainwindow.ui but got created by
    // convertTabsToDocks() above, so translation needs to be done manually.
    const auto dockChildren = content_window->findChildren<DockWidget *>();
    for (DockWidget *dw : dockChildren)
    {
        if (dw->widget() == ui->tab)
            dw->setWindowTitle(tr("Keypad"));
        else if (dw->widget() == ui->tabFiles)
            dw->setWindowTitle(tr("File Transfer"));
        else if (dw->widget() == ui->tabSerial)
            dw->setWindowTitle(tr("Serial Monitor"));
        else if (dw->widget() == ui->tabDebugger)
            dw->setWindowTitle(tr("Debugger"));
    }
}

void MainWindow::showSpeed(double value)
{
    if (status_bar_speed_label)
        status_bar_speed_label->setText(tr("Speed: %1 %").arg(value * 100, 1, 'f', 0));
}

void MainWindow::screenshot()
{
    QImage image = renderFramebuffer();

    QString filename = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), QString(), tr("PNG images (*.png)"));
    if (filename.isEmpty())
        return;

    if (!image.save(filename, "PNG"))
        QMessageBox::critical(this, tr("Screenshot failed"), tr("Failed to save screenshot!"));
}

void MainWindow::recordGIF()
{
    static QString path;

    if (path.isEmpty())
    {
        // TODO: Use QTemporaryFile?
        path = QDir::tempPath() + QDir::separator() + QStringLiteral("firebird_tmp.gif");

        gif_start_recording(path.toStdString().c_str(), 3);
    }
    else
    {
        if (gif_stop_recording())
        {
            QString filename = QFileDialog::getSaveFileName(this, tr("Save Recording"), QString(), tr("GIF images (*.gif)"));
            if (filename.isEmpty())
                QFile(path).remove();
            else
            {
                QFile(filename).remove();
                QFile(path).rename(filename);
            }
        }
        else
            QMessageBox::warning(this, tr("Failed recording GIF"), tr("A failure occured during recording"));

        path = QString();
    }

    ui->actionRecord_GIF->setChecked(!path.isEmpty());
}

void MainWindow::launchIdaInstantDebugging()
{
    if (!the_qml_bridge || !the_qml_bridge->getGDBEnabled())
    {
        QMessageBox::warning(this, tr("GDB server disabled"),
                             tr("Enable the GDB server in settings before launching IDA."));
        return;
    }

    QString ida_path = settings ? settings->value(QStringLiteral("ida_binary_path")).toString() : QString();
    if (ida_path.isEmpty() || !QFileInfo::exists(ida_path))
    {
        ida_path = QFileDialog::getOpenFileName(this, tr("Select IDA executable"));
        if (ida_path.isEmpty())
            return;
        if (settings)
            settings->setValue(QStringLiteral("ida_binary_path"), ida_path);
    }

    QString last_input = settings ? settings->value(QStringLiteral("ida_last_input")).toString() : QString();
    QString input_path = QFileDialog::getOpenFileName(this, tr("Select IDA input file"), last_input);
    if (input_path.isEmpty())
    {
        const auto choice = QMessageBox::question(this, tr("No input file"),
                                                  tr("Launch IDA without an input file?"));
        if (choice != QMessageBox::Yes)
            return;
    }
    else if (settings)
    {
        settings->setValue(QStringLiteral("ida_last_input"), input_path);
    }

    const QString host = settings ? settings->value(QStringLiteral("ida_gdb_host"),
                                                   QStringLiteral("127.0.0.1")).toString()
                                  : QStringLiteral("127.0.0.1");
    const int port = the_qml_bridge->getGDBPort();

    const QString r_arg = QStringLiteral("-rgdb@%1:%2").arg(host).arg(port);
    QStringList args;
    args << r_arg;
    if (!input_path.isEmpty())
        args << input_path;

    auto *proc = new QProcess(this);
    proc->start(ida_path, args);
    if (!proc->waitForStarted())
    {
        QMessageBox::warning(this, tr("Launch failed"),
                             tr("Failed to launch IDA at %1").arg(ida_path));
        proc->deleteLater();
    }
}

void MainWindow::connectUSB()
{
    if (usblink_connected)
        usblink_queue_reset();
    else
        usblink_connect();

    usblinkChanged(false);
}

void MainWindow::usblinkChanged(bool state)
{
    ui->actionConnect->setText(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->actionConnect->setChecked(state);
    ui->buttonUSB->setToolTip(state ? tr("Disconnect USB") : tr("Connect USB"));
    ui->buttonUSB->setChecked(state);
}

void MainWindow::setExtLCD(bool state)
{
    if (state)
        lcd.show();
    else
        lcd.hide();

    ui->actionLCD_Window->setChecked(state);
}

bool MainWindow::resume()
{
    /* If there's no kit set, use the default kit */
    if (the_qml_bridge->getCurrentKitId() == -1)
        the_qml_bridge->useDefaultKit();

    applyQMLBridgeSettings();

    auto snapshot_path = the_qml_bridge->getSnapshotPath();
    if (!snapshot_path.isEmpty())
        return resumeFromPath(snapshot_path);
    else
    {
        QMessageBox::warning(this, tr("Can't resume"), tr("The current kit does not have a snapshot file configured"));
        return false;
    }
}

void MainWindow::suspend()
{
    auto snapshot_path = the_qml_bridge->getSnapshotPath();
    if (!snapshot_path.isEmpty())
        suspendToPath(snapshot_path);
    else
        QMessageBox::warning(this, tr("Can't suspend"), tr("The current kit does not have a snapshot file configured"));
}

void MainWindow::resumeFromFile()
{
    QString snapshot = QFileDialog::getOpenFileName(this, tr("Select snapshot to resume from"));
    if (!snapshot.isEmpty())
        resumeFromPath(snapshot);
}

void MainWindow::suspendToFile()
{
    QString snapshot = QFileDialog::getSaveFileName(this, tr("Select snapshot to suspend to"));
    if (!snapshot.isEmpty())
        suspendToPath(snapshot);
}

void MainWindow::saveFlash()
{
    flash_save_changes();
}

void MainWindow::createFlash()
{
    if (!flash_dialog)
        flash_dialog = flash_dialog_component->create();

    if (!flash_dialog)
        qWarning() << "Could not create flash dialog!";
    else
        flash_dialog->setProperty("visible", QVariant(true));
}

void MainWindow::setUIEditMode(bool e)
{
    settings->setValue(QStringLiteral("uiEditModeEnabled"), e);

    const auto dockChildren = content_window->findChildren<DockWidget *>();
    for (DockWidget *dw : dockChildren)
        dw->hideTitlebar(!e);
}

void MainWindow::showAbout()
{
    aboutDialog.show();
}

void MainWindow::isBusy(bool busy)
{
    if (busy)
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    else
        QApplication::restoreOverrideCursor();
}

void MainWindow::started(bool success)
{
    updateUIActionState(success);

    if (success)
        showStatusMsg(tr("Emulation started"));
    else
        QMessageBox::warning(this, tr("Could not start the emulation"), tr("Starting the emulation failed.\nAre the paths to boot1 and flash correct?"));
}

void MainWindow::resumed(bool success)
{
    updateUIActionState(success);

    if (success)
        showStatusMsg(tr("Emulation resumed from snapshot"));
    else
        QMessageBox::warning(this, tr("Could not resume"), tr("Resuming failed.\nTry to fix the issue and try again."));
}

void MainWindow::suspended(bool success)
{
    if (success)
        showStatusMsg(tr("Snapshot saved"));
    else
        QMessageBox::warning(this, tr("Could not suspend"), tr("Suspending failed.\nTry to fix the issue and try again."));

    if (close_after_suspend)
    {
        if (!success)
            close_after_suspend = false; // May try again
        else
            this->close();
    }
}

void MainWindow::stopped()
{
    updateUIActionState(false);
    showStatusMsg(tr("Emulation stopped"));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (config_dialog)
        config_dialog->setProperty("visible", false);

    if (flash_dialog)
        flash_dialog->setProperty("visible", false);

    if (!close_after_suspend &&
        settings->value(QStringLiteral("suspendOnClose")).toBool() && emu_thread.isRunning() && exiting == false)
    {
        close_after_suspend = true;
        qDebug("Suspending...");
        suspend();
        e->ignore();
        return;
    }

    if (emu_thread.isRunning() && !emu_thread.stop())
        qDebug("Terminating emulator thread failed.");

    QMainWindow::closeEvent(e);
}

void MainWindow::showStatusMsg(QString str)
{
    status_label.setText(str);
}

void MainWindow::kitDataChanged(QModelIndex, QModelIndex, QVector<int> roles)
{
    if (roles.contains(KitModel::NameRole))
    {
        refillKitMenus();

        // Need to update window title if kit is active
        updateWindowTitle();
    }
}

void MainWindow::kitAnythingChanged()
{
    if (the_qml_bridge->getKitModel()->rowCount() != ui->menuRestart_with_Kit->actions().size())
        refillKitMenus();
}

void MainWindow::currentKitChanged(const Kit &kit)
{
    (void)kit;
    updateWindowTitle();
}

void MainWindow::refillKitMenus()
{
    ui->menuRestart_with_Kit->clear();
    ui->menuBoot_Diags_with_Kit->clear();

    auto &&kit_model = the_qml_bridge->getKitModel();
    for (auto &&kit : kit_model->getKits())
    {
        QAction *action = ui->menuRestart_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, SIGNAL(triggered()), this, SLOT(startKit()));

        action = ui->menuBoot_Diags_with_Kit->addAction(kit.name);
        action->setData(kit.id);
        connect(action, SIGNAL(triggered()), this, SLOT(startKitDiags()));
    }
}

void MainWindow::updateWindowTitle()
{
    // Need to update window title if kit is active
    int kitIndex = the_qml_bridge->kitIndexForID(the_qml_bridge->getCurrentKitId());
    if (kitIndex >= 0)
    {
        auto name = the_qml_bridge->getKitModel()->getKits()[kitIndex].name;
        setWindowTitle(tr("Firebird Emu - %1").arg(name));
    }
    else
        setWindowTitle(tr("Firebird Emu"));
}

void MainWindow::applyQMLBridgeSettings()
{
    // Reload the current kit
    the_qml_bridge->useKit(the_qml_bridge->getCurrentKitId());

    emu_thread.port_gdb = the_qml_bridge->getGDBEnabled() ? the_qml_bridge->getGDBPort() : 0;
    emu_thread.port_rdbg = the_qml_bridge->getRDBEnabled() ? the_qml_bridge->getRDBPort() : 0;
}

void MainWindow::restart()
{
    /* If there's no kit set, use the default kit */
    if (the_qml_bridge->getCurrentKitId() == -1)
        the_qml_bridge->useDefaultKit();

    applyQMLBridgeSettings();

    if (emu_thread.boot1.isEmpty())
    {
        QMessageBox::critical(this, tr("No boot1 set"), tr("Before you can start the emulation, you have to select a proper boot1 file."));
        return;
    }

    if (emu_thread.flash.isEmpty())
    {
        QMessageBox::critical(this, tr("No flash image loaded"), tr("Before you can start the emulation, you have to load a proper flash file.\n"
                                                                    "You can create one via Flash->Create Flash in the menu."));
        return;
    }

    if (emu_thread.stop())
        emu_thread.start();
    else
        QMessageBox::warning(this, tr("Restart needed"), tr("Failed to restart emulator. Close and reopen this app.\n"));
}

void MainWindow::openConfiguration()
{
    if (!config_dialog)
        config_dialog = config_component->create();

    if (!config_dialog)
        qWarning() << "Could not create config dialog!";
    else
        config_dialog->setProperty("visible", QVariant(true));
}

void MainWindow::startKit()
{
    auto action = qobject_cast<QAction *>(sender());
    if (!action)
    {
        qWarning() << "Received signal from invalid sender";
        return;
    }

    auto kit_id = static_cast<unsigned int>(action->data().toInt());
    the_qml_bridge->setCurrentKit(kit_id);
    boot_order = ORDER_BOOT2;
    restart();
}

void MainWindow::startKitDiags()
{
    auto action = qobject_cast<QAction *>(sender());
    if (!action)
    {
        qWarning() << "Received signal from invalid sender";
        return;
    }

    auto kit_id = static_cast<unsigned int>(action->data().toInt());
    the_qml_bridge->setCurrentKit(kit_id);
    boot_order = ORDER_DIAGS;
    restart();
}

void MainWindow::xmodemSend()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Select file to send"));
    if (filename.isEmpty())
        return;

    std::string path = filename.toStdString();
    xmodem_send(path.c_str());
}

void MainWindow::switchToMobileUI()
{
    switchUIMode(true);
}

bool QQuickWidgetLessBroken::event(QEvent *event)
{
    if (event->type() == QEvent::Leave)
    {
        QMouseEvent ev(QEvent::MouseMove, QPointF(0, 0), QPointF(0, 0), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QQuickWidget::event(&ev);
    }

    return QQuickWidget::event(event);
}
