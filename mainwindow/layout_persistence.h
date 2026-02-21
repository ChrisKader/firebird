#ifndef MAINWINDOW_LAYOUT_PERSISTENCE_H
#define MAINWINDOW_LAYOUT_PERSISTENCE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class QMainWindow;

QByteArray serializeDockLayout(QMainWindow *window);
bool restoreDockLayout(QMainWindow *window, const QByteArray &layoutData, QString *errorOut = nullptr);
QJsonObject makeDockLayoutJson(QMainWindow *window);
QString layoutProfilesDirPath();
QString layoutProfilePath(const QString &profileName);
bool ensureLayoutProfilesDir(QString *errorOut = nullptr);
bool saveLayoutProfile(QMainWindow *window,
                       const QString &profileName,
                       const QJsonObject &debugDockState = QJsonObject(),
                       const QJsonObject &coreDockConnections = QJsonObject(),
                       QString *errorOut = nullptr);
bool restoreLayoutProfile(QMainWindow *window,
                          const QString &profileName,
                          QString *errorOut = nullptr,
                          QJsonObject *debugDockStateOut = nullptr,
                          QJsonObject *coreDockConnectionsOut = nullptr);

#endif
