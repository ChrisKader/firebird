#ifndef CYCLECOUNTERWIDGET_H
#define CYCLECOUNTERWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <stdint.h>

class CycleCounterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CycleCounterWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void resetCounter();

private:
    QLabel *m_totalLabel = nullptr;
    QLabel *m_deltaLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QPushButton *m_resetBtn = nullptr;

    int64_t m_baselineCycles = 0;
};

#endif // CYCLECOUNTERWIDGET_H
