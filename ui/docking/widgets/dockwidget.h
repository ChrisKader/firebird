#ifndef DOCKWIDGET_H
#define DOCKWIDGET_H

#include <QDockWidget>
#include <QLabel>
#include <QAction>
#include <QtGlobal>

/* QDockWidget subclass that shows a thin title label when docked
 * (for visual separation) and the default OS title bar when floating.
 * Optionally shows a Material icon from the toggle action. */

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include "ui/docking/widgets/kdockwidget.h"
using DockWidget = KDockWidget;
#else
class DockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit DockWidget(const QString &title, QWidget *parent = nullptr,
                         Qt::WindowFlags flags = Qt::WindowFlags());

    explicit DockWidget(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags())
        : DockWidget({}, parent, flags) {}

    ~DockWidget() {}

    void setWidget(QWidget *widget);

    void hideTitlebar(bool b);
    void applyThinBarStyle();

public slots:
    void refreshTitlebar();

private:
    bool hide_titlebar_if_possible = false;
    QWidget *m_thinBarWidget = nullptr;
    QLabel *m_thinIcon = nullptr;
    QLabel *m_thinTitleBar = nullptr;
};
#endif

#endif // DOCKWIDGET_H
