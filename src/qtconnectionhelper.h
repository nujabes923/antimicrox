#ifndef QTCONNECTIONHELPER_H
#define QTCONNECTIONHELPER_H

#include <QObject>
#include <QThread>

// Offline devices belong to the GUI thread, while physical devices normally
// belong to the input thread. Synchronous edits must support both arrangements.
inline Qt::ConnectionType synchronousConnectionType(const QObject *receiver)
{
    return receiver->thread() == QThread::currentThread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
}

#endif
