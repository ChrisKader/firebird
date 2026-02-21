#ifndef DOCKBACKEND_H
#define DOCKBACKEND_H

#include <QMainWindow>
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

} // namespace DockBackend

#endif // DOCKBACKEND_H
