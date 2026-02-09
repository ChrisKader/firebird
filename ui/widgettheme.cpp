#include "widgettheme.h"

#include <QPalette>
#include <QWidget>
#include <QString>

#include "app/qmlbridge.h"

const WidgetTheme &currentWidgetTheme()
{
    static const WidgetTheme dark_theme{
        /* Base palette */
        QColor(QStringLiteral("#181818")),   // window
        QColor(QStringLiteral("#1e1e1e")),   // surface
        QColor(QStringLiteral("#202020")),   // surfaceAlt
        QColor(QStringLiteral("#252526")),   // dock
        QColor(QStringLiteral("#1b1b1c")),   // dockTitle
        QColor(QStringLiteral("#333333")),   // border
        QColor(QStringLiteral("#007acc")),   // accent
        QColor(QStringLiteral("#d4d4d4")),   // text
        QColor(QStringLiteral("#858585")),   // textMuted
        QColor(QStringLiteral("#264f78")),   // selection
        QColor(QStringLiteral("#ffffff")),   // selectionText
        QColor(QStringLiteral("#202020")),   // statusBg

        /* Syntax highlighting */
        QColor(QStringLiteral("#569CD6")),   // syntaxMnemonic (blue, VS Code keyword)
        QColor(QStringLiteral("#C586C0")),   // syntaxBranch (purple, VS Code control flow)
        QColor(QStringLiteral("#4EC9B0")),   // syntaxRegister (teal, VS Code type)
        QColor(QStringLiteral("#B5CEA8")),   // syntaxImmediate (green, VS Code number)
        QColor(QStringLiteral("#858585")),   // syntaxAddress (muted gray)
        QColor(QStringLiteral("#DCDCAA")),   // syntaxSymbol (yellow, VS Code function)

        /* Markers */
        QColor(QStringLiteral("#E51400")),   // markerBreakpoint (bright red)
        QColor(QStringLiteral("#4EC9B0")),   // markerWatchRead (teal)
        QColor(QStringLiteral("#CE9178")),   // markerWatchWrite (orange)
        QColor(QStringLiteral("#3A3A00")),   // markerPcBg (subtle dark yellow)
        QColor(QStringLiteral("#FFFF00")),   // markerPcArrow (bright yellow)

        /* Changed-value highlight */
        QColor(QStringLiteral("#FF6B6B")),   // changedValue (softer red)

        /* Frame separator */
        QColor(255, 255, 255, 25),           // frameSeparator

        /* ANSI terminal overrides */
        QColor(QStringLiteral("#858585")),   // ansiBlack (visible gray on dark bg)
        QColor(QStringLiteral("#DCDCAA")),   // ansiYellow (soft yellow on dark bg)

        /* Activity Bar */
        QColor(QStringLiteral("#333333")),   // activityBarBg
        QColor(QStringLiteral("#858585")),   // activityBarFg
        QColor(QStringLiteral("#007acc")),   // activityBarActiveBorder (accent)
        QColor(QStringLiteral("#ffffff")),   // activityBarActiveFg
        QColor(QStringLiteral("#007acc")),   // activityBarBadgeBg (accent)
        QColor(QStringLiteral("#ffffff")),   // activityBarBadgeFg

        /* Panel tabs */
        QColor(QStringLiteral("#007acc")),   // panelTabActiveBorder
        QColor(QStringLiteral("#d4d4d4")),   // panelTabActiveFg
        QColor(QStringLiteral("#858585")),   // panelTabInactiveFg

        /* Console tags */
        QColor(QStringLiteral("#4EC9B0")),   // consoleTagUart (teal, like syntaxRegister)
        QColor(QStringLiteral("#C586C0")),   // consoleTagDebug (purple, like syntaxBranch)
        QColor(QStringLiteral("#DCDCAA")),   // consoleTagSys (yellow, like syntaxSymbol)

        /* Misc UI */
        QColor(128, 128, 128, 80),           // scrollbarThumb
        QColor(QStringLiteral("#333333")),   // inputBorder
        QColor(QStringLiteral("#007acc")),   // inputActiveBorder
    };

    static const WidgetTheme light_theme{
        /* Base palette */
        QColor(QStringLiteral("#f5f5f5")),   // window
        QColor(QStringLiteral("#ffffff")),   // surface
        QColor(QStringLiteral("#ededed")),   // surfaceAlt
        QColor(QStringLiteral("#f2f2f2")),   // dock
        QColor(QStringLiteral("#e6e6e6")),   // dockTitle
        QColor(QStringLiteral("#c4c4c4")),   // border
        QColor(QStringLiteral("#0066b8")),   // accent
        QColor(QStringLiteral("#1f1f1f")),   // text
        QColor(QStringLiteral("#5e5e5e")),   // textMuted
        QColor(QStringLiteral("#cce6ff")),   // selection
        QColor(QStringLiteral("#1a1a1a")),   // selectionText
        QColor(QStringLiteral("#e9e9e9")),   // statusBg

        /* Syntax highlighting */
        QColor(QStringLiteral("#0000CC")),   // syntaxMnemonic (classic blue)
        QColor(QStringLiteral("#CC0000")),   // syntaxBranch (red)
        QColor(QStringLiteral("#008000")),   // syntaxRegister (green)
        QColor(QStringLiteral("#800080")),   // syntaxImmediate (purple)
        QColor(QStringLiteral("#808080")),   // syntaxAddress (gray)
        QColor(QStringLiteral("#008080")),   // syntaxSymbol (teal)

        /* Markers */
        QColor(QStringLiteral("#CC2222")),   // markerBreakpoint
        QColor(QStringLiteral("#22AA22")),   // markerWatchRead
        QColor(QStringLiteral("#CC8800")),   // markerWatchWrite
        QColor(QStringLiteral("#FFFFA0")),   // markerPcBg (yellow highlight)
        QColor(QStringLiteral("#000000")),   // markerPcArrow (black)

        /* Changed-value highlight */
        QColor(QStringLiteral("#CC0000")),   // changedValue (red)

        /* Frame separator */
        QColor(0, 0, 0, 25),                // frameSeparator

        /* ANSI terminal overrides */
        QColor(QStringLiteral("#1f1f1f")),   // ansiBlack (dark on light bg)
        QColor(QStringLiteral("#B8860B")),   // ansiYellow (dark goldenrod on light bg)

        /* Activity Bar */
        QColor(QStringLiteral("#2c2c2c")),   // activityBarBg
        QColor(QStringLiteral("#858585")),   // activityBarFg
        QColor(QStringLiteral("#0066b8")),   // activityBarActiveBorder (accent)
        QColor(QStringLiteral("#1f1f1f")),   // activityBarActiveFg
        QColor(QStringLiteral("#0066b8")),   // activityBarBadgeBg (accent)
        QColor(QStringLiteral("#ffffff")),   // activityBarBadgeFg

        /* Panel tabs */
        QColor(QStringLiteral("#0066b8")),   // panelTabActiveBorder
        QColor(QStringLiteral("#1f1f1f")),   // panelTabActiveFg
        QColor(QStringLiteral("#5e5e5e")),   // panelTabInactiveFg

        /* Console tags */
        QColor(QStringLiteral("#008080")),   // consoleTagUart (teal)
        QColor(QStringLiteral("#AF00DB")),   // consoleTagDebug (purple)
        QColor(QStringLiteral("#795E26")),   // consoleTagSys (olive)

        /* Misc UI */
        QColor(128, 128, 128, 80),           // scrollbarThumb
        QColor(QStringLiteral("#c4c4c4")),   // inputBorder
        QColor(QStringLiteral("#0066b8")),   // inputActiveBorder
    };

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

void setWidgetBackground(QWidget *w, const QColor &color, const QColor &text)
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
