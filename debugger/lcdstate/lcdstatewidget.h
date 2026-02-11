#ifndef LCDSTATEWIDGET_H
#define LCDSTATEWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QTimer>

#include "ui/dockstate.h"

class LCDStateWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit LCDStateWidget(QWidget *parent = nullptr);
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

public slots:
    void refresh();

private slots:
    void onAutoRefreshChanged(int index);

private:
    QTreeWidget *m_tree = nullptr;
    QComboBox *m_refreshCombo = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

#endif // LCDSTATEWIDGET_H
