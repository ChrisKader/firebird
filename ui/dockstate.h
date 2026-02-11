#ifndef DOCKSTATE_H
#define DOCKSTATE_H

#include <QJsonObject>
#include <QString>

struct DockState {
    QString dockId;
    QJsonObject customState;
};

class DockStateSerializable
{
public:
    virtual ~DockStateSerializable() = default;
    virtual QJsonObject serializeState() const = 0;
    virtual void restoreState(const QJsonObject &state) = 0;
};

#endif // DOCKSTATE_H
