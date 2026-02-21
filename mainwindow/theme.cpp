#include "mainwindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QToolButton>

#include "debugger/dockmanager.h"
#include "ui/widgets/debugger/disassembly/disassemblywidget.h"
#include "ui/widgets/debugger/hexview/hexviewwidget.h"
#include "ui/materialicons.h"
#include "ui/widgettheme.h"
#include "ui_mainwindow.h"

static QString normalize_button_tooltip_text(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return text;
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    text = text.simplified();
    return text;
}

static bool looks_like_icon_glyph(const QString &text)
{
    if (text.size() != 1)
        return false;
    const ushort cp = text.at(0).unicode();
    return cp >= 0xE000u && cp <= 0xF8FFu; // Unicode Private Use Area
}

static QString tooltip_from_object_name(QString object_name)
{
    if (object_name.isEmpty())
        return {};

    if (object_name.startsWith(QStringLiteral("button")))
        object_name.remove(0, 6);
    else if (object_name.startsWith(QStringLiteral("btn")))
        object_name.remove(0, 3);
    else if (object_name.startsWith(QStringLiteral("action")))
        object_name.remove(0, 6);

    object_name.replace(QLatin1Char('_'), QLatin1Char(' '));
    object_name.replace(QRegularExpression(QStringLiteral("([a-z0-9])([A-Z])")),
                        QStringLiteral("\\1 \\2"));
    object_name = normalize_button_tooltip_text(object_name);
    if (object_name.isEmpty())
        return {};

    object_name[0] = object_name.at(0).toUpper();
    return object_name;
}

void MainWindow::applyButtonUxDefaults(QWidget *root)
{
    if (!root)
        return;

    const auto buttons = root->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (!button)
            continue;

        if (button->toolTip().trimmed().isEmpty()) {
            QString tip;

            if (auto *toolButton = qobject_cast<QToolButton *>(button)) {
                if (QAction *action = toolButton->defaultAction()) {
                    tip = normalize_button_tooltip_text(action->toolTip());
                    if (tip.isEmpty())
                        tip = normalize_button_tooltip_text(action->text());
                }
            }

            if (tip.isEmpty())
                tip = normalize_button_tooltip_text(button->text());
            if (looks_like_icon_glyph(tip))
                tip.clear();
            if (tip.isEmpty())
                tip = normalize_button_tooltip_text(button->accessibleName());
            if (tip.isEmpty())
                tip = tooltip_from_object_name(button->objectName());

            if (!tip.isEmpty())
                button->setToolTip(tip);
        }
    }
}

void MainWindow::applyWidgetTheme()
{
    const WidgetTheme &theme = currentWidgetTheme();

    /* Fusion is the only Qt style that fully respects qApp->setPalette().
     * The macOS native style ignores palette for most widgets.
     * CEmu uses the same approach. */
    static bool fusionSet = false;
    if (!fusionSet) {
        qApp->setStyle(QStringLiteral("Fusion"));
        fusionSet = true;
    }

    /* Build palette and apply globally. Fusion handles the rest. */
    QPalette pal;
    applyPaletteColors(pal, theme);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, theme.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Text, theme.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, theme.textMuted);
    pal.setColor(QPalette::Mid, theme.border);
    pal.setColor(QPalette::Dark, theme.border);
    pal.setColor(QPalette::Light, theme.surfaceAlt);
    pal.setColor(QPalette::Midlight, theme.surfaceAlt);
    pal.setColor(QPalette::Shadow, theme.window);
    qApp->setPalette(pal);

    const QColor hoverTop = theme.surfaceAlt.lighter(110);
    const QColor hoverBottom = theme.surfaceAlt.darker(104);
    const QColor pressedTop = theme.surfaceAlt.darker(108);
    const QColor pressedBottom = theme.surfaceAlt.darker(118);
    const QString sharedButtonUx = QStringLiteral(
        "QPushButton:hover, QToolButton:hover {"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %2, stop:1 %3);"
        "}"
        "QPushButton:pressed, QToolButton:pressed, QPushButton:checked, QToolButton:checked {"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %4, stop:1 %5);"
        "}"
    ).arg(theme.accent.name(),
          hoverTop.name(),
          hoverBottom.name(),
          pressedTop.name(),
          pressedBottom.name());

    /* KDDockWidgets recommends avoiding broad stylesheets for dock internals.
     * Keep richer dock/tab CSS only on the legacy non-KDD path. */
    if (content_window) {
#ifndef FIREBIRD_USE_KDDOCKWIDGETS
        QString ss = QStringLiteral(
            /* Tab bar styling (bottom and right dock areas) */
            "QTabBar::tab {"
            "  background: %1;"
            "  color: %2;"
            "  padding: 4px 12px;"
            "  border: none;"
            "  border-bottom: 2px solid transparent;"
            "}"
            "QTabBar::tab:selected {"
            "  color: %3;"
            "  border-bottom: 2px solid %4;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  color: %3;"
            "}"
            /* Thin scrollbars */
            "QScrollBar:vertical {"
            "  width: 10px; background: transparent; margin: 0;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: %5; border-radius: 4px; min-height: 20px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: rgba(128,128,128,140);"
            "}"
            "QScrollBar:horizontal {"
            "  height: 10px; background: transparent; margin: 0;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: %5; border-radius: 4px; min-width: 20px;"
            "}"
            "QScrollBar::handle:horizontal:hover {"
            "  background: rgba(128,128,128,140);"
            "}"
            "QScrollBar::add-line, QScrollBar::sub-line {"
            "  height: 0; width: 0;"
            "}"
            "QScrollBar::add-page, QScrollBar::sub-page {"
            "  background: transparent;"
            "}"
            /* Splitter handle styling */
            "QSplitter::handle {"
            "  background: %6;"
            "}"
            "QSplitter::handle:hover {"
            "  background: %7;"
            "}"
            /* Input field focus border */
            "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {"
            "  border: 1px solid %7;"
            "}"
        ).arg(
            theme.dock.name(),                // %1 tab bg
            theme.panelTabInactiveFg.name(),  // %2 inactive tab text
            theme.panelTabActiveFg.name(),    // %3 active tab text
            theme.panelTabActiveBorder.name(),// %4 active tab border
            theme.scrollbarThumb.name(),      // %5 scrollbar thumb
            theme.border.name(),              // %6 splitter
            theme.accent.name()               // %7 accent
        ) + sharedButtonUx;
        content_window->setStyleSheet(ss);
#else
        content_window->setStyleSheet(QString());
#endif
        applyButtonUxDefaults(content_window);
    }

    /* The outer QMainWindow has no docks of its own; suppress the Fusion-style
     * separator lines that Qt draws at each dock-area boundary.  Target only
     * the outer window (objectName "MainWindow") so content_window's dock
     * resize handles remain functional. */
    const QString outerWindowStyle = QStringLiteral(
        "QMainWindow#MainWindow::separator { width: 0; height: 0; }"
        "QToolBar#headerToolBar { border: none; }"
    );
#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    // Keep KDD path styles scoped to local widgets instead of root-window CSS.
    setStyleSheet(QString());
    if (ui->headerBar)
        ui->headerBar->setStyleSheet(sharedButtonUx);
    if (status_bar_tray)
        status_bar_tray->setStyleSheet(sharedButtonUx);
    if (m_dock_controls && m_dock_controls->widget())
        m_dock_controls->widget()->setStyleSheet(sharedButtonUx);
#else
    setStyleSheet(outerWindowStyle + sharedButtonUx);
#endif
    applyButtonUxDefaults(this);

    /* Refresh dock icons (color may have changed with theme) and thin title bars */
    if (m_debugDocks)
        m_debugDocks->refreshIcons();
    if (content_window) {
        const auto dockChildren = content_window->findChildren<DockWidget *>();
        for (DockWidget *dw : dockChildren) {
            dw->applyThinBarStyle();
            dw->refreshTitlebar();  // pick up new icon pixmaps
        }
    }

    /* Also refresh menu action icons */
    {
        using namespace MaterialIcons;
        const QColor fg = palette().color(QPalette::WindowText);
        auto mi = [&](ushort cp) { return fromCodepoint(material_icon_font, cp, fg); };

        if (ui->actionRestart)       ui->actionRestart->setIcon(mi(CP::Play));
        if (ui->actionReset)         ui->actionReset->setIcon(mi(CP::Refresh));
        if (ui->actionDebugger)      ui->actionDebugger->setIcon(mi(CP::BugReport));
        if (ui->actionConfiguration) ui->actionConfiguration->setIcon(mi(CP::Settings));
        if (ui->actionPause)         ui->actionPause->setIcon(mi(CP::Pause));
        if (ui->actionScreenshot)    ui->actionScreenshot->setIcon(mi(CP::Screenshot));
        if (ui->actionConnect)       ui->actionConnect->setIcon(mi(CP::USB));
        if (ui->actionRecord_GIF)    ui->actionRecord_GIF->setIcon(mi(CP::Image));
        if (ui->actionLCD_Window)    ui->actionLCD_Window->setIcon(mi(CP::Display));
        if (ui->actionResume)        ui->actionResume->setIcon(mi(CP::Play));
        if (ui->actionSuspend)       ui->actionSuspend->setIcon(mi(CP::Save));
        if (ui->actionSave)          ui->actionSave->setIcon(mi(CP::Save));
        if (ui->actionCreate_flash)  ui->actionCreate_flash->setIcon(mi(CP::Add));
        if (ui->refreshButton) {
            ui->refreshButton->setIcon(mi(CP::Refresh));
            ui->refreshButton->setText(QString());
            ui->refreshButton->setToolTip(tr("Refresh file list"));
        }
    }

    /* Force repaint on custom-painted widgets (they read theme colors directly) */
    if (m_debugDocks && m_debugDocks->disassembly())
        m_debugDocks->disassembly()->viewport()->update();
    if (m_debugDocks && m_debugDocks->hexView())
        m_debugDocks->hexView()->viewport()->update();
}
