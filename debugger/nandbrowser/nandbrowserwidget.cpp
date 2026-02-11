#include "nandbrowserwidget.h"
#include "nandfileeditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QFont>
#include <QStyle>
#include <QFile>
#include <QTimer>

#include "app/emuthread.h"
#include "core/emu.h"
#include "core/flash.h"
#include "core/nand_fs.h"

// -------------------- Construction --------------------

NandBrowserWidget::NandBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    auto *currentBtn = new QToolButton(m_toolbar);
    currentBtn->setText(tr("Current Flash"));
    currentBtn->setToolTip(tr("Browse the currently loaded flash image"));
    m_toolbar->addWidget(currentBtn);
    connect(currentBtn, &QToolButton::clicked, this, &NandBrowserWidget::openCurrentFlash);

    auto *refreshBtn = new QToolButton(m_toolbar);
    refreshBtn->setText(tr("Refresh"));
    m_toolbar->addWidget(refreshBtn);
    connect(refreshBtn, &QToolButton::clicked, this, &NandBrowserWidget::refresh);

    m_toolbar->addSeparator();

    // Search controls in toolbar
    auto *searchLabel = new QLabel(tr(" Search: "), m_toolbar);
    m_toolbar->addWidget(searchLabel);

    m_searchEdit = new QLineEdit(m_toolbar);
    m_searchEdit->setPlaceholderText(tr("ASCII string..."));
    m_searchEdit->setMaximumWidth(200);
    m_toolbar->addWidget(m_searchEdit);

    m_searchScope = new QComboBox(m_toolbar);
    m_searchScope->addItem(tr("All"));
    m_toolbar->addWidget(m_searchScope);

    auto *searchBtn = new QToolButton(m_toolbar);
    searchBtn->setText(tr("Go"));
    m_toolbar->addWidget(searchBtn);
    connect(searchBtn, &QToolButton::clicked, this, &NandBrowserWidget::onSearchTriggered);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &NandBrowserWidget::onSearchTriggered);

    layout->addWidget(m_toolbar);

    // Info label
    m_infoLabel = new QLabel(tr("No flash loaded. Click \"Current Flash\" to browse."), this);
    m_infoLabel->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_infoLabel);

    // Vertical splitter: top = main content, bottom = search results
    m_vertSplitter = new QSplitter(Qt::Vertical, this);

    // Horizontal splitter: left = tree, right = content
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left: partition/filesystem tree
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Name"), tr("Offset"), tr("Size")});
    m_tree->setColumnCount(3);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemClicked, this, &NandBrowserWidget::onTreeItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &NandBrowserWidget::onTreeItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &NandBrowserWidget::onTreeContextMenu);
    m_splitter->addWidget(m_tree);

    // Right: stacked widget with different views
    m_rightPane = new QStackedWidget(this);

    // Welcome page
    m_welcomePage = new QWidget(this);
    auto *welcomeLayout = new QVBoxLayout(m_welcomePage);
    auto *welcomeLabel = new QLabel(tr("Select a partition or file to view its contents."), m_welcomePage);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(welcomeLabel);
    m_rightPane->addWidget(m_welcomePage); // index 0

    // Page table
    m_pageTable = new QTableWidget(this);
    m_pageTable->setColumnCount(5);
    m_pageTable->setHorizontalHeaderLabels({tr("Page"), tr("Block"), tr("Offset"), tr("Status"), tr("Preview")});
    m_pageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pageTable->horizontalHeader()->setStretchLastSection(true);
    m_pageTable->setAlternatingRowColors(true);
    connect(m_pageTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto *offItem = m_pageTable->item(row, 2);
        if (!offItem) return;
        size_t offset = offItem->data(Qt::UserRole).toULongLong();
        showHexView(offset, nand.metrics.page_size);
    });
    m_rightPane->addWidget(m_pageTable); // index 1

    // Hex view
    m_hexView = new QPlainTextEdit(this);
    m_hexView->setReadOnly(true);
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    m_hexView->setFont(mono);
    m_hexView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rightPane->addWidget(m_hexView); // index 2

    // Text preview
    m_textPreview = new QPlainTextEdit(this);
    m_textPreview->setReadOnly(true);
    m_textPreview->setFont(mono);
    m_textPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_rightPane->addWidget(m_textPreview); // index 3

    m_splitter->addWidget(m_rightPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);

    m_vertSplitter->addWidget(m_splitter);

    // Search results table (bottom)
    m_searchResults = new QTableWidget(this);
    m_searchResults->setColumnCount(3);
    m_searchResults->setHorizontalHeaderLabels({tr("Offset"), tr("Partition"), tr("Context")});
    m_searchResults->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_searchResults->horizontalHeader()->setStretchLastSection(true);
    m_searchResults->setAlternatingRowColors(true);
    m_searchResults->hide();
    connect(m_searchResults, &QTableWidget::itemDoubleClicked, this, &NandBrowserWidget::onSearchResultClicked);
    m_vertSplitter->addWidget(m_searchResults);

    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 1);

    layout->addWidget(m_vertSplitter, 1);
}

// -------------------- Public slots --------------------

void NandBrowserWidget::openCurrentFlash()
{
    const uint8_t *data = flash_get_nand_data();
    if (!data)
    {
        m_infoLabel->setText(tr("No flash image loaded"));
        return;
    }

    // Check if the emulator is running and needs to be paused.
    // If already paused, skip the delay and don't unpause afterward.
    bool needsPause = emu_thread.isRunning() && !emu_thread.isPaused();

    if (needsPause)
    {
        m_infoLabel->setText(tr("Pausing emulator..."));
        emu_thread.setPaused(true);
    }
    else
    {
        m_infoLabel->setText(tr("Loading..."));
    }

    // Defer heavy work so the emu thread has time to reach its pause loop.
    // No delay needed if already paused or not running.
    QTimer::singleShot(needsPause ? 200 : 0, this, [this, needsPause]() {
        doLoad();
        if (needsPause)
            emu_thread.setPaused(false);
    });
}

void NandBrowserWidget::refresh()
{
    openCurrentFlash();
}

void NandBrowserWidget::doLoad()
{
    const uint8_t *data = flash_get_nand_data();
    if (!data)
    {
        m_infoLabel->setText(tr("No flash image loaded"));
        return;
    }

    m_partitions.clear();
    m_filesystem.reset();
    m_fsValid = false;
    m_fsPartIndex = -1;

    size_t totalSize = flash_get_nand_size();
    m_infoLabel->setText(tr("Flash: %1 (%2 pages, page_size=0x%3)")
                              .arg(formatSize(totalSize))
                              .arg(nand.metrics.num_pages)
                              .arg(nand.metrics.page_size, 0, 16));

    m_searchScope->clear();
    m_searchScope->addItem(tr("All"));

    populatePartitions();
}

// -------------------- Partition population --------------------

void NandBrowserWidget::populatePartitions()
{
    m_tree->clear();
    m_rightPane->setCurrentIndex(0);

    flash_partition_info parts[20];
    int count = flash_get_partitions(parts, 20);

    m_partitions.resize(count);
    for (int i = 0; i < count; i++)
        m_partitions[i] = parts[i];

    for (int i = 0; i < count; i++)
    {
        auto *item = new QTreeWidgetItem(m_tree);
        QString name = QString::fromUtf8(parts[i].name);
        item->setText(0, name);
        item->setText(1, formatOffset(parts[i].offset));
        item->setText(2, formatSize(parts[i].size));
        item->setData(0, RoleType, QStringLiteral("partition"));
        item->setData(0, RoleOffset, QVariant::fromValue((qulonglong)parts[i].offset));
        item->setData(0, RoleSize, QVariant::fromValue((qulonglong)parts[i].size));
        item->setData(0, RolePartIndex, i);

        // Add to search scope
        m_searchScope->addItem(name);

        // Try to parse filesystem for the Filesystem partition
        if (name.contains(QStringLiteral("Filesystem"), Qt::CaseInsensitive))
        {
            m_fsPartIndex = i;
            populateFilesystemTree(item, i);
        }
    }

    // Only expand the top two levels (partitions + first-level dirs).
    // expandAll() with thousands of items freezes Qt's layout engine.
    m_tree->expandToDepth(1);
    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(1);
}

void NandBrowserWidget::populateFilesystemTree(QTreeWidgetItem *fsItem, int partIndex)
{
    const uint8_t *data = flash_get_nand_data();
    size_t nand_size = flash_get_nand_size();
    if (!data || partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    m_filesystem = std::make_unique<NandFilesystem>(
        nand_fs_parse(data, nand_size,
                      m_partitions[partIndex].offset,
                      m_partitions[partIndex].size,
                      nand.metrics));

    m_fsValid = m_filesystem->valid;
    if (!m_fsValid)
    {
        auto *noFs = new QTreeWidgetItem(fsItem);
        QString errMsg = m_filesystem->error.empty()
            ? tr("(Could not parse filesystem)")
            : tr("(Parse error: %1)").arg(QString::fromStdString(m_filesystem->error));
        noFs->setText(0, errMsg);
        return;
    }

    // Add root children
    std::set<uint32_t> visited;
    visited.insert(m_filesystem->root_inode);
    addFsChildren(fsItem, *m_filesystem, m_filesystem->root_inode, 0, visited);
}

void NandBrowserWidget::addFsChildren(QTreeWidgetItem *parentItem, const NandFilesystem &fs,
                                       uint32_t parent_inode, int depth,
                                       std::set<uint32_t> &visited)
{
    if (depth > 32) return;

    auto kids = fs.children(parent_inode);

    // Sort: directories first, then alphabetically
    std::sort(kids.begin(), kids.end(), [](const NandFsNode *a, const NandFsNode *b) {
        if (a->type != b->type)
            return a->type == NandFsNode::DIR_NODE;
        return a->name < b->name;
    });

    for (const auto *node : kids)
    {
        auto *item = new QTreeWidgetItem(parentItem);
        item->setText(0, QString::fromStdString(node->name));
        item->setText(2, node->type == NandFsNode::DIR_NODE ? QString() : formatSize(node->size));

        if (node->type == NandFsNode::DIR_NODE)
        {
            item->setData(0, RoleType, QStringLiteral("fsdir"));
            item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            // Skip already-visited inodes to prevent cycles in corrupt filesystems
            if (!visited.count(node->inode_num))
            {
                visited.insert(node->inode_num);
                addFsChildren(item, fs, node->inode_num, depth + 1, visited);
            }
        }
        else
        {
            item->setData(0, RoleType, QStringLiteral("fsfile"));
            item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        }
        item->setData(0, RoleFsPath, QString::fromStdString(node->full_path));
        item->setData(0, RoleInodeNum, node->inode_num);
    }
}

// -------------------- Tree interaction --------------------

void NandBrowserWidget::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    QString type = item->data(0, RoleType).toString();

    if (type == QStringLiteral("partition"))
    {
        int idx = item->data(0, RolePartIndex).toInt();
        showPartitionPages(idx);
    }
    else if (type == QStringLiteral("fsfile"))
    {
        // Show file hex preview
        const NandFsNode *node = findFsNode(item->data(0, RoleFsPath).toString());
        if (node)
        {
            auto data = nand_fs_read_file(*m_filesystem, *node,
                                           flash_get_nand_data(), flash_get_nand_size());
            if (data.size() <= 256 * 1024) // Only show hex for files up to 256KB
                showTextPreview(data, QString::fromStdString(node->full_path));
        }
    }
}

void NandBrowserWidget::onTreeItemDoubleClicked(QTreeWidgetItem *item, int)
{
    QString type = item->data(0, RoleType).toString();

    if (type == QStringLiteral("partition"))
    {
        size_t offset = item->data(0, RoleOffset).toULongLong();
        showHexView(offset, std::min((size_t)nand.metrics.page_size * 4, (size_t)4096));
    }
    else if (type == QStringLiteral("fsfile"))
    {
        const NandFsNode *node = findFsNode(item->data(0, RoleFsPath).toString());
        if (!node) return;

        QString name = QString::fromStdString(node->name);
        bool isText = name.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".lua"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".cfg"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".log"), Qt::CaseInsensitive) ||
                      name.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive);

        if (isText)
            editFile(node);
        else
            extractFile(node);
    }
}

void NandBrowserWidget::onTreeContextMenu(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item) return;

    QString type = item->data(0, RoleType).toString();
    QMenu menu(this);

    if (type == QStringLiteral("partition"))
    {
        int idx = item->data(0, RolePartIndex).toInt();
        menu.addAction(tr("View Pages"), this, [this, idx]() { showPartitionPages(idx); });
        menu.addAction(tr("View Hex (first page)"), this, [this, idx]() {
            if (idx >= 0 && idx < (int)m_partitions.size())
                showHexView(m_partitions[idx].offset, nand.metrics.page_size);
        });
        menu.addSeparator();
        menu.addAction(tr("Export Partition..."), this, [this, idx]() { exportPartition(idx); });
        menu.addAction(tr("Import Partition..."), this, [this, idx]() { importPartition(idx); });
    }
    else if (type == QStringLiteral("fsfile"))
    {
        const NandFsNode *node = findFsNode(item->data(0, RoleFsPath).toString());
        if (node)
        {
            menu.addAction(tr("View as Text"), this, [this, node]() {
                auto data = nand_fs_read_file(*m_filesystem, *node,
                                               flash_get_nand_data(), flash_get_nand_size());
                showTextPreview(data, QString::fromStdString(node->full_path));
            });
            menu.addAction(tr("View as Hex"), this, [this, node]() {
                auto data = nand_fs_read_file(*m_filesystem, *node,
                                               flash_get_nand_data(), flash_get_nand_size());
                // Show hex of file data
                showHexView(0, 0); // Clear first
                // Build hex from file data
                QString hex;
                size_t len = data.size();
                for (size_t i = 0; i < len; i += 16)
                {
                    hex += QStringLiteral("%1: ").arg(i, 8, 16, QLatin1Char('0'));
                    for (size_t j = 0; j < 16; j++)
                    {
                        if (i + j < len)
                            hex += QStringLiteral("%1 ").arg(data[i + j], 2, 16, QLatin1Char('0'));
                        else
                            hex += QStringLiteral("   ");
                        if (j == 7) hex += QLatin1Char(' ');
                    }
                    hex += QStringLiteral(" |");
                    for (size_t j = 0; j < 16 && i + j < len; j++)
                    {
                        char c = data[i + j];
                        hex += (c >= 0x20 && c < 0x7F) ? QChar(QLatin1Char(c)) : QChar(QLatin1Char('.'));
                    }
                    hex += QStringLiteral("|\n");
                }
                m_hexView->setPlainText(hex);
                m_rightPane->setCurrentWidget(m_hexView);
            });
            menu.addSeparator();
            menu.addAction(tr("Extract to..."), this, [this, node]() { extractFile(node); });
            menu.addAction(tr("Edit..."), this, [this, node]() { editFile(node); });
        }
    }
    else if (type == QStringLiteral("fsdir"))
    {
        // Directory context menu
        menu.addAction(tr("Expand All"), this, [item]() { item->setExpanded(true); });
    }

    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// -------------------- Partition page view --------------------

void NandBrowserWidget::showPartitionPages(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    size_t offset = m_partitions[partIndex].offset;
    size_t size = m_partitions[partIndex].size;
    uint32_t pageSize = nand.metrics.page_size;
    uint32_t pagesPerBlock = 1u << nand.metrics.log2_pages_per_block;

    m_pageTable->setRowCount(0);
    uint32_t numPages = (uint32_t)(size / pageSize);
    // Limit display to first 1024 pages for performance
    uint32_t displayPages = std::min(numPages, 1024u);

    m_pageTable->setRowCount(displayPages);
    size_t nand_size = flash_get_nand_size();

    for (uint32_t i = 0; i < displayPages; i++)
    {
        size_t pageOff = offset + (size_t)i * pageSize;
        uint32_t block = i / pagesPerBlock;

        auto *pageItem = new QTableWidgetItem(QString::number(i));
        auto *blockItem = new QTableWidgetItem(QString::number(block));
        auto *offsetItem = new QTableWidgetItem(formatOffset(pageOff));
        offsetItem->setData(Qt::UserRole, QVariant::fromValue((qulonglong)pageOff));

        // Check if page is erased
        bool erased = true;
        if (pageOff + 16 <= nand_size)
        {
            for (int b = 0; b < 16; b++)
            {
                if (data[pageOff + b] != 0xFF) { erased = false; break; }
            }
        }

        auto *statusItem = new QTableWidgetItem(erased ? tr("Erased") : tr("Data"));
        if (erased)
            statusItem->setForeground(Qt::gray);

        // Preview: first 16 bytes as hex
        QString preview;
        if (pageOff + 16 <= nand_size)
        {
            for (int b = 0; b < 16; b++)
                preview += QStringLiteral("%1 ").arg(data[pageOff + b], 2, 16, QLatin1Char('0'));
        }
        auto *previewItem = new QTableWidgetItem(preview.trimmed());
        if (erased)
            previewItem->setForeground(Qt::gray);

        m_pageTable->setItem(i, 0, pageItem);
        m_pageTable->setItem(i, 1, blockItem);
        m_pageTable->setItem(i, 2, offsetItem);
        m_pageTable->setItem(i, 3, statusItem);
        m_pageTable->setItem(i, 4, previewItem);
    }

    if (numPages > displayPages)
    {
        m_infoLabel->setText(tr("Showing first %1 of %2 pages for %3")
                                  .arg(displayPages).arg(numPages)
                                  .arg(QString::fromUtf8(m_partitions[partIndex].name)));
    }

    m_pageTable->resizeColumnsToContents();
    m_rightPane->setCurrentWidget(m_pageTable);
}

// -------------------- Hex view --------------------

void NandBrowserWidget::showHexView(size_t offset, size_t size)
{
    const uint8_t *data = flash_get_nand_data();
    size_t nand_size = flash_get_nand_size();
    if (!data || offset >= nand_size)
    {
        m_hexView->setPlainText(tr("(No data)"));
        m_rightPane->setCurrentWidget(m_hexView);
        return;
    }

    if (offset + size > nand_size)
        size = nand_size - offset;

    // Limit to 64KB for display
    size_t displaySize = std::min(size, (size_t)(64 * 1024));

    QString hex;
    hex.reserve((int)(displaySize / 16 + 1) * 80);

    for (size_t i = 0; i < displaySize; i += 16)
    {
        hex += QStringLiteral("%1: ").arg(offset + i, 8, 16, QLatin1Char('0'));

        for (size_t j = 0; j < 16; j++)
        {
            if (i + j < displaySize)
                hex += QStringLiteral("%1 ").arg(data[offset + i + j], 2, 16, QLatin1Char('0'));
            else
                hex += QStringLiteral("   ");
            if (j == 7) hex += QLatin1Char(' ');
        }

        hex += QStringLiteral(" |");
        for (size_t j = 0; j < 16 && i + j < displaySize; j++)
        {
            char c = data[offset + i + j];
            hex += (c >= 0x20 && c < 0x7F) ? QChar(QLatin1Char(c)) : QChar(QLatin1Char('.'));
        }
        hex += QStringLiteral("|\n");
    }

    if (displaySize < size)
        hex += QStringLiteral("\n... (%1 more bytes)\n").arg(size - displaySize);

    m_hexView->setPlainText(hex);
    m_rightPane->setCurrentWidget(m_hexView);
}

// -------------------- Text preview --------------------

void NandBrowserWidget::showTextPreview(const std::vector<uint8_t> &data, const QString &title)
{
    QString text = QString::fromUtf8(reinterpret_cast<const char *>(data.data()), (int)data.size());
    m_textPreview->setPlainText(text);
    m_rightPane->setCurrentWidget(m_textPreview);

    // Also update info
    m_infoLabel->setText(tr("Preview: %1 (%2 bytes)").arg(title).arg(data.size()));
}

// -------------------- Export / Import --------------------

void NandBrowserWidget::exportPartition(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    QString name = QString::fromUtf8(m_partitions[partIndex].name);
    QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Partition"),
        name + QStringLiteral(".bin"),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }

    f.write(reinterpret_cast<const char *>(data + m_partitions[partIndex].offset),
            m_partitions[partIndex].size);
    f.close();

    m_infoLabel->setText(tr("Exported %1 (%2) to %3")
                              .arg(name, formatSize(m_partitions[partIndex].size), filename));
}

void NandBrowserWidget::importPartition(int partIndex)
{
    if (partIndex < 0 || partIndex >= (int)m_partitions.size())
        return;

    QString filename = QFileDialog::getOpenFileName(
        this, tr("Import Partition"),
        QString(),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file"));
        return;
    }

    QByteArray content = f.readAll();
    f.close();

    if ((size_t)content.size() > m_partitions[partIndex].size)
    {
        auto ret = QMessageBox::warning(this, tr("Size Mismatch"),
                                         tr("File is %1 but partition is only %2. Truncate?")
                                             .arg(formatSize(content.size()),
                                                  formatSize(m_partitions[partIndex].size)),
                                         QMessageBox::Yes | QMessageBox::Cancel);
        if (ret != QMessageBox::Yes) return;
        content.truncate(m_partitions[partIndex].size);
    }

    bool ok = flash_write_raw(m_partitions[partIndex].offset,
                               reinterpret_cast<const uint8_t *>(content.constData()),
                               content.size());
    if (ok)
        m_infoLabel->setText(tr("Imported %1 bytes into %2")
                                  .arg(content.size())
                                  .arg(QString::fromUtf8(m_partitions[partIndex].name)));
    else
        QMessageBox::warning(this, tr("Error"), tr("Failed to write to NAND"));
}

void NandBrowserWidget::exportPage(size_t offset, size_t size)
{
    const uint8_t *data = flash_get_nand_data();
    if (!data) return;

    QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Page"),
        QStringLiteral("page_%1.bin").arg(offset, 8, 16, QLatin1Char('0')),
        tr("Binary files (*.bin);;All files (*)"));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(reinterpret_cast<const char *>(data + offset), size);
    f.close();
}

// -------------------- Filesystem file operations --------------------

void NandBrowserWidget::extractFile(const NandFsNode *node)
{
    if (!node || !m_fsValid) return;

    auto data = nand_fs_read_file(*m_filesystem, *node,
                                   flash_get_nand_data(), flash_get_nand_size());

    QString filename = QFileDialog::getSaveFileName(
        this, tr("Extract File"),
        QString::fromStdString(node->name));
    if (filename.isEmpty()) return;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing"));
        return;
    }
    f.write(reinterpret_cast<const char *>(data.data()), data.size());
    f.close();

    m_infoLabel->setText(tr("Extracted %1 (%2 bytes)")
                              .arg(QString::fromStdString(node->name)).arg(data.size()));
}

void NandBrowserWidget::editFile(const NandFsNode *node)
{
    if (!node || !m_fsValid) return;

    auto data = nand_fs_read_file(*m_filesystem, *node,
                                   flash_get_nand_data(), flash_get_nand_size());

    auto *editor = new NandFileEditor(*m_filesystem, *node, data, this);
    connect(editor, &NandFileEditor::savedToNand, this, [this]() {
        m_infoLabel->setText(tr("File saved to NAND. Use Flash > Save Changes to persist."));
    });
    editor->exec();
    delete editor;
}

const NandFsNode *NandBrowserWidget::findFsNode(const QString &path)
{
    if (!m_fsValid) return nullptr;
    if (!m_filesystem) return nullptr;
    return m_filesystem->find(path.toStdString());
}

// -------------------- Search --------------------

void NandBrowserWidget::onSearchTriggered()
{
    QString query = m_searchEdit->text();
    if (query.isEmpty()) return;

    const uint8_t *data = flash_get_nand_data();
    size_t nand_size = flash_get_nand_size();
    if (!data || nand_size == 0) return;

    m_infoLabel->setText(tr("Searching..."));

    // Pause emulator for consistent reads
    bool needsPause = emu_thread.isRunning() && !emu_thread.isPaused();
    if (needsPause)
        emu_thread.setPaused(true);

    QByteArray needle = query.toUtf8();
    int needleLen = needle.size();

    // Determine search range
    size_t searchStart = 0, searchEnd = nand_size;
    int scopeIdx = m_searchScope->currentIndex();
    if (scopeIdx > 0 && scopeIdx - 1 < (int)m_partitions.size())
    {
        searchStart = m_partitions[scopeIdx - 1].offset;
        searchEnd = searchStart + m_partitions[scopeIdx - 1].size;
    }

    m_searchResults->setRowCount(0);
    m_searchResults->show();

    int maxResults = 500;
    int row = 0;

    for (size_t i = searchStart; i + needleLen <= searchEnd && row < maxResults; i++)
    {
        if (memcmp(data + i, needle.constData(), needleLen) == 0)
        {
            m_searchResults->setRowCount(row + 1);

            auto *offItem = new QTableWidgetItem(formatOffset(i));
            offItem->setData(Qt::UserRole, QVariant::fromValue((qulonglong)i));

            // Find which partition
            QString partName = tr("Unknown");
            for (size_t p = 0; p < m_partitions.size(); p++)
            {
                size_t pStart = m_partitions[p].offset;
                size_t pEnd = pStart + m_partitions[p].size;
                if (i >= pStart && i < pEnd)
                {
                    partName = QString::fromUtf8(m_partitions[p].name);
                    break;
                }
            }

            // Context: show surrounding bytes as ASCII
            size_t ctxStart = (i >= 16) ? i - 16 : 0;
            size_t ctxEnd = std::min(i + needleLen + 32, searchEnd);
            QString ctx;
            for (size_t j = ctxStart; j < ctxEnd; j++)
            {
                char c = data[j];
                ctx += (c >= 0x20 && c < 0x7F) ? QChar(QLatin1Char(c)) : QChar(QLatin1Char('.'));
            }

            m_searchResults->setItem(row, 0, offItem);
            m_searchResults->setItem(row, 1, new QTableWidgetItem(partName));
            m_searchResults->setItem(row, 2, new QTableWidgetItem(ctx));
            row++;
        }
    }

    if (needsPause)
        emu_thread.setPaused(false);

    m_searchResults->resizeColumnsToContents();
    m_infoLabel->setText(tr("Search: %1 results for \"%2\"%3")
                              .arg(row).arg(query)
                              .arg(row >= maxResults ? tr(" (limited)") : QString()));
}

void NandBrowserWidget::onSearchResultClicked(QTableWidgetItem *item)
{
    int row = item->row();
    auto *offItem = m_searchResults->item(row, 0);
    if (!offItem) return;

    size_t offset = offItem->data(Qt::UserRole).toULongLong();
    // Show hex view centered on the result
    size_t start = (offset >= 256) ? offset - 256 : 0;
    showHexView(start, 1024);
}

// -------------------- Formatting helpers --------------------

QString NandBrowserWidget::formatSize(size_t bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString NandBrowserWidget::formatOffset(size_t offset)
{
    return QStringLiteral("0x%1").arg(offset, 8, 16, QLatin1Char('0'));
}
