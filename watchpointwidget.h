#ifndef WATCHPOINTWIDGET_H
#define WATCHPOINTWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <stdint.h>

class WatchpointWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WatchpointWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void goToAddress(uint32_t addr);

private slots:
    void addWatchpoint();
    void removeWatchpoint();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
};

#endif // WATCHPOINTWIDGET_H
