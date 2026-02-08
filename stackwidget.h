#ifndef STACKWIDGET_H
#define STACKWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <stdint.h>

class StackWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StackWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    void goToAddress(uint32_t addr);

private:
    QTreeWidget *m_tree = nullptr;
    static constexpr int MAX_STACK_WORDS = 64;
};

#endif // STACKWIDGET_H
