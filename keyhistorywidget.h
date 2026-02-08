#ifndef KEYHISTORYWIDGET_H
#define KEYHISTORYWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

class KeyHistoryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KeyHistoryWidget(QWidget *parent = nullptr);

public slots:
    void addEntry(const QString &keyName, bool pressed);
    void clear();

private:
    QPlainTextEdit *m_textEdit = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    int m_maxEntries = 200;
};

#endif // KEYHISTORYWIDGET_H
