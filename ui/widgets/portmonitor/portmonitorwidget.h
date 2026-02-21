#ifndef PORTMONITORWIDGET_H
#define PORTMONITORWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QTimer>
#include <QComboBox>
#include <QCheckBox>
#include <QHash>
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
    void exportCSV();
    void onAutoRefreshChanged(int index);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void contextMenuAt(const QPoint &pos);

private:
    void addPortEntry(uint32_t addr, const QString &name,
                      QTreeWidgetItem *parent = nullptr);
    QTreeWidgetItem *findOrCreateGroup(const QString &group);
    QString decodePeripheralValue(uint32_t addr, uint32_t val) const;

    QTreeWidget *m_tree = nullptr;
    QToolBar *m_toolbar = nullptr;
    QComboBox *m_refreshCombo = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;

    /* Previous values for change highlighting */
    QHash<uint32_t, uint32_t> m_prevValues;
};

#endif // PORTMONITORWIDGET_H
