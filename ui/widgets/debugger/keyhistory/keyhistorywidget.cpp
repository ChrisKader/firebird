#include "keyhistorywidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QFileDialog>
#include <QTextStream>
#include <QAction>
#include <QSet>
#include <QJsonObject>

KeyHistoryWidget::KeyHistoryWidget(QWidget *parent)
    : QWidget(parent)
{
    m_elapsed.start();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    /* Toolbar */
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    auto *filterLabel = new QLabel(tr("Filter:"), this);
    m_toolbar->addWidget(filterLabel);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("key name..."));
    m_filterEdit->setMaximumWidth(120);
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &KeyHistoryWidget::applyFilter);
    m_toolbar->addWidget(m_filterEdit);

    m_toolbar->addSeparator();

    auto *sizeLabel = new QLabel(tr("Size:"), this);
    m_toolbar->addWidget(sizeLabel);

    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(6, 24);
    m_fontSizeSpin->setValue(9);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int size) {
        QFont f = m_textEdit->font();
        f.setPointSize(size);
        m_textEdit->setFont(f);
    });
    m_toolbar->addWidget(m_fontSizeSpin);

    m_toolbar->addSeparator();

    QAction *exportAct = m_toolbar->addAction(QStringLiteral("Save"));
    exportAct->setToolTip(tr("Export history to CSV"));
    connect(exportAct, &QAction::triggered, this, &KeyHistoryWidget::exportHistory);

    QAction *clearAct = m_toolbar->addAction(QStringLiteral("Clear"));
    connect(clearAct, &QAction::triggered, this, &KeyHistoryWidget::clear);

    layout->addWidget(m_toolbar);

    /* Text display */
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setMaximumBlockCount(m_maxEntries);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    m_textEdit->setFont(mono);

    layout->addWidget(m_textEdit);

    /* Stats label */
    m_statsLabel = new QLabel(this);
    m_statsLabel->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(m_statsLabel);
}

QJsonObject KeyHistoryWidget::serializeState() const
{
    QJsonObject state;
    if (m_filterEdit)
        state.insert(QStringLiteral("filterText"), m_filterEdit->text());
    if (m_fontSizeSpin)
        state.insert(QStringLiteral("fontSize"), m_fontSizeSpin->value());
    return state;
}

void KeyHistoryWidget::restoreState(const QJsonObject &state)
{
    if (m_fontSizeSpin) {
        const int size = state.value(QStringLiteral("fontSize")).toInt(m_fontSizeSpin->value());
        if (size >= m_fontSizeSpin->minimum() && size <= m_fontSizeSpin->maximum())
            m_fontSizeSpin->setValue(size);
    }
    if (m_filterEdit)
        m_filterEdit->setText(state.value(QStringLiteral("filterText")).toString());
}

void KeyHistoryWidget::addEntry(const QString &keyName, bool pressed)
{
    qint64 ms = m_elapsed.elapsed();

    Entry e;
    e.elapsed_ms = ms;
    e.keyName = keyName;
    e.pressed = pressed;
    m_entries.append(e);

    if (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();

    if (pressed) {
        m_totalPresses++;
        m_uniqueKeys.insert(keyName);
    }

    /* Check filter */
    QString filter = m_filterEdit->text();
    if (!filter.isEmpty() && !keyName.contains(filter, Qt::CaseInsensitive))
        goto update_stats;

    {
        /* Format timestamp */
        int secs = (int)(ms / 1000);
        int msec = (int)(ms % 1000);
        QString timestamp = QStringLiteral("[%1:%2.%3]")
            .arg(secs / 60, 2, 10, QLatin1Char('0'))
            .arg(secs % 60, 2, 10, QLatin1Char('0'))
            .arg(msec, 3, 10, QLatin1Char('0'));

        QString prefix = pressed ? QStringLiteral("\u25BC ") : QStringLiteral("\u25B2 ");
        m_textEdit->appendPlainText(timestamp + QStringLiteral(" ") + prefix + keyName);

        /* Auto-scroll */
        QTextCursor cursor = m_textEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_textEdit->setTextCursor(cursor);
    }

update_stats:
    m_statsLabel->setText(QStringLiteral("Keys pressed: %1, Unique: %2")
        .arg(m_totalPresses).arg(m_uniqueKeys.size()));
}

void KeyHistoryWidget::clear()
{
    m_textEdit->clear();
    m_entries.clear();
    m_totalPresses = 0;
    m_uniqueKeys.clear();
    m_elapsed.restart();
    m_statsLabel->setText(QString());
}

void KeyHistoryWidget::applyFilter()
{
    rebuildDisplay();
}

void KeyHistoryWidget::rebuildDisplay()
{
    m_textEdit->clear();
    QString filter = m_filterEdit->text();

    for (const auto &e : m_entries) {
        if (!filter.isEmpty() && !e.keyName.contains(filter, Qt::CaseInsensitive))
            continue;

        int secs = (int)(e.elapsed_ms / 1000);
        int msec = (int)(e.elapsed_ms % 1000);
        QString timestamp = QStringLiteral("[%1:%2.%3]")
            .arg(secs / 60, 2, 10, QLatin1Char('0'))
            .arg(secs % 60, 2, 10, QLatin1Char('0'))
            .arg(msec, 3, 10, QLatin1Char('0'));

        QString prefix = e.pressed ? QStringLiteral("\u25BC ") : QStringLiteral("\u25B2 ");
        m_textEdit->appendPlainText(timestamp + QStringLiteral(" ") + prefix + e.keyName);
    }
}

void KeyHistoryWidget::exportHistory()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Key History"),
        QString(), tr("CSV Files (*.csv);;Text Files (*.txt)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);

    if (path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        out << "Timestamp_ms,Key,Action\n";
        for (const auto &e : m_entries) {
            out << e.elapsed_ms << ","
                << "\"" << e.keyName << "\","
                << (e.pressed ? "press" : "release") << "\n";
        }
    } else {
        for (const auto &e : m_entries) {
            int secs = (int)(e.elapsed_ms / 1000);
            int msec = (int)(e.elapsed_ms % 1000);
            QString prefix = e.pressed ? QStringLiteral("\u25BC ") : QStringLiteral("\u25B2 ");
            out << QStringLiteral("[%1:%2.%3] %4%5\n")
                .arg(secs / 60, 2, 10, QLatin1Char('0'))
                .arg(secs % 60, 2, 10, QLatin1Char('0'))
                .arg(msec, 3, 10, QLatin1Char('0'))
                .arg(prefix)
                .arg(e.keyName);
        }
    }
}
