#ifndef NANDBROWSERWIDGET_H
#define NANDBROWSERWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QString>
#include <stdint.h>
#include <memory>
#include <set>
#include <vector>

#include "core/storage/flash.h"
#include "core/storage/nand_fs.h"

class NandBrowserWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NandBrowserWidget(QWidget *parent = nullptr);

public slots:
    void openCurrentFlash();
    void refresh();

private slots:
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void onSearchTriggered();
    void onSearchResultClicked(QTableWidgetItem *item);

private:
    enum TreeItemRole {
        RoleType = Qt::UserRole,        // "partition", "page", "fsdir", "fsfile"
        RoleOffset = Qt::UserRole + 1,  // byte offset into NAND
        RoleSize = Qt::UserRole + 2,    // byte size
        RolePartIndex = Qt::UserRole + 3, // partition index
        RoleFsPath = Qt::UserRole + 4,  // filesystem path (for fs nodes)
        RoleInodeNum = Qt::UserRole + 5,
    };

    void doLoad();
    void populatePartitions();
    void populateFilesystemTree(QTreeWidgetItem *fsItem, int partIndex);
    void addFsChildren(QTreeWidgetItem *parentItem, const NandFilesystem &fs,
                       uint32_t parent_inode, int depth,
                       std::set<uint32_t> &visited);
    void showPartitionPages(int partIndex);
    void showHexView(size_t offset, size_t size);
    void showTextPreview(const std::vector<uint8_t> &data, const QString &title);
    void exportPartition(int partIndex);
    void importPartition(int partIndex);
    void exportPage(size_t offset, size_t size);
    void extractFile(const NandFsNode *node);
    void editFile(const NandFsNode *node);
    const NandFsNode *findFsNode(const QString &path);

    static QString formatSize(size_t bytes);
    static QString formatOffset(size_t offset);

    // Widgets
    QToolBar *m_toolbar;
    QLabel *m_infoLabel;
    QSplitter *m_splitter;
    QTreeWidget *m_tree;
    QStackedWidget *m_rightPane;

    // Right pane pages
    QWidget *m_welcomePage;
    QTableWidget *m_pageTable;     // Page list for partition
    QPlainTextEdit *m_hexView;     // Hex dump
    QPlainTextEdit *m_textPreview; // Text preview

    // Search
    QLineEdit *m_searchEdit;
    QComboBox *m_searchScope;
    QTableWidget *m_searchResults;
    QSplitter *m_vertSplitter;     // Top (main content) / bottom (search results)

    // State
    std::vector<flash_partition_info> m_partitions;
    std::unique_ptr<NandFilesystem> m_filesystem;
    bool m_fsValid = false;
    int m_fsPartIndex = -1;  // Which partition index holds filesystem
};

#endif // NANDBROWSERWIDGET_H
