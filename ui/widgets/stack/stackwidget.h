#ifndef STACKWIDGET_H
#define STACKWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QSpinBox>
#include <QLabel>
#include <QHash>
#include <stdint.h>

class StackWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StackWidget(QWidget *parent = nullptr);

    void setSymbols(const QHash<uint32_t, QString> *symbols) { m_symbols = symbols; }

public slots:
    void refresh();

signals:
    void goToAddress(uint32_t addr);
    void gotoDisassembly(uint32_t addr);

private:
    void contextMenuAt(const QPoint &pos);
    bool looksLikeReturnAddr(uint32_t val) const;

    QTreeWidget *m_tree = nullptr;
    QLabel *m_spLabel = nullptr;
    QSpinBox *m_depthSpin = nullptr;

    const QHash<uint32_t, QString> *m_symbols = nullptr;

    static constexpr int DEFAULT_STACK_WORDS = 64;
};

#endif // STACKWIDGET_H
