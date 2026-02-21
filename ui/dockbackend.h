#ifndef DOCKBACKEND_H
#define DOCKBACKEND_H

#include <QList>
#include <QMainWindow>
#include <QObject>
#include <QSize>

#include "ui/dockwidget.h"

namespace DockBackend {

void addDockWidgetCompat(QMainWindow *window,
                         DockWidget *dock,
                         Qt::DockWidgetArea area,
                         DockWidget *relativeTo = nullptr,
                         bool startHidden = false,
                         bool preserveCurrentSize = false,
                         const QSize &preferredSize = QSize());

void addDockWidgetCompatAnyRelative(QMainWindow *window,
                                    DockWidget *dock,
                                    Qt::DockWidgetArea area,
                                    QObject *relativeToAny = nullptr,
                                    bool startHidden = false,
                                    bool preserveCurrentSize = false,
                                    const QSize &preferredSize = QSize());

void tabifyDockWidgetCompat(QMainWindow *window,
                            DockWidget *first,
                            DockWidget *second);

void removeDockWidgetCompat(QMainWindow *window,
                            DockWidget *dock);

void splitDockWidgetCompat(QMainWindow *window,
                           DockWidget *first,
                           DockWidget *second,
                           Qt::Orientation orientation);

void resizeDocksCompat(QMainWindow *window,
                       const QList<DockWidget *> &docks,
                       const QList<int> &sizes,
                       Qt::Orientation orientation);

} // namespace DockBackend

#endif // DOCKBACKEND_H
