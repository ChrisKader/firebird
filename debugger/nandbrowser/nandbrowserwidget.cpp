#include "nandbrowserwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>

#include "core/emu.h"
#include "core/flash.h"

NandBrowserWidget::NandBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    auto *openBtn = new QToolButton(m_toolbar);
    openBtn->setText(tr("Open..."));
    openBtn->setToolTip(tr("Open NAND image file"));
    m_toolbar->addWidget(openBtn);
    connect(openBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, tr("Open NAND Image"), QString(),
            tr("NAND images (*.nand *.img *.bin);;All files (*)"));
        if (!path.isEmpty())
            openImage(path);
    });

    auto *currentBtn = new QToolButton(m_toolbar);
    currentBtn->setText(tr("Current Flash"));
    currentBtn->setToolTip(tr("Browse the currently loaded flash image"));
    m_toolbar->addWidget(currentBtn);
    connect(currentBtn, &QToolButton::clicked, this, &NandBrowserWidget::openCurrentFlash);

    auto *refreshBtn = new QToolButton(m_toolbar);
    refreshBtn->setText(tr("Refresh"));
    m_toolbar->addWidget(refreshBtn);
    connect(refreshBtn, &QToolButton::clicked, this, &NandBrowserWidget::refresh);

    layout->addWidget(m_toolbar);

    m_infoLabel = new QLabel(tr("No image loaded"), this);
    m_infoLabel->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_infoLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Name"), tr("Offset"), tr("Size")});
    m_tree->setColumnCount(3);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);
}

void NandBrowserWidget::openImage(const QString &path)
{
    m_currentPath = path;
    NandInfo info = readNandImage(path);
    populateTree(info);
}

void NandBrowserWidget::openCurrentFlash()
{
    QString flashPath = QString::fromStdString(path_flash);
    if (flashPath.isEmpty()) {
        m_infoLabel->setText(tr("No flash image loaded"));
        return;
    }
    openImage(flashPath);
}

void NandBrowserWidget::refresh()
{
    if (!m_currentPath.isEmpty())
        openImage(m_currentPath);
}

NandBrowserWidget::NandInfo NandBrowserWidget::readNandImage(const QString &path)
{
    NandInfo info;
    info.path = path;
    info.isLarge = false;
    info.product = 0;
    info.totalSize = 0;

    FILE *f = fopen(path.toLocal8Bit().constData(), "rb");
    if (!f) {
        info.hwType = tr("(could not open)");
        return info;
    }

    /* Determine size */
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    info.totalSize = fileSize > 0 ? (size_t)fileSize : 0;

    /* Classify: classic (33 MB range) vs large CX/CX2 (132 MB range) */
    info.isLarge = (info.totalSize > 40 * 1024 * 1024);

    uint16_t pageSize = info.isLarge ? 2048 : 512;

    /* Read product ID from manufacturing page (manuf data at offset 0) */
    uint8_t manufPage[2048];
    memset(manufPage, 0, sizeof(manufPage));
    size_t readSize = pageSize < sizeof(manufPage) ? pageSize : sizeof(manufPage);
    if (fread(manufPage, 1, readSize, f) == readSize) {
        /* Product ID is typically at offset 0x20 in the manuf page on CX/CX2 */
        if (info.isLarge) {
            info.product = manufPage[0x20] | ((uint16_t)manufPage[0x21] << 8);
        }
    }

    /* Read HW type string using existing flash API */
    fseek(f, 0, SEEK_SET);
    std::string hwType = flash_read_type(f);
    info.hwType = hwType.empty() ? tr("Unknown") : QString::fromStdString(hwType);

    /* Partition layout estimates.
     * These are approximate offsets based on typical TI-Nspire NAND layout. */
    uint32_t sparePerPage = info.isLarge ? 64 : 16;
    uint32_t fullPageSize = pageSize + sparePerPage;
    uint32_t pagesPerBlock = info.isLarge ? 64 : 32;
    uint32_t blockSize = fullPageSize * pagesPerBlock;

    info.partitions[0] = {tr("Manufacturing"), 0, blockSize};
    info.partitions[1] = {tr("Boot2"), blockSize, blockSize * (info.isLarge ? 16 : 32)};

    size_t boot2End = info.partitions[1].offset + info.partitions[1].size;
    info.partitions[2] = {tr("Bootdata"), boot2End, blockSize * 2};

    size_t bootdataEnd = info.partitions[2].offset + info.partitions[2].size;
    info.partitions[3] = {tr("Diags"), bootdataEnd, blockSize * (info.isLarge ? 8 : 16)};

    size_t diagsEnd = info.partitions[3].offset + info.partitions[3].size;
    size_t remaining = info.totalSize > diagsEnd ? info.totalSize - diagsEnd : 0;
    info.partitions[4] = {tr("Filesystem"), diagsEnd, remaining};

    fclose(f);
    return info;
}

static QString formatSize(size_t bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

void NandBrowserWidget::populateTree(const NandInfo &info)
{
    m_tree->clear();

    QString sizeStr = formatSize(info.totalSize);
    m_infoLabel->setText(QStringLiteral("%1 - %2 (%3)")
                             .arg(QFileInfo(info.path).fileName(), info.hwType, sizeStr));

    auto *root = new QTreeWidgetItem(m_tree);
    root->setText(0, QFileInfo(info.path).fileName());
    root->setText(1, QStringLiteral("0x000000"));
    root->setText(2, sizeStr);
    root->setExpanded(true);

    static const QChar prefixes[] = {
        QLatin1Char('M'), QLatin1Char('B'), QLatin1Char('D'),
        QLatin1Char('X'), QLatin1Char('F')
    };

    for (int i = 0; i < 5; i++) {
        auto *item = new QTreeWidgetItem(root);
        item->setText(0, QStringLiteral("[%1] %2").arg(prefixes[i]).arg(info.partitions[i].name));
        item->setText(1, QStringLiteral("0x%1").arg(info.partitions[i].offset, 6, 16, QLatin1Char('0')));
        item->setText(2, formatSize(info.partitions[i].size));
    }

    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(1);
}
