#include "hexviewwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

#include "core/debug/debug_api.h"

void HexViewWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *goAddr = menu.addAction(tr("Go to address..."));
    connect(goAddr, &QAction::triggered, this, [this]() {
        m_addrEdit->setFocus();
        m_addrEdit->selectAll();
    });

    if (m_selectedOffset >= 0) {
        uint32_t addr = m_baseAddr + (uint32_t)m_selectedOffset;
        menu.addSeparator();

        QAction *copyAddr = menu.addAction(tr("Copy address"));
        connect(copyAddr, &QAction::triggered, this, [addr]() {
            QApplication::clipboard()->setText(
                QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
        });

        /* Copy as Hex String */
        QAction *copyHex = menu.addAction(tr("Copy as hex string (16 bytes)"));
        connect(copyHex, &QAction::triggered, this, [addr]() {
            uint8_t buf[16];
            int n = debug_read_memory(addr, buf, 16);
            QString hex;
            for (int i = 0; i < n; i++)
                hex += QStringLiteral("%1").arg(buf[i], 2, 16, QLatin1Char('0'));
            QApplication::clipboard()->setText(hex);
        });

        /* Copy as C Array */
        QAction *copyCArray = menu.addAction(tr("Copy as C array (16 bytes)"));
        connect(copyCArray, &QAction::triggered, this, [addr]() {
            uint8_t buf[16];
            int n = debug_read_memory(addr, buf, 16);
            QString text = QStringLiteral("{ ");
            for (int i = 0; i < n; i++) {
                if (i > 0) text += QStringLiteral(", ");
                text += QStringLiteral("0x%1").arg(buf[i], 2, 16, QLatin1Char('0'));
            }
            text += QStringLiteral(" }");
            QApplication::clipboard()->setText(text);
        });

        /* Copy as uint32 */
        QAction *copyU32 = menu.addAction(tr("Copy as uint32"));
        connect(copyU32, &QAction::triggered, this, [addr]() {
            uint32_t val = 0;
            debug_read_memory(addr, &val, 4);
            QApplication::clipboard()->setText(
                QStringLiteral("0x%1").arg(val, 8, 16, QLatin1Char('0')));
        });

        menu.addSeparator();

        QAction *viewDisasm = menu.addAction(tr("View in disassembly"));
        connect(viewDisasm, &QAction::triggered, this, [this, addr]() {
            emit gotoDisassembly(addr);
        });

        menu.addSeparator();

        /* Fill region */
        QAction *fillAct = menu.addAction(tr("Fill region..."));
        connect(fillAct, &QAction::triggered, this, [this, addr]() {
            QDialog dlg(this);
            dlg.setWindowTitle(tr("Fill Region"));
            auto *form = new QFormLayout(&dlg);

            auto *startEdit = new QLineEdit(&dlg);
            startEdit->setText(QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0')));
            form->addRow(tr("Start:"), startEdit);

            auto *lenEdit = new QLineEdit(&dlg);
            lenEdit->setText(QStringLiteral("100"));
            lenEdit->setPlaceholderText(QStringLiteral("hex byte count"));
            form->addRow(tr("Length:"), lenEdit);

            auto *valEdit = new QLineEdit(&dlg);
            valEdit->setText(QStringLiteral("00"));
            valEdit->setPlaceholderText(QStringLiteral("hex byte value"));
            form->addRow(tr("Fill byte:"), valEdit);

            auto *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() == QDialog::Accepted) {
                bool ok1, ok2, ok3;
                uint32_t startAddr = startEdit->text().toUInt(&ok1, 16);
                uint32_t length = lenEdit->text().toUInt(&ok2, 16);
                uint8_t fillByte = static_cast<uint8_t>(valEdit->text().toUInt(&ok3, 16));
                if (ok1 && ok2 && ok3 && length > 0 && length <= 0x100000) {
                    QByteArray fill(static_cast<int>(length), static_cast<char>(fillByte));
                    debug_write_memory(startAddr, fill.constData(), static_cast<int>(length));
                    refresh();
                }
            }
        });

        menu.addSeparator();

        /* Export region to file */
        QAction *exportAct = menu.addAction(tr("Export region to file..."));
        connect(exportAct, &QAction::triggered, this, [this, addr]() {
            bool ok = false;
            int size = QInputDialog::getInt(this, tr("Export Region"),
                                             tr("Number of bytes to export:"),
                                             256, 1, 0x1000000, 1, &ok);
            if (!ok) return;
            QString path = QFileDialog::getSaveFileName(this, tr("Export Memory"),
                                                         QString(), tr("Binary files (*.bin);;All files (*)"));
            if (path.isEmpty()) return;
            QByteArray data(size, 0);
            int read = debug_read_memory(addr, data.data(), size);
            data.resize(read);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
            else
                QMessageBox::warning(this, tr("Export Failed"), tr("Could not write file."));
        });

        /* Import/load file to address */
        QAction *importAct = menu.addAction(tr("Import file to address..."));
        connect(importAct, &QAction::triggered, this, [this, addr]() {
            QString path = QFileDialog::getOpenFileName(this, tr("Import Memory"),
                                                         QString(), tr("Binary files (*.bin);;All files (*)"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Import Failed"), tr("Could not read file."));
                return;
            }
            QByteArray data = f.readAll();
            if (!data.isEmpty()) {
                debug_write_memory(addr, data.constData(), data.size());
                refresh();
            }
        });

        menu.addSeparator();

        QAction *setBp = menu.addAction(tr("Set exec breakpoint"));
        connect(setBp, &QAction::triggered, this, [addr]() {
            debug_set_breakpoint(addr, true, false, false);
        });

        QAction *setReadWp = menu.addAction(tr("Set read watchpoint"));
        connect(setReadWp, &QAction::triggered, this, [addr]() {
            debug_set_breakpoint(addr, false, true, false);
        });

        QAction *setWriteWp = menu.addAction(tr("Set write watchpoint"));
        connect(setWriteWp, &QAction::triggered, this, [addr]() {
            debug_set_breakpoint(addr, false, false, true);
        });
    }

    menu.exec(event->globalPos());
}
