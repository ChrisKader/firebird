#ifndef WIDGETTHEME_H
#define WIDGETTHEME_H

#include <QColor>

struct WidgetTheme
{
    /* Base palette */
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

    /* Syntax highlighting (disassembly, hex view) */
    QColor syntaxMnemonic;
    QColor syntaxBranch;
    QColor syntaxRegister;
    QColor syntaxImmediate;
    QColor syntaxAddress;
    QColor syntaxSymbol;

    /* Markers */
    QColor markerBreakpoint;
    QColor markerWatchRead;
    QColor markerWatchWrite;
    QColor markerPcBg;
    QColor markerPcArrow;

    /* Changed-value highlight */
    QColor changedValue;

    /* Frame separator (stack widget) */
    QColor frameSeparator;

    /* ANSI terminal color overrides */
    QColor ansiBlack;
    QColor ansiYellow;

    /* Activity Bar */
    QColor activityBarBg;
    QColor activityBarFg;
    QColor activityBarActiveBorder;
    QColor activityBarActiveFg;
    QColor activityBarBadgeBg;
    QColor activityBarBadgeFg;

    /* Panel tabs */
    QColor panelTabActiveBorder;
    QColor panelTabActiveFg;
    QColor panelTabInactiveFg;

    /* Console tags */
    QColor consoleTagUart;
    QColor consoleTagDebug;
    QColor consoleTagSys;

    /* Misc UI */
    QColor scrollbarThumb;
    QColor inputBorder;
    QColor inputActiveBorder;
};

const WidgetTheme &currentWidgetTheme();

class QPalette;
void applyPaletteColors(QPalette &pal, const WidgetTheme &theme);

class QWidget;
void setWidgetBackground(QWidget *w, const QColor &color, const QColor &text = QColor());

#endif // WIDGETTHEME_H
