#ifndef KDOCKWIDGET_H
#define KDOCKWIDGET_H

#include <QIcon>

#include "ui/dockwidget.h"

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

#endif // KDOCKWIDGET_H
