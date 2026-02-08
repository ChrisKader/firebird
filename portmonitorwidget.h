#ifndef PORTMONITORWIDGET_H
#define PORTMONITORWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <stdint.h>

class PortMonitorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PortMonitorWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void goToAddress(uint32_t addr);

private slots:
    void addPort();
    void removePort();
    void addCommonPorts();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    void addPortEntry(uint32_t addr, const QString &name);

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_commonBtn = nullptr;
};

#endif // PORTMONITORWIDGET_H
