#include "hexviewwidget.h"

#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QVBoxLayout>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QPalette>

#include "core/debug_api.h"

HexViewWidget::HexViewWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_monoFont.setPointSize(11);

    memset(m_data, 0, sizeof(m_data));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new QToolBar(container);
    m_toolbar->setIconSize(QSize(16, 16));

    m_addrEdit = new QLineEdit(m_toolbar);
    m_addrEdit->setPlaceholderText(QStringLiteral("Address..."));
    m_addrEdit->setFixedWidth(100);
    m_addrEdit->setFont(m_monoFont);
    connect(m_addrEdit, &QLineEdit::returnPressed, this, [this]() {
        bool ok = false;
        uint32_t addr = m_addrEdit->text().toUInt(&ok, 16);
        if (ok)
            goToAddress(addr);
    });
    m_toolbar->addWidget(m_addrEdit);
    m_toolbar->addSeparator();

    /* Search bar */
    m_searchEdit = new QLineEdit(m_toolbar);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search hex..."));
    m_searchEdit->setFixedWidth(120);
    m_searchEdit->setFont(m_monoFont);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        doSearch(true);
    });
    m_toolbar->addWidget(m_searchEdit);

    QAction *searchNext = m_toolbar->addAction(QStringLiteral("\u25B6"));
    searchNext->setToolTip(tr("Search Next"));
    connect(searchNext, &QAction::triggered, this, [this]() { doSearch(true); });

    m_toolbar->addSeparator();

    /* ASCII toggle */
    m_asciiToggle = new QCheckBox(tr("ASCII"), m_toolbar);
    m_asciiToggle->setChecked(true);
    connect(m_asciiToggle, &QCheckBox::toggled, this, [this](bool checked) {
        m_showAscii = checked;
        viewport()->update();
    });
    m_toolbar->addWidget(m_asciiToggle);

    layout->addWidget(m_toolbar);

    setViewportMargins(0, m_toolbar->sizeHint().height(), 0, 0);
    container->setGeometry(0, 0, width(), m_toolbar->sizeHint().height());

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    viewport()->setFont(m_monoFont);
    viewport()->setCursor(Qt::IBeamCursor);
}

int HexViewWidget::lineHeight() const
{
    return QFontMetrics(m_monoFont).height() + 2;
}

int HexViewWidget::visibleLineCount() const
{
    return viewport()->height() / lineHeight();
}

uint32_t HexViewWidget::selectedAddress() const
{
    if (m_selectedOffset < 0)
        return m_baseAddr;
    return m_baseAddr + (uint32_t)m_selectedOffset;
}

void HexViewWidget::refresh()
{
    int rows = visibleLineCount() + 1;
    if (rows > 64) rows = 64;
    m_dataRows = rows;

    uint32_t addr = m_baseAddr + (uint32_t)verticalScrollBar()->value() * BYTES_PER_ROW;
    debug_read_memory(addr, m_data, rows * BYTES_PER_ROW);

    viewport()->update();
}

void HexViewWidget::goToAddress(uint32_t addr)
{
    m_baseAddr = addr & ~(uint32_t)(BYTES_PER_ROW - 1);
    m_selectedOffset = (int)(addr - m_baseAddr);
    m_editOffset = -1;
    verticalScrollBar()->setValue(0);
    refresh();
}

void HexViewWidget::doSearch(bool forward)
{
    (void)forward;
    QString hexStr = m_searchEdit->text().remove(QLatin1Char(' '));
    if (hexStr.isEmpty())
        return;

    QByteArray pattern;
    for (int i = 0; i + 1 < hexStr.size(); i += 2) {
        bool ok;
        uint8_t byte = hexStr.mid(i, 2).toUInt(&ok, 16);
        if (!ok) return;
        pattern.append(static_cast<char>(byte));
    }

    uint32_t start = selectedAddress() + 1;
    uint32_t result = debug_search_memory(start, 0x1000000,
                                           reinterpret_cast<const uint8_t *>(pattern.constData()),
                                           pattern.size());
    if (result != 0xFFFFFFFF) {
        goToAddress(result);
    }
}

void HexViewWidget::updateScrollBar()
{
    verticalScrollBar()->setRange(0, TOTAL_ROWS - visibleLineCount());
    verticalScrollBar()->setPageStep(visibleLineCount());
}

void HexViewWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    if (m_toolbar) {
        int h = m_toolbar->sizeHint().height();
        m_toolbar->parentWidget()->setGeometry(0, 0, width(), h);
        setViewportMargins(0, h, 0, 0);
    }

    updateScrollBar();
    refresh();
}

void HexViewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(m_monoFont);

    const QPalette &pal = palette();
    const QColor bgColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor selColor = pal.color(QPalette::Highlight);
    const QColor selTextColor = pal.color(QPalette::HighlightedText);
    const QColor mutedColor = pal.color(QPalette::PlaceholderText);
    const QColor addrColor(0x80, 0x80, 0x80);
    const bool isDark = bgColor.lightness() < 128;

    p.fillRect(viewport()->rect(), bgColor);

    int lh = lineHeight();
    QFontMetrics fm(m_monoFont);
    int charW = fm.horizontalAdvance(QLatin1Char('0'));

    int xAddr = 4;
    int xHex = xAddr + charW * 10;
    int hexWidth = charW * (BYTES_PER_ROW * 3 + 1);
    int xAscii = xHex + hexWidth + charW;

    /* Draw address column background */
    p.fillRect(0, 0, xHex - 2, viewport()->height(),
               isDark ? bgColor.lighter(110) : bgColor.darker(103));

    int scrollOff = verticalScrollBar()->value();
    int visible = visibleLineCount();

    for (int row = 0; row < visible && row < m_dataRows; row++) {
        int y = row * lh;
        uint32_t addr = m_baseAddr + (uint32_t)(scrollOff + row) * BYTES_PER_ROW;
        const uint8_t *rowData = &m_data[row * BYTES_PER_ROW];

        /* Address */
        p.setPen(addrColor);
        QString addrStr = QStringLiteral("%1").arg(addr, 8, 16, QLatin1Char('0'));
        p.drawText(xAddr, y, charW * 9, lh, Qt::AlignLeft | Qt::AlignVCenter, addrStr);

        /* Hex bytes */
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            int byteOff = (scrollOff + row) * BYTES_PER_ROW + col;
            int x = xHex + col * charW * 3;
            if (col >= 8)
                x += charW;

            bool selected = (byteOff == m_selectedOffset);
            bool editing = (byteOff == m_editOffset);

            if (selected || editing) {
                p.fillRect(x - 1, y, charW * 2 + 2, lh, selColor);
                p.setPen(selTextColor);
            } else {
                /* Color non-zero bytes slightly differently */
                p.setPen(rowData[col] == 0 ? mutedColor : textColor);
            }

            QString hexByte = QStringLiteral("%1").arg(rowData[col], 2, 16, QLatin1Char('0'));
            p.drawText(x, y, charW * 2, lh, Qt::AlignLeft | Qt::AlignVCenter, hexByte);

            /* Editing cursor */
            if (editing) {
                int cursorX = x + m_editNibble * charW;
                p.setPen(isDark ? Qt::white : Qt::black);
                p.drawLine(cursorX, y + 2, cursorX, y + lh - 2);
            }
        }

        /* ASCII column */
        if (m_showAscii) {
            for (int col = 0; col < BYTES_PER_ROW; col++) {
                int byteOff = (scrollOff + row) * BYTES_PER_ROW + col;
                char c = rowData[col];
                bool isPrintable = (c >= 0x20 && c < 0x7F);
                bool selected = (byteOff == m_selectedOffset);

                if (selected) {
                    p.fillRect(xAscii + col * charW, y, charW, lh, selColor);
                    p.setPen(selTextColor);
                } else {
                    p.setPen(isPrintable ? textColor : mutedColor);
                }

                QChar ch = isPrintable ? QLatin1Char(c) : QLatin1Char('.');
                p.drawText(xAscii + col * charW, y, charW, lh,
                           Qt::AlignLeft | Qt::AlignVCenter, ch);
            }
        }
    }

    /* Separator line between hex and ASCII */
    if (m_showAscii) {
        p.setPen(pal.color(QPalette::Mid));
        int sepX = xAscii - charW / 2;
        p.drawLine(sepX, 0, sepX, viewport()->height());
    }
}

void HexViewWidget::mousePressEvent(QMouseEvent *event)
{
    QFontMetrics fm(m_monoFont);
    int charW = fm.horizontalAdvance(QLatin1Char('0'));
    int xHex = 4 + charW * 10;
    int lh = lineHeight();

    int row = (int)(event->position().y() / lh);
    int x = (int)event->position().x();

    if (x >= xHex) {
        int relX = x - xHex;
        int col = -1;

        /* Account for gap at byte 8 */
        if (relX < charW * 8 * 3) {
            col = relX / (charW * 3);
        } else {
            int adjusted = relX - charW; /* subtract gap */
            if (adjusted >= charW * 8 * 3) {
                col = 8 + (adjusted - charW * 8 * 3) / (charW * 3);
            } else {
                col = adjusted / (charW * 3);
            }
        }

        if (col >= 0 && col < BYTES_PER_ROW) {
            int scrollOff = verticalScrollBar()->value();
            m_selectedOffset = (scrollOff + row) * BYTES_PER_ROW + col;
            m_editOffset = m_selectedOffset;
            m_editNibble = 0;
            viewport()->update();
        }
    }

    QAbstractScrollArea::mousePressEvent(event);
}

void HexViewWidget::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y();
    if (delta == 0) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    int lines = delta > 0 ? -3 : 3;
    int newVal = verticalScrollBar()->value() + lines;
    newVal = qBound(verticalScrollBar()->minimum(), newVal, verticalScrollBar()->maximum());
    verticalScrollBar()->setValue(newVal);
    refresh();
    event->accept();
}

void HexViewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_G && (event->modifiers() & Qt::ControlModifier)) {
        m_addrEdit->setFocus();
        m_addrEdit->selectAll();
        return;
    }

    if (event->key() == Qt::Key_F && (event->modifiers() & Qt::ControlModifier)) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
        return;
    }

    /* Hex editing: type hex digits to modify bytes */
    if (m_editOffset >= 0) {
        QString text = event->text();
        if (text.size() == 1) {
            QChar c = text.at(0).toLower();
            int nibble = -1;
            if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                nibble = c.toLatin1() - '0';
            else if (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                nibble = c.toLatin1() - 'a' + 10;

            if (nibble >= 0) {
                uint32_t addr = m_baseAddr + (uint32_t)m_editOffset;
                uint8_t current;
                debug_read_memory(addr, &current, 1);

                if (m_editNibble == 0) {
                    current = (uint8_t)((nibble << 4) | (current & 0x0F));
                    m_editNibble = 1;
                } else {
                    current = (uint8_t)((current & 0xF0) | nibble);
                    m_editNibble = 0;
                    m_editOffset++;
                    m_selectedOffset = m_editOffset;
                }
                debug_write_memory(addr, &current, 1);
                refresh();
                return;
            }
        }

        /* Arrow key navigation */
        if (event->key() == Qt::Key_Right) {
            m_editOffset++;
            m_selectedOffset = m_editOffset;
            m_editNibble = 0;
            viewport()->update();
            return;
        } else if (event->key() == Qt::Key_Left) {
            if (m_editOffset > 0) m_editOffset--;
            m_selectedOffset = m_editOffset;
            m_editNibble = 0;
            viewport()->update();
            return;
        } else if (event->key() == Qt::Key_Down) {
            m_editOffset += BYTES_PER_ROW;
            m_selectedOffset = m_editOffset;
            viewport()->update();
            return;
        } else if (event->key() == Qt::Key_Up) {
            if (m_editOffset >= BYTES_PER_ROW) m_editOffset -= BYTES_PER_ROW;
            m_selectedOffset = m_editOffset;
            viewport()->update();
            return;
        } else if (event->key() == Qt::Key_Escape) {
            m_editOffset = -1;
            viewport()->update();
            return;
        }
    }

    QAbstractScrollArea::keyPressEvent(event);
}

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

        QAction *viewDisasm = menu.addAction(tr("View in disassembly"));
        connect(viewDisasm, &QAction::triggered, this, [this, addr]() {
            emit gotoDisassembly(addr);
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
