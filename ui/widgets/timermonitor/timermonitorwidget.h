#ifndef TIMERMONITORWIDGET_H
#define TIMERMONITORWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QTimer>

#include "ui/docking/dockstate.h"

class TimerMonitorWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit TimerMonitorWidget(QWidget *parent = nullptr);
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

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
