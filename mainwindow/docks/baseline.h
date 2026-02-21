#ifndef MAINWINDOW_DOCKS_BASELINE_H
#define MAINWINDOW_DOCKS_BASELINE_H

#include <QByteArray>
#include <QJsonObject>

QByteArray makeBaselineKddLayoutBytes();
QJsonObject makeBaselineDebugDockStateObject();
QJsonObject makeBaselineCoreDockConnectionsObject();

#endif
