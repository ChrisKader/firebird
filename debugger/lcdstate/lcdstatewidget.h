#ifndef LCDSTATEWIDGET_H
#define LCDSTATEWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QTimer>

class LCDStateWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LCDStateWidget(QWidget *parent = nullptr);

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
