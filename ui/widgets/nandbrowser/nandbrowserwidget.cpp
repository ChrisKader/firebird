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
#include "core/storage/flash.h"
#include "core/storage/nand_fs.h"

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

    EmuThread *emu = emuThreadInstance();
    if (!emu)
    {
        m_infoLabel->setText(tr("Emulator thread unavailable"));
        return;
    }

    // Check if the emulator is running and needs to be paused.
    // If already paused, skip the delay and don't unpause afterward.
    bool needsPause = emu->isRunning() && !emu->isPaused();

    if (needsPause)
    {
        m_infoLabel->setText(tr("Pausing emulator..."));
        emu->setPaused(true);
    }
    else
    {
        m_infoLabel->setText(tr("Loading..."));
    }

    // Defer heavy work so the emu thread has time to reach its pause loop.
    // No delay needed if already paused or not running.
    QTimer::singleShot(needsPause ? 200 : 0, this, [this, needsPause, emu]() {
        doLoad();
        if (needsPause)
            emu->setPaused(false);
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
