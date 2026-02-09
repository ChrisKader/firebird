#ifndef TIMERMONITORWIDGET_H
#define TIMERMONITORWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QTimer>

class TimerMonitorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimerMonitorWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onAutoRefreshChanged(int index);

private:
    void addClassicTimers();
    void addCxTimers();
    void addWatchdog();

    QTreeWidget *m_tree = nullptr;
    QComboBox *m_refreshCombo = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

#endif // TIMERMONITORWIDGET_H
