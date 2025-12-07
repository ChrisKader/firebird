#ifndef DOCKWIDGET_H
#define DOCKWIDGET_H

#include <QDockWidget>
#include <QLabel>
#include <QToolButton>
#include <QFont>

/* This class augments QDockWidget with the function
 * to hide the titlebar of non-floating docks. */

class DockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit DockWidget(const QString &title, QWidget *parent = nullptr,
                         Qt::WindowFlags flags = Qt::WindowFlags());

    explicit DockWidget(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags())
        : DockWidget({}, parent, flags) {}

    ~DockWidget() {}

    void hideTitlebar(bool b);
    void applyButtonStyle(const QFont &iconFont);

public slots:
    void refreshTitlebar();
    void updateCustomTitle(const QString &title);

protected slots:
    void buildCustomTitlebar();

protected:
    bool hide_titlebar_if_possible = false;
    QWidget *custom_titlebar = nullptr;
    QLabel *title_label = nullptr;
    QToolButton *float_button = nullptr;
    QToolButton *close_button = nullptr;
};

#endif // DOCKWIDGET_H
