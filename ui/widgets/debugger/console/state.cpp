#include "ui/widgets/debugger/console/consolewidget.h"

#include <QJsonArray>

QJsonObject ConsoleWidget::serializeState() const
{
    QJsonObject state;
    state.insert(QStringLiteral("filterText"), m_filterInput ? m_filterInput->text() : m_filterText);
    if (m_output)
        state.insert(QStringLiteral("maxBlockCount"), m_output->maximumBlockCount());

    QJsonArray history;
    for (const QString &entry : m_cmdHistory)
        history.append(entry);
    state.insert(QStringLiteral("commandHistory"), history);
    return state;
}

void ConsoleWidget::restoreState(const QJsonObject &state)
{
    if (m_output) {
        const int maxBlocks = state.value(QStringLiteral("maxBlockCount")).toInt(m_output->maximumBlockCount());
        if (maxBlocks > 0)
            m_output->setMaximumBlockCount(maxBlocks);
    }

    const QString filterText = state.value(QStringLiteral("filterText")).toString();
    if (m_filterInput)
        m_filterInput->setText(filterText);
    else
        m_filterText = filterText.trimmed();

    m_cmdHistory.clear();
    const QJsonArray history = state.value(QStringLiteral("commandHistory")).toArray();
    for (const QJsonValue &value : history) {
        const QString entry = value.toString().trimmed();
        if (!entry.isEmpty())
            m_cmdHistory.append(entry);
    }
    while (m_cmdHistory.size() > MAX_HISTORY)
        m_cmdHistory.removeFirst();
    m_historyIdx = -1;
}
