#ifndef NANDBROWSERWIDGET_H
#define NANDBROWSERWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QLabel>
#include <QString>
#include <stdint.h>

class NandBrowserWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NandBrowserWidget(QWidget *parent = nullptr);

public slots:
    void openImage(const QString &path);
    void openCurrentFlash();
    void refresh();

private:
    struct PartitionInfo {
        QString name;
        size_t offset;
        size_t size;
    };

    struct NandInfo {
        QString path;
        bool isLarge;
        uint16_t product;
        QString hwType;
        size_t totalSize;
        PartitionInfo partitions[5]; /* Manuf, Boot2, Bootdata, Diags, Filesystem */
    };

    void populateTree(const NandInfo &info);
    NandInfo readNandImage(const QString &path);

    QTreeWidget *m_tree;
    QToolBar *m_toolbar;
    QLabel *m_infoLabel;
    QString m_currentPath;
};

#endif // NANDBROWSERWIDGET_H
