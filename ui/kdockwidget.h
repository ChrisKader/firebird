#ifndef KDOCKWIDGET_H
#define KDOCKWIDGET_H

#include <QIcon>
#include <QDockWidget>
#include <QAction>

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
    #include <kddockwidgets/DockWidget.h>
#else
    #include "ui/dockwidget.h"
#endif

#ifdef FIREBIRD_USE_KDDOCKWIDGETS
class KDockWidget : public KDDockWidgets::QtWidgets::DockWidget
{
    Q_OBJECT

public:
    explicit KDockWidget(const QString &uniqueName,
                         const QString &title,
                         QWidget *parent = nullptr);

    void applyThinTitlebar(bool enabled = true);
    void setDockIcon(const QIcon &icon);

    // Compatibility helpers used by existing DockWidget-oriented call sites.
    QAction *toggleViewAction() const { return toggleAction(); }
    void hideTitlebar(bool);
    void applyThinBarStyle();
    void refreshTitlebar();
    void setAllowedAreas(Qt::DockWidgetAreas);
    void setFeatures(QDockWidget::DockWidgetFeatures);
    void setFloating(bool b) { KDDockWidgets::QtWidgets::DockWidget::setFloating(b); }

signals:
    void topLevelChanged(bool floating);
    void visibilityChanged(bool visible);
    void dockLocationChanged(Qt::DockWidgetArea area);
};
#else
class KDockWidget : public DockWidget
{
    Q_OBJECT

public:
    explicit KDockWidget(const QString &uniqueName,
                         const QString &title,
                         QWidget *parent = nullptr);

    void applyThinTitlebar(bool enabled = true);
    void setDockIcon(const QIcon &icon);
};
#endif

#endif // KDOCKWIDGET_H
