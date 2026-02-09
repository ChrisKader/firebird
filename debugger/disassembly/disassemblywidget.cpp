#include "disassemblywidget.h"

#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QFontDatabase>
#include <QVBoxLayout>
#include <QToolButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QPalette>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

#include "core/debug_api.h"
#include "ui/widgettheme.h"

/* -- Syntax helpers ------------------------------------------------------- */

void DisassemblyWidget::parseMnemonicOperands(const QString &text,
                                               QString &mnemonic, QString &operands)
{
    int tab = text.indexOf(QLatin1Char('\t'));
    if (tab < 0) {
        mnemonic = text.trimmed();
        operands.clear();
    } else {
        mnemonic = text.left(tab).trimmed();
        operands = text.mid(tab + 1).trimmed();
    }
}

bool DisassemblyWidget::isBranchMnemonic(const QString &mnem) const
{
    if (mnem.startsWith(QLatin1String("b"), Qt::CaseInsensitive)) {
        if (mnem.size() == 1) return true;
        QChar c = mnem.at(1);
        if (c == QLatin1Char('l') || c == QLatin1Char('x') || c == QLatin1Char('e')
            || c == QLatin1Char('n') || c == QLatin1Char('c') || c == QLatin1Char('m')
            || c == QLatin1Char('p') || c == QLatin1Char('v') || c == QLatin1Char('h')
            || c == QLatin1Char('g') || c == QLatin1Char('l'))
            return true;
        if (mnem == QLatin1String("bic")) return false;
    }
    if (mnem == QLatin1String("swi") || mnem == QLatin1String("svc"))
        return true;
    return false;
}

uint32_t DisassemblyWidget::parseBranchTarget(const Line &line) const
{
    /* Try to extract hex address from operands like "0x10001234" */
    QString ops = line.operands.trimmed();
    if (ops.startsWith(QLatin1String("0x")) || ops.startsWith(QLatin1String("0X"))) {
        bool ok;
        uint32_t addr = ops.mid(2).split(QLatin1Char(',')).first().toUInt(&ok, 16);
        if (ok) return addr;
    }
    /* Try bare hex */
    bool ok;
    uint32_t addr = ops.split(QLatin1Char(',')).first().toUInt(&ok, 16);
    if (ok && addr >= 0x10000) return addr;

    return 0xFFFFFFFF;
}

QString DisassemblyWidget::symbolForAddress(uint32_t addr) const
{
    auto it = m_symbols.constFind(addr);
    return (it != m_symbols.constEnd()) ? it.value() : QString();
}

/* -- Symbol file loading -------------------------------------------------- */

bool DisassemblyWidget::loadSymbolFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_symbols.clear();
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        /* Support formats: "addr=name" or "addr name" */
        int sep = line.indexOf(QLatin1Char('='));
        if (sep < 0) sep = line.indexOf(QLatin1Char(' '));
        if (sep < 0) continue;

        bool ok;
        uint32_t addr = line.left(sep).trimmed().toUInt(&ok, 16);
        if (!ok) continue;

        QString name = line.mid(sep + 1).trimmed();
        if (!name.isEmpty())
            m_symbols.insert(addr, name);
    }

    return true;
}

/* -- Navigation history --------------------------------------------------- */

void DisassemblyWidget::pushNavHistory(uint32_t addr)
{
    m_navHistory.push(addr);
    m_navForward.clear();
}

void DisassemblyWidget::navBack()
{
    if (m_navHistory.isEmpty()) return;
    m_navForward.push(m_baseAddr);
    uint32_t addr = m_navHistory.pop();
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

void DisassemblyWidget::navForward()
{
    if (m_navForward.isEmpty()) return;
    m_navHistory.push(m_baseAddr);
    uint32_t addr = m_navForward.pop();
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

/* -- Construction --------------------------------------------------------- */

DisassemblyWidget::DisassemblyWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_monoFont.setPointSize(11);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new QToolBar(container);
    m_toolbar->setIconSize(QSize(16, 16));

    m_addrEdit = new QLineEdit(m_toolbar);
    m_addrEdit->setPlaceholderText(QStringLiteral("Go to address..."));
    m_addrEdit->setFixedWidth(120);
    m_addrEdit->setFont(m_monoFont);
    connect(m_addrEdit, &QLineEdit::returnPressed, this, [this]() {
        bool ok = false;
        uint32_t addr = m_addrEdit->text().toUInt(&ok, 16);
        if (ok) {
            pushNavHistory(m_baseAddr);
            goToAddress(addr);
        }
    });
    m_toolbar->addWidget(m_addrEdit);
    m_toolbar->addSeparator();

    /* Back/Forward navigation */
    QAction *backAct = m_toolbar->addAction(QStringLiteral("\u25C0"));
    backAct->setToolTip(tr("Back (Alt+Left)"));
    backAct->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    connect(backAct, &QAction::triggered, this, &DisassemblyWidget::navBack);

    QAction *fwdAct = m_toolbar->addAction(QStringLiteral("\u25B6\u25B6"));
    fwdAct->setToolTip(tr("Forward (Alt+Right)"));
    fwdAct->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    connect(fwdAct, &QAction::triggered, this, &DisassemblyWidget::navForward);

    m_toolbar->addSeparator();

    auto addBtn = [&](const QString &text, const QString &tip, const QString &shortcut, const QString &cmd) {
        QAction *act = m_toolbar->addAction(text);
        act->setToolTip(tip);
        if (!shortcut.isEmpty())
            act->setShortcut(QKeySequence(shortcut));
        connect(act, &QAction::triggered, this, [this, cmd]() {
            emit debugCommand(cmd);
        });
    };

    addBtn(QStringLiteral("\u25B6"), tr("Continue (F5)"),   QStringLiteral("F5"),  QStringLiteral("c"));
    addBtn(QStringLiteral("\u2193"), tr("Step Into (F6)"),  QStringLiteral("F6"),  QStringLiteral("s"));
    addBtn(QStringLiteral("\u2192"), tr("Step Over (F7)"),  QStringLiteral("F7"),  QStringLiteral("n"));
    addBtn(QStringLiteral("\u2191"), tr("Step Out (F8)"),   QStringLiteral("F8"),  QStringLiteral("finish"));

    m_toolbar->addSeparator();

    /* Search bar */
    m_searchEdit = new QLineEdit(m_toolbar);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search..."));
    m_searchEdit->setFixedWidth(100);
    m_searchEdit->setFont(m_monoFont);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        QString text = m_searchEdit->text().trimmed();
        if (text.isEmpty()) return;

        /* Try as address first */
        bool ok;
        uint32_t addr = text.toUInt(&ok, 16);
        if (ok) {
            pushNavHistory(m_baseAddr);
            goToAddress(addr);
            return;
        }

        /* Try as symbol name */
        for (auto it = m_symbols.constBegin(); it != m_symbols.constEnd(); ++it) {
            if (it.value().contains(text, Qt::CaseInsensitive)) {
                pushNavHistory(m_baseAddr);
                goToAddress(it.key());
                return;
            }
        }

        /* Search mnemonics in visible lines */
        for (int i = 0; i < m_lines.size(); i++) {
            if (m_lines[i].mnemonic.contains(text, Qt::CaseInsensitive)) {
                m_selectedLine = i;
                viewport()->update();
                return;
            }
        }
    });
    m_toolbar->addWidget(m_searchEdit);

    layout->addWidget(m_toolbar);

    setViewportMargins(0, m_toolbar->sizeHint().height(), 0, 0);
    container->setGeometry(0, 0, width(), m_toolbar->sizeHint().height());

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    viewport()->setFont(m_monoFont);
    viewport()->setCursor(Qt::ArrowCursor);
}

int DisassemblyWidget::lineHeight() const
{
    return QFontMetrics(m_monoFont).height() + 2;
}

int DisassemblyWidget::visibleLineCount() const
{
    return viewport()->height() / lineHeight();
}

void DisassemblyWidget::refresh()
{
    uint32_t regs[16];
    uint32_t cpsr, spsr;
    bool has_spsr;
    debug_get_registers(regs, &cpsr, &spsr, &has_spsr);
    m_pcAddr = regs[15];

    goToPC();
}

void DisassemblyWidget::goToPC()
{
    int visible = visibleLineCount();
    int before = visible / 3;

    bool is_thumb = debug_is_thumb_mode();
    uint32_t step = is_thumb ? 2 : 4;
    m_baseAddr = m_pcAddr - before * step;

    updateLines();
    viewport()->update();
}

void DisassemblyWidget::goToAddress(uint32_t addr)
{
    m_baseAddr = addr;
    m_selectedLine = -1;
    updateLines();
    viewport()->update();
}

void DisassemblyWidget::updateLines()
{
    struct debug_disasm_line raw_lines[NUM_LINES];
    int count = debug_disassemble(m_baseAddr, raw_lines, NUM_LINES);

    struct debug_breakpoint bps[256];
    int bp_count = debug_list_breakpoints(bps, 256);

    m_lines.resize(count);
    for (int i = 0; i < count; i++) {
        Line &line = m_lines[i];
        line.addr = raw_lines[i].addr;
        line.raw = raw_lines[i].raw;
        line.size = raw_lines[i].size;
        line.is_thumb = raw_lines[i].is_thumb;
        line.is_pc = (line.addr == m_pcAddr);
        line.has_exec_bp = false;
        line.has_read_wp = false;
        line.has_write_wp = false;

        QString full = QString::fromLatin1(raw_lines[i].text);
        parseMnemonicOperands(full, line.mnemonic, line.operands);

        for (int b = 0; b < bp_count; b++) {
            if (bps[b].addr == line.addr) {
                if (bps[b].exec) line.has_exec_bp = true;
                if (bps[b].read) line.has_read_wp = true;
                if (bps[b].write) line.has_write_wp = true;
            }
        }
    }

    updateScrollBar();
}

void DisassemblyWidget::updateScrollBar()
{
    verticalScrollBar()->setRange(0, qMax(0, m_lines.size() - visibleLineCount()));
    verticalScrollBar()->setPageStep(visibleLineCount());
}

void DisassemblyWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    if (m_toolbar) {
        int h = m_toolbar->sizeHint().height();
        m_toolbar->parentWidget()->setGeometry(0, 0, width(), h);
        setViewportMargins(0, h, 0, 0);
    }

    updateScrollBar();
}

/* -- Syntax-highlighting paint -------------------------------------------- */

static void drawOperandsHighlighted(QPainter &p, int x, int y, int h,
                                     const QFont &font, const QString &operands,
                                     const QColor &defaultColor,
                                     const QColor &registerColor,
                                     const QColor &immediateColor)
{
    QFontMetrics fm(font);
    int cx = x;
    int i = 0;
    int len = operands.size();

    while (i < len) {
        QChar c = operands.at(i);
        int start = i;
        QColor color = defaultColor;

        if (c == QLatin1Char('r') || c == QLatin1Char('s') || c == QLatin1Char('l')
            || c == QLatin1Char('p') || c == QLatin1Char('c')) {
            int j = i + 1;
            while (j < len && (operands.at(j).isLetterOrNumber()))
                j++;
            QString tok = operands.mid(i, j - i);
            static const QStringList regs = {
                QStringLiteral("r0"),  QStringLiteral("r1"),  QStringLiteral("r2"),
                QStringLiteral("r3"),  QStringLiteral("r4"),  QStringLiteral("r5"),
                QStringLiteral("r6"),  QStringLiteral("r7"),  QStringLiteral("r8"),
                QStringLiteral("r9"),  QStringLiteral("r10"), QStringLiteral("r11"),
                QStringLiteral("r12"), QStringLiteral("r13"), QStringLiteral("r14"),
                QStringLiteral("r15"), QStringLiteral("sp"),  QStringLiteral("lr"),
                QStringLiteral("pc"),  QStringLiteral("cpsr"),QStringLiteral("spsr"),
            };
            if (regs.contains(tok, Qt::CaseInsensitive)) {
                color = registerColor;
                i = j;
            } else {
                i++;
            }
        } else if (c == QLatin1Char('#') || (c.isDigit() && i > 0 &&
                   operands.at(i - 1) != QLatin1Char('r'))) {
            color = immediateColor;
            if (c == QLatin1Char('#')) i++;
            while (i < len && (operands.at(i).isLetterOrNumber() || operands.at(i) == QLatin1Char('x')))
                i++;
        } else {
            i++;
        }

        QString segment = operands.mid(start, i - start);
        p.setPen(color);
        p.drawText(cx, y, fm.horizontalAdvance(segment), h,
                   Qt::AlignLeft | Qt::AlignVCenter, segment);
        cx += fm.horizontalAdvance(segment);
    }
}

void DisassemblyWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(m_monoFont);

    const QPalette &pal = palette();
    const QColor bgColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor selColor = pal.color(QPalette::Highlight);
    const bool isDark = bgColor.lightness() < 128;

    const WidgetTheme &theme = currentWidgetTheme();

    p.fillRect(viewport()->rect(), bgColor);

    /* Draw margin background */
    p.fillRect(0, 0, MARGIN_WIDTH, viewport()->height(),
               isDark ? bgColor.lighter(120) : bgColor.darker(105));
    p.setPen(pal.color(QPalette::Mid));
    p.drawLine(MARGIN_WIDTH, 0, MARGIN_WIDTH, viewport()->height());

    int lh = lineHeight();
    int scrollOff = verticalScrollBar()->value();
    int visible = visibleLineCount();

    QFontMetrics fm(m_monoFont);
    int charW = fm.horizontalAdvance(QLatin1Char('0'));

    int xAddr = MARGIN_WIDTH + 4;
    int xRaw = xAddr + charW * 10;
    int xMnem = xRaw + charW * 10;
    int xOper = xMnem + charW * 8;

    for (int i = 0; i < visible && (i + scrollOff) < m_lines.size(); i++) {
        const Line &line = m_lines[i + scrollOff];
        int y = i * lh;

        /* Symbol label above instruction */
        QString sym = symbolForAddress(line.addr);
        if (!sym.isEmpty()) {
            QFont symFont = m_monoFont;
            symFont.setBold(true);
            p.setFont(symFont);
            p.setPen(theme.syntaxSymbol);
            p.drawText(xAddr, y - lh / 2, viewport()->width(), lh,
                       Qt::AlignLeft | Qt::AlignBottom, sym + QStringLiteral(":"));
            p.setFont(m_monoFont);
        }

        /* PC row background */
        if (line.is_pc) {
            p.fillRect(MARGIN_WIDTH + 1, y, viewport()->width() - MARGIN_WIDTH - 1, lh,
                       theme.markerPcBg);
        } else if (i + scrollOff == m_selectedLine) {
            QColor sel = selColor;
            sel.setAlpha(40);
            p.fillRect(MARGIN_WIDTH + 1, y, viewport()->width() - MARGIN_WIDTH - 1, lh, sel);
        }

        /* -- Margin markers ------------------------------------------------ */
        int markerX = 2;
        int markerY = y + 2;
        int markerS = lh - 4;

        if (line.has_exec_bp) {
            p.save();
            p.setBrush(theme.markerBreakpoint);
            p.setPen(Qt::NoPen);
            p.drawEllipse(markerX, markerY, markerS, markerS);
            p.restore();
        }
        if (line.has_read_wp) {
            p.save();
            p.setPen(theme.markerWatchRead);
            QFont smallFont = m_monoFont;
            smallFont.setPointSize(7);
            smallFont.setBold(true);
            p.setFont(smallFont);
            p.drawText(markerX, markerY, markerS, markerS,
                       Qt::AlignCenter, QStringLiteral("R"));
            p.setFont(m_monoFont);
            p.restore();
        }
        if (line.has_write_wp) {
            p.save();
            p.setPen(theme.markerWatchWrite);
            QFont smallFont = m_monoFont;
            smallFont.setPointSize(7);
            smallFont.setBold(true);
            p.setFont(smallFont);
            p.drawText(markerX, markerY, markerS, markerS,
                       Qt::AlignCenter, QStringLiteral("W"));
            p.setFont(m_monoFont);
            p.restore();
        }

        /* PC arrow indicator */
        if (line.is_pc) {
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(theme.markerPcArrow);
            int ax = MARGIN_WIDTH - 12;
            int ay = y + lh / 2;
            QPolygon arrow;
            arrow << QPoint(ax, ay - 3) << QPoint(ax + 6, ay) << QPoint(ax, ay + 3);
            p.drawPolygon(arrow);
            p.restore();
        }

        /* -- Address column ------------------------------------------------ */
        p.setPen(theme.syntaxAddress);
        QString addrStr = QStringLiteral("%1").arg(line.addr, 8, 16, QLatin1Char('0'));
        p.drawText(xAddr, y, charW * 9, lh, Qt::AlignLeft | Qt::AlignVCenter, addrStr);

        /* -- Raw bytes column ---------------------------------------------- */
        p.setPen(theme.syntaxAddress);
        QString rawStr;
        if (line.size == 4)
            rawStr = QStringLiteral("%1").arg(line.raw, 8, 16, QLatin1Char('0'));
        else
            rawStr = QStringLiteral("%1").arg(line.raw & 0xFFFF, 4, 16, QLatin1Char('0'));
        p.drawText(xRaw, y, charW * 9, lh, Qt::AlignLeft | Qt::AlignVCenter, rawStr);

        /* -- Mnemonic (colored by type) ------------------------------------ */
        QColor mnemColor = isBranchMnemonic(line.mnemonic) ? theme.syntaxBranch
                                                           : theme.syntaxMnemonic;
        QFont boldFont = m_monoFont;
        boldFont.setBold(true);
        p.setFont(boldFont);
        p.setPen(mnemColor);
        p.drawText(xMnem, y, charW * 7, lh, Qt::AlignLeft | Qt::AlignVCenter, line.mnemonic);
        p.setFont(m_monoFont);

        /* -- Operands (syntax highlighted) --------------------------------- */
        if (!line.operands.isEmpty()) {
            /* Resolve symbol references in operands */
            QString displayOps = line.operands;
            if (isBranchMnemonic(line.mnemonic)) {
                uint32_t target = parseBranchTarget(line);
                if (target != 0xFFFFFFFF) {
                    QString targetSym = symbolForAddress(target);
                    if (!targetSym.isEmpty()) {
                        displayOps = QStringLiteral("%1 <%2>")
                            .arg(line.operands, targetSym);
                    }
                }
            }
            drawOperandsHighlighted(p, xOper, y, lh, m_monoFont, displayOps,
                                    textColor,
                                    theme.syntaxRegister,
                                    theme.syntaxImmediate);
        }
    }
}

/* -- Mouse and keyboard interaction --------------------------------------- */

void DisassemblyWidget::mousePressEvent(QMouseEvent *event)
{
    int lh = lineHeight();
    int scrollOff = verticalScrollBar()->value();
    int lineIdx = (int)(event->position().y() / lh) + scrollOff;

    bool ctrl = event->modifiers() & Qt::ControlModifier;

    if (lineIdx >= 0 && lineIdx < m_lines.size()) {
        if (event->position().x() < MARGIN_WIDTH) {
            const Line &line = m_lines[lineIdx];
            if (line.has_exec_bp) {
                debug_clear_breakpoint(line.addr);
            } else {
                debug_set_breakpoint(line.addr, true, false, false);
            }
            updateLines();
            viewport()->update();
            emit breakpointToggled(m_lines[lineIdx].addr, !m_lines[lineIdx].has_exec_bp);
        } else if (ctrl && isBranchMnemonic(m_lines[lineIdx].mnemonic)) {
            /* Ctrl+Click on branch target: navigate to it */
            uint32_t target = parseBranchTarget(m_lines[lineIdx]);
            if (target != 0xFFFFFFFF) {
                if (event->modifiers() & Qt::ShiftModifier) {
                    /* Ctrl+Shift+Click: open in hex view */
                    emit addressSelected(target);
                } else {
                    pushNavHistory(m_baseAddr);
                    goToAddress(target);
                }
            }
        } else {
            m_selectedLine = lineIdx;
            viewport()->update();
        }
    }

    QAbstractScrollArea::mousePressEvent(event);
}

void DisassemblyWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    int lh = lineHeight();
    int scrollOff = verticalScrollBar()->value();
    int lineIdx = (int)(event->position().y() / lh) + scrollOff;

    if (lineIdx >= 0 && lineIdx < m_lines.size()) {
        emit addressSelected(m_lines[lineIdx].addr);
    }
}

void DisassemblyWidget::wheelEvent(QWheelEvent *event)
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

    event->accept();
}

void DisassemblyWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_G && (event->modifiers() & Qt::ControlModifier)) {
        m_addrEdit->setFocus();
        m_addrEdit->selectAll();
    } else if (event->key() == Qt::Key_F && (event->modifiers() & Qt::ControlModifier)) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void DisassemblyWidget::contextMenuEvent(QContextMenuEvent *event)
{
    int lh = lineHeight();
    int scrollOff = verticalScrollBar()->value();
    int lineIdx = event->pos().y() / lh + scrollOff;

    QMenu menu(this);

    QAction *goTo = menu.addAction(tr("Go to address..."));
    connect(goTo, &QAction::triggered, this, [this]() {
        bool ok = false;
        QString text = QInputDialog::getText(this, tr("Go to address"),
                                              tr("Hex address:"), QLineEdit::Normal,
                                              QString(), &ok);
        if (ok) {
            uint32_t addr = text.toUInt(&ok, 16);
            if (ok) {
                pushNavHistory(m_baseAddr);
                goToAddress(addr);
            }
        }
    });

    QAction *goPC = menu.addAction(tr("Go to PC"));
    connect(goPC, &QAction::triggered, this, &DisassemblyWidget::goToPC);

    /* Load symbol file */
    QAction *loadSym = menu.addAction(tr("Load symbol file..."));
    connect(loadSym, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Load Symbol File"),
                                                     QString(), tr("Map files (*.map *.lab);;All files (*)"));
        if (!path.isEmpty()) {
            loadSymbolFile(path);
            updateLines();
            viewport()->update();
        }
    });

    if (lineIdx >= 0 && lineIdx < m_lines.size()) {
        const Line &line = m_lines[lineIdx];
        menu.addSeparator();

        /* Copy operations */
        QAction *copyAddr = menu.addAction(tr("Copy address"));
        connect(copyAddr, &QAction::triggered, this, [line]() {
            QApplication::clipboard()->setText(
                QStringLiteral("%1").arg(line.addr, 8, 16, QLatin1Char('0')));
        });

        QAction *copyInstr = menu.addAction(tr("Copy instruction"));
        connect(copyInstr, &QAction::triggered, this, [line]() {
            QApplication::clipboard()->setText(
                QStringLiteral("%1: %2 %3")
                    .arg(line.addr, 8, 16, QLatin1Char('0'))
                    .arg(line.mnemonic)
                    .arg(line.operands));
        });

        menu.addSeparator();

        QString bpText = line.has_exec_bp ? tr("Remove breakpoint") : tr("Set breakpoint");
        QAction *bp = menu.addAction(bpText);
        connect(bp, &QAction::triggered, this, [this, line]() {
            if (line.has_exec_bp)
                debug_clear_breakpoint(line.addr);
            else
                debug_set_breakpoint(line.addr, true, false, false);
            updateLines();
            viewport()->update();
        });

        QAction *readWp = menu.addAction(line.has_read_wp ? tr("Remove read watchpoint") : tr("Set read watchpoint"));
        connect(readWp, &QAction::triggered, this, [this, line]() {
            if (line.has_read_wp)
                debug_clear_breakpoint(line.addr);
            else
                debug_set_breakpoint(line.addr, false, true, false);
            updateLines();
            viewport()->update();
        });

        QAction *writeWp = menu.addAction(line.has_write_wp ? tr("Remove write watchpoint") : tr("Set write watchpoint"));
        connect(writeWp, &QAction::triggered, this, [this, line]() {
            if (line.has_write_wp)
                debug_clear_breakpoint(line.addr);
            else
                debug_set_breakpoint(line.addr, false, false, true);
            updateLines();
            viewport()->update();
        });

        menu.addSeparator();

        QAction *viewMem = menu.addAction(tr("View in memory"));
        connect(viewMem, &QAction::triggered, this, [this, line]() {
            emit addressSelected(line.addr);
        });

        QAction *runTo = menu.addAction(tr("Run to cursor"));
        connect(runTo, &QAction::triggered, this, [this, line]() {
            debug_set_breakpoint(line.addr, true, false, false);
            emit debugCommand(QStringLiteral("c"));
        });
    }

    menu.exec(event->globalPos());
}
