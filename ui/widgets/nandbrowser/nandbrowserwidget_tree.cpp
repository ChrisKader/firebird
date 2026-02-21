#include "nandbrowserwidget.h"

#include <QApplication>
#include <QMenu>
#include <QStyle>

#include <algorithm>

#include "core/storage/flash.h"
#include "core/storage/nand_fs.h"
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

const NandFsNode *NandBrowserWidget::findFsNode(const QString &path)
{
    if (!m_fsValid) return nullptr;
    if (!m_filesystem) return nullptr;
    return m_filesystem->find(path.toStdString());
}
