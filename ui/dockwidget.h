#ifndef DOCKWIDGET_H
#define DOCKWIDGET_H

#include <QDockWidget>
#include <QLabel>
#include <QAction>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/DockWidget.h>
#endif

/* QDockWidget subclass that shows a thin title label when docked
 * (for visual separation) and the default OS title bar when floating.
 * Optionally shows a Material icon from the toggle action. */

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
class DockWidget : public KDDockWidgets::QtWidgets::DockWidget
{
    Q_OBJECT
public:
    explicit DockWidget(const QString &uniqueName, QWidget *parent = nullptr,
                        Qt::WindowFlags flags = Qt::WindowFlags());

    explicit DockWidget(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags())
        : DockWidget({}, parent, flags) {}

    ~DockWidget() {}

    void setWidget(QWidget *widget);

    void hideTitlebar(bool b);
    void applyThinBarStyle();
    void refreshTitlebar();

    // Compatibility helpers used by existing QDockWidget-oriented code paths.
    QAction *toggleViewAction() const { return toggleAction(); }
    void setAllowedAreas(Qt::DockWidgetAreas) {}
    void setFeatures(QDockWidget::DockWidgetFeatures) {}
    void setFloating(bool b) { KDDockWidgets::QtWidgets::DockWidget::setFloating(b); }

signals:
    void topLevelChanged(bool floating);
    void visibilityChanged(bool visible);
    void dockLocationChanged(Qt::DockWidgetArea area);
};
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
