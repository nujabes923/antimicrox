#ifndef ALTERNATECLOUD_H
#define ALTERNATECLOUD_H
#include <QByteArray>
#include <QString>
#include <QVariantMap>

// A portable snapshot, independent of a machine's controller ID or paths.
struct AlternateCloudBundle
{
    QString name, deviceType, updated;
    QByteArray profile;
    QVariantMap sets;
    QString hotkey;
    int activeSet = 0;
};
namespace AlternateCloud {
bool read(const QString &path, AlternateCloudBundle &bundle, QString *error);
bool write(const QString &path, const AlternateCloudBundle &bundle, QString *error);
bool portableProfile(const QByteArray &source, const QString &deviceType, QByteArray &result, QString *error,
                     bool importing = false);
}
#endif
