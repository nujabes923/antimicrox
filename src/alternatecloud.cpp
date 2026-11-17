#include "alternatecloud.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool validGroups(const QVariantList &groups)
{
    if (groups.size() < 2 || groups.size() > 256) return false;
    for (const QVariant &value : groups) {
        const QVariantMap row = value.toMap();
        if (row.value("Button").toString().isEmpty()) return false;
        if (row.value("PressCount", 0).toInt() < 0 || row.value("PressCount", 0).toInt() > 10000) return false;
    }
    return true;
}
bool validSets(const QVariantMap &sets)
{
    if (sets.size() != 8) return false;
    for (int i = 1; i <= 8; ++i) {
        const QString key = QString::number(i);
        if (!sets.contains(key)) return false;
        const QVariantMap set = sets.value(key).toMap();
        if (set.size() > 4096) return false;
        for (auto it = set.cbegin(); it != set.cend(); ++it) {
            const QStringList parts = it.key().split('/');
            if (parts.contains("..") || parts.contains(".") || parts.contains("")) return false;
        }
        if (set.contains("Groups/size") && (set.value("Groups/size").toInt() < 2 || set.value("Groups/size").toInt() > 256)) return false;
        const QVariantMap schemes = set.value("Presets").toMap();
        if (schemes.size() > 256) return false;
        for (auto it = schemes.cbegin(); it != schemes.cend(); ++it) {
            const QVariantMap scheme = it.value().toMap();
            if (it.key().trimmed().isEmpty() || !validGroups(scheme.isEmpty() ? it.value().toList() : scheme.value("Groups").toList())) return false;
        }
    }
    return true;
}
}

bool AlternateCloud::portableProfile(const QByteArray &source, const QString &deviceType, QByteArray &result,
                                    QString *error, bool importing)
{
    if (source.isEmpty() || source.size() > 8 * 1024 * 1024) return fail(error, "Profile is empty or too large.");
    QXmlStreamReader reader(source);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    bool root = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::DTD || reader.tokenType() == QXmlStreamReader::EntityReference)
            return fail(error, "Profile entities are not supported.");
        if (reader.isStartElement()) {
            if (!root) {
                if (reader.name() != deviceType || (deviceType != "gamecontroller" && deviceType != "joystick"))
                    return fail(error, "Controller profile type does not match the bundle.");
                root = true;
            }
            if (reader.name() == QStringLiteral("path")) {
                QString path = reader.readElementText();
                QString normalized = path; normalized.replace('\\', '/');
                if (QFileInfo(normalized).fileName().compare("ActionRecorder.exe", Qt::CaseInsensitive) == 0)
                    path = importing ? QCoreApplication::applicationDirPath() + "/ActionRecorder.exe" : "ActionRecorder.exe";
                else if (importing && !QFileInfo::exists(path))
                    return fail(error, "This profile needs an external program that is missing on this machine: " + path);
                if (importing && !QFileInfo::exists(path)) return fail(error, "ActionRecorder.exe is missing from this installation.");
                writer.writeTextElement("path", path);
                continue;
            }
        }
        writer.writeCurrentToken(reader);
    }
    if (reader.hasError() || writer.hasError() || !root) return fail(error, "Invalid controller profile XML.");
    result = output;
    return true;
}

bool AlternateCloud::write(const QString &path, const AlternateCloudBundle &bundle, QString *error)
{
    QByteArray profile;
    if (bundle.name.trimmed().isEmpty() || bundle.activeSet < 0 || bundle.activeSet > 7 || !validSets(bundle.sets))
        return fail(error, "Invalid scheme bundle settings.");
    if (!portableProfile(bundle.profile, bundle.deviceType, profile, error)) return false;
    QJsonObject json;
    json["format"] = "antimicrox-alternate-cloud"; json["version"] = 1;
    json["name"] = bundle.name; json["deviceType"] = bundle.deviceType;
    json["updated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    json["activeSet"] = bundle.activeSet; json["hotkey"] = bundle.hotkey;
    json["profile"] = QString::fromUtf8(profile); json["sets"] = QJsonObject::fromVariantMap(bundle.sets);
    const QByteArray bytes = QJsonDocument(json).toJson();
    if (bytes.size() > 16 * 1024 * 1024) return fail(error, "Scheme bundle exceeds 16 MB.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(error, file.errorString());
    return true;
}

bool AlternateCloud::read(const QString &path, AlternateCloudBundle &bundle, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > 16 * 1024 * 1024) return fail(error, "Scheme bundle exceeds 16 MB.");
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject json = document.object();
    if (parse.error != QJsonParseError::NoError || json.value("format").toString() != "antimicrox-alternate-cloud" ||
        json.value("version").toInt() != 1) return fail(error, "Not a supported Alternate cloud bundle (version 1 required).");
    AlternateCloudBundle candidate;
    candidate.name = json.value("name").toString(); candidate.deviceType = json.value("deviceType").toString();
    candidate.updated = json.value("updated").toString(); candidate.activeSet = json.value("activeSet").toInt(-1);
    candidate.hotkey = json.value("hotkey").toString(); candidate.sets = json.value("sets").toObject().toVariantMap();
    if (candidate.name.trimmed().isEmpty() || candidate.activeSet < 0 || candidate.activeSet > 7 || !validSets(candidate.sets))
        return fail(error, "Incomplete or invalid Alternate settings.");
    if (!portableProfile(json.value("profile").toString().toUtf8(), candidate.deviceType, candidate.profile, error)) return false;
    bundle = candidate;
    return true;
}
