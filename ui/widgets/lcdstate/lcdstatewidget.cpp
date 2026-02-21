#include "lcdstatewidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>

extern "C" {
#include "core/emu.h"
#include "core/misc.h"
}

#include "core/debug/debug_api.h"
#include "ui/theme/widgettheme.h"

LCDStateWidget::LCDStateWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    /* Auto-refresh toolbar */
    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(tr("Refresh:"), this));
    m_refreshCombo = new QComboBox(this);
    m_refreshCombo->addItems({tr("Manual"), tr("100 ms"), tr("500 ms"), tr("1 s")});
    topBar->addWidget(m_refreshCombo);
    topBar->addStretch();
    layout->addLayout(topBar);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &LCDStateWidget::refresh);
    connect(m_refreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LCDStateWidget::onAutoRefreshChanged);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Register"), tr("Value"), tr("Decoded")});
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_tree->setFont(mono);

    layout->addWidget(m_tree);
}

QJsonObject LCDStateWidget::serializeState() const
{
    QJsonObject state;
    if (m_refreshCombo)
        state.insert(QStringLiteral("refreshIndex"), m_refreshCombo->currentIndex());
    return state;
}

void LCDStateWidget::restoreState(const QJsonObject &state)
{
    if (!m_refreshCombo)
        return;
    const int idx = state.value(QStringLiteral("refreshIndex")).toInt(m_refreshCombo->currentIndex());
    if (idx >= 0 && idx < m_refreshCombo->count())
        m_refreshCombo->setCurrentIndex(idx);
}

void LCDStateWidget::onAutoRefreshChanged(int index)
{
    m_refreshTimer->stop();
    switch (index) {
    case 1: m_refreshTimer->start(100); break;
    case 2: m_refreshTimer->start(500); break;
    case 3: m_refreshTimer->start(1000); break;
    default: break;
    }
}

void LCDStateWidget::refresh()
{
    if (!isVisible())
        return;

    m_tree->clear();

    const WidgetTheme &theme = currentWidgetTheme();

    auto hex = [](uint32_t v, int w = 8) {
        return QStringLiteral("0x%1").arg(v, w, 16, QLatin1Char('0'));
    };

    auto readReg = [](uint32_t offset) -> uint32_t {
        uint32_t val = 0;
        debug_peek_reg(0xC0000000 + offset, &val);
        return val;
    };

    /* Applied to all items after the tree is populated (see below). */

    /* Timing registers */
    auto *timingGroup = new QTreeWidgetItem(m_tree);
    timingGroup->setText(0, QStringLiteral("Timing"));

    uint32_t t0 = readReg(0x000);
    uint32_t t1 = readReg(0x004);
    uint32_t t2 = readReg(0x008);

    {
        auto *item = new QTreeWidgetItem(timingGroup);
        item->setText(0, QStringLiteral("HorizTiming (0x000)"));
        item->setText(1, hex(t0));
        int hbp = (t0 >> 24 & 0xFF) + 1;
        int hfp = (t0 >> 16 & 0xFF) + 1;
        int hsw = (t0 >> 8 & 0xFF) + 1;
        int ppl = (t2 >> 16 & 0x3FF) + 1;
        item->setText(2, QStringLiteral("BP=%1 FP=%2 SW=%3 PPL=%4")
                         .arg(hbp).arg(hfp).arg(hsw).arg(ppl));
    }
    {
        auto *item = new QTreeWidgetItem(timingGroup);
        item->setText(0, QStringLiteral("VertTiming (0x004)"));
        item->setText(1, hex(t1));
        int vbp = (t1 >> 24 & 0xFF);
        int vfp = (t1 >> 16 & 0xFF);
        int vsw = (t1 >> 10 & 0x3F) + 1;
        int lpp = (t1 & 0x3FF) + 1;
        item->setText(2, QStringLiteral("BP=%1 FP=%2 SW=%3 LPP=%4")
                         .arg(vbp).arg(vfp).arg(vsw).arg(lpp));
    }
    {
        auto *item = new QTreeWidgetItem(timingGroup);
        item->setText(0, QStringLiteral("ClkTiming (0x008)"));
        item->setText(1, hex(t2));
        int pcd_hi = (t2 >> 27) & 0x1F;
        int pcd_lo = t2 & 0x1F;
        int pcd = (pcd_hi << 5) | pcd_lo;
        if (!(t2 & (1 << 26)))
            pcd += 2;
        item->setText(2, QStringLiteral("PCD=%1 BCD=%2").arg(pcd).arg((t2 >> 26) & 1));
    }

    /* Framebuffer */
    auto *fbGroup = new QTreeWidgetItem(m_tree);
    fbGroup->setText(0, QStringLiteral("Framebuffer"));

    uint32_t upbase = readReg(0x010);
    uint32_t lpbase = readReg(0x014);

    {
        auto *item = new QTreeWidgetItem(fbGroup);
        item->setText(0, QStringLiteral("UPBASE (0x010)"));
        item->setText(1, hex(upbase));
        item->setText(2, QStringLiteral("Upper panel DMA address"));
    }
    {
        auto *item = new QTreeWidgetItem(fbGroup);
        item->setText(0, QStringLiteral("LPBASE (0x014)"));
        item->setText(1, hex(lpbase));
    }

    /* Control */
    auto *ctrlGroup = new QTreeWidgetItem(m_tree);
    ctrlGroup->setText(0, QStringLiteral("Control"));

    /* PL110: control at 0x01C, intmask at 0x018
     * PL111 (CX/CX2): control at 0x018, intmask at 0x01C */
    uint32_t ctrl = readReg(emulate_cx ? 0x018 : 0x01C);
    uint32_t intmask = readReg(emulate_cx ? 0x01C : 0x018);

    {
        auto *item = new QTreeWidgetItem(ctrlGroup);
        item->setText(0, QStringLiteral("Control"));
        item->setText(1, hex(ctrl));
        bool enabled = ctrl & 1;
        int bpp_code = (ctrl >> 1) & 7;
        static const int bpp_values[] = {1, 2, 4, 8, 16, 24, 16, 16};
        int bpp = bpp_values[bpp_code];
        bool bgr = (ctrl >> 8) & 1;
        item->setText(2, QStringLiteral("EN=%1 BPP=%2 BGR=%3")
                         .arg(enabled ? QStringLiteral("Y") : QStringLiteral("N"))
                         .arg(bpp)
                         .arg(bgr ? QStringLiteral("Y") : QStringLiteral("N")));
    }

    /* Resolution summary */
    {
        int ppl = (t2 >> 16 & 0x3FF) + 1;
        int lpp = (t1 & 0x3FF) + 1;
        auto *item = new QTreeWidgetItem(ctrlGroup);
        item->setText(0, QStringLiteral("Resolution"));
        item->setText(2, QStringLiteral("%1 x %2").arg(ppl).arg(lpp));
    }

    /* Interrupts */
    {
        auto *item = new QTreeWidgetItem(ctrlGroup);
        item->setText(0, QStringLiteral("IntMask"));
        item->setText(1, hex(intmask, 2));
    }
    {
        uint32_t intstat = readReg(0x020);
        auto *item = new QTreeWidgetItem(ctrlGroup);
        item->setText(0, QStringLiteral("IntStatus (0x020)"));
        item->setText(1, hex(intstat, 2));
    }

    /* Contrast (hdq1w register, not part of PL11x) */
    {
        auto *item = new QTreeWidgetItem(ctrlGroup);
        item->setText(0, QStringLiteral("Contrast"));
        item->setText(1, QStringLiteral("%1").arg(hdq1w.lcd_contrast));
        bool off = (hdq1w.lcd_contrast == 0);
        item->setText(2, off ? QStringLiteral("LCD off")
                             : QStringLiteral("%1%%").arg(hdq1w.lcd_contrast * 100 / LCD_CONTRAST_MAX));
    }

    /* Cursor */
    auto *cursorGroup = new QTreeWidgetItem(m_tree);
    cursorGroup->setText(0, QStringLiteral("Cursor"));

    {
        uint32_t curCtrl = readReg(0xC00);
        auto *item = new QTreeWidgetItem(cursorGroup);
        item->setText(0, QStringLiteral("CursorCtrl (0xC00)"));
        item->setText(1, hex(curCtrl, 2));
        item->setText(2, (curCtrl & 1) ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
    }
    {
        uint32_t curXY = readReg(0xC10);
        auto *item = new QTreeWidgetItem(cursorGroup);
        item->setText(0, QStringLiteral("CursorXY (0xC10)"));
        item->setText(1, hex(curXY));
        int cx = curXY & 0xFFF;
        int cy = (curXY >> 16) & 0xFFF;
        item->setText(2, QStringLiteral("X=%1 Y=%2").arg(cx).arg(cy));
    }

    m_tree->expandAll();

    /* Color all items: register names teal, values green, decoded yellow. */
    std::function<void(QTreeWidgetItem *)> colorAll = [&](QTreeWidgetItem *item) {
        if (item->childCount() == 0) {
            item->setForeground(0, QBrush(theme.syntaxRegister));
            item->setForeground(1, QBrush(theme.syntaxImmediate));
            item->setForeground(2, QBrush(theme.syntaxSymbol));
        } else {
            item->setForeground(0, QBrush(theme.syntaxMnemonic));
        }
        for (int i = 0; i < item->childCount(); i++)
            colorAll(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        colorAll(m_tree->topLevelItem(i));
}
