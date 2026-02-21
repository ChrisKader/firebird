#ifndef KEYHISTORYWIDGET_H
#define KEYHISTORYWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QElapsedTimer>
#include <QVector>

#include "ui/docking/state/dockstate.h"

class KeyHistoryWidget : public QWidget, public DockStateSerializable
{
    Q_OBJECT

public:
    explicit KeyHistoryWidget(QWidget *parent = nullptr);
    QJsonObject serializeState() const override;
    void restoreState(const QJsonObject &state) override;

public slots:
    void addEntry(const QString &keyName, bool pressed);
    void clear();

private slots:
    void applyFilter();
    void exportHistory();

private:
    struct Entry {
        qint64  elapsed_ms;
        QString keyName;
        bool    pressed;
    };

    void rebuildDisplay();

    QPlainTextEdit *m_textEdit = nullptr;
    QToolBar *m_toolbar = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_statsLabel = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;

    QVector<Entry> m_entries;
    QElapsedTimer m_elapsed;
    int m_maxEntries = 500;
    int m_totalPresses = 0;
    QSet<QString> m_uniqueKeys;
};

#endif // KEYHISTORYWIDGET_H
