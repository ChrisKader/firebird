#ifndef HEXVIEWWIDGET_H
#define HEXVIEWWIDGET_H

#include <QAbstractScrollArea>
#include <QFont>
#include <QLineEdit>
#include <QToolBar>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <stdint.h>

class HexViewWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit HexViewWidget(QWidget *parent = nullptr);

    enum SearchType { SearchHex = 0, SearchAscii, SearchUint32LE, SearchUint32BE };

public slots:
    void refresh();
    void goToAddress(uint32_t addr);

signals:
    void addressSelected(uint32_t addr);
    void gotoDisassembly(uint32_t addr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    int lineHeight() const;
    int visibleLineCount() const;
    void updateScrollBar();
    void doSearch(bool forward);
    void doFindAll();
    QByteArray buildSearchPattern() const;
    uint32_t selectedAddress() const;

    uint32_t m_baseAddr = 0;
    int m_selectedOffset = -1;
    bool m_showAscii = true;

    static constexpr int BYTES_PER_ROW = 16;
    static constexpr int TOTAL_ROWS = 0x10000;

    QFont m_monoFont;
    QLineEdit *m_addrEdit = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_searchTypeCombo = nullptr;
    QCheckBox *m_asciiToggle = nullptr;
    QToolBar *m_toolbar = nullptr;
    QListWidget *m_findResultsList = nullptr;

    uint8_t m_data[BYTES_PER_ROW * 64];
    int m_dataRows = 0;

    /* Editing state */
    int m_editOffset = -1;
    int m_editNibble = 0; /* 0 = high nibble, 1 = low nibble */
};

#endif // HEXVIEWWIDGET_H
