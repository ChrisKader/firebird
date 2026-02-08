#include "keyhistorywidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFontDatabase>

KeyHistoryWidget::KeyHistoryWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setMaximumBlockCount(m_maxEntries);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    m_textEdit->setFont(mono);

    layout->addWidget(m_textEdit);

    auto *controlLayout = new QHBoxLayout;

    auto *fontLabel = new QLabel(tr("Size:"), this);
    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(6, 24);
    m_fontSizeSpin->setValue(9);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int size) {
        QFont f = m_textEdit->font();
        f.setPointSize(size);
        m_textEdit->setFont(f);
    });

    m_clearBtn = new QPushButton(tr("Clear"), this);
    connect(m_clearBtn, &QPushButton::clicked, this, &KeyHistoryWidget::clear);

    controlLayout->addWidget(fontLabel);
    controlLayout->addWidget(m_fontSizeSpin);
    controlLayout->addStretch();
    controlLayout->addWidget(m_clearBtn);
    layout->addLayout(controlLayout);
}

void KeyHistoryWidget::addEntry(const QString &keyName, bool pressed)
{
    QString prefix = pressed ? QStringLiteral("\u25BC ") : QStringLiteral("\u25B2 ");
    m_textEdit->appendPlainText(prefix + keyName);

    /* Auto-scroll */
    QTextCursor cursor = m_textEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_textEdit->setTextCursor(cursor);
}

void KeyHistoryWidget::clear()
{
    m_textEdit->clear();
}
