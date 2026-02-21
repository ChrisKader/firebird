#ifndef MMUVIEWERWIDGET_H
#define MMUVIEWERWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <stdint.h>

class MMUViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MMUViewerWidget(QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onAutoRefreshChanged(int index);
    void onL1ItemClicked(QTreeWidgetItem *item, int column);
    void onTranslate();

private:
    void populateCP15();
    void populateL1Table();
    void populateL2Table(uint32_t l1_desc, uint32_t va_base);
    uint32_t readPhys32(uint32_t paddr);

    static QString decodeFaultStatus(uint32_t fsr);
    static QString decodeAP(uint32_t ap);
    static QString decodeDomainAccess(uint32_t dacr, int domain);

    /* Auto-refresh */
    QComboBox *m_refreshCombo = nullptr;
    QTimer *m_refreshTimer = nullptr;

    /* CP15 tree */
    QTreeWidget *m_cp15Tree = nullptr;

    /* L1 page table tree */
    QTreeWidget *m_l1Tree = nullptr;

    /* L2 page table tree */
    QTreeWidget *m_l2Tree = nullptr;

    /* VA -> PA translation */
    QLineEdit *m_vaInput = nullptr;
    QLabel *m_paOutput = nullptr;
};

#endif // MMUVIEWERWIDGET_H
