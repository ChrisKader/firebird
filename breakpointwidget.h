#ifndef BREAKPOINTWIDGET_H
#define BREAKPOINTWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <stdint.h>

class BreakpointWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BreakpointWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void goToAddress(uint32_t addr);

private slots:
    void addBreakpoint();
    void removeBreakpoint();
    void removeAll();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_removeAllBtn = nullptr;
};

#endif // BREAKPOINTWIDGET_H
