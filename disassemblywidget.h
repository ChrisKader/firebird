#ifndef DISASSEMBLYWIDGET_H
#define DISASSEMBLYWIDGET_H

#include <QAbstractScrollArea>
#include <QFont>
#include <QVector>
#include <QLineEdit>
#include <QToolBar>

#include "core/debug_api.h"

class DisassemblyWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit DisassemblyWidget(QWidget *parent = nullptr);

    void setIconFont(const QFont &font) { m_iconFont = font; }

public slots:
    void refresh();
    void goToAddress(uint32_t addr);
    void goToPC();

signals:
    void breakpointToggled(uint32_t addr, bool set);
    void addressSelected(uint32_t addr);
    void debugCommand(QString cmd);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Line {
        uint32_t addr;
        uint32_t raw;
        QString  mnemonic;
        QString  operands;
        uint8_t  size;
        bool     is_thumb;
        bool     has_exec_bp;
        bool     has_read_wp;
        bool     has_write_wp;
        bool     is_pc;
    };

    void updateLines();
    void updateScrollBar();
    int lineHeight() const;
    int visibleLineCount() const;

    void parseMnemonicOperands(const QString &text, QString &mnemonic, QString &operands);
    bool isBranchMnemonic(const QString &mnem) const;

    QVector<Line> m_lines;
    uint32_t m_baseAddr = 0;
    uint32_t m_pcAddr = 0;
    int m_selectedLine = -1;

    QFont m_monoFont;
    QFont m_iconFont;
    QLineEdit *m_addrEdit = nullptr;
    QToolBar *m_toolbar = nullptr;

    static constexpr int MARGIN_WIDTH = 24;
    static constexpr int NUM_LINES = 128;
};

#endif // DISASSEMBLYWIDGET_H
