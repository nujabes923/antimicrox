#include "actionlibrary.h"
#include "antkeymapper.h"
#include "event.h"
#include "joybuttontypes/joybutton.h"
#include "qtconnectionhelper.h"
#include "xml/joybuttonslotxml.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <memory>

namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QByteArray serialize(JoyButtonSlot *slot)
{
    QByteArray bytes;
    QXmlStreamWriter writer(&bytes);
    JoyButtonSlotXml xml(slot);
    xml.writeConfig(&writer);
    return bytes;
}
bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QObject::tr("Could not save %1: %2").arg(path, file.errorString()));
    return true;
}
QByteArray document(const LibraryAction &action)
{
    return QJsonDocument(QJsonObject{{"schema", 1}, {"id", action.id}, {"name", action.name},
        {"tags", action.tags}, {"description", action.description}, {"favorite", action.favorite},
        {"revision", action.revision}, {"updatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"slotsXml", QString::fromUtf8(action.assignments)}}).toJson();
}
}

ActionLibrary::ActionLibrary(const QString &directory) : root(QDir(directory).absolutePath()) {}

QList<QByteArray> ActionLibrary::steps(const QByteArray &xml, QString *error)
{
    if (error) error->clear();
    QList<QByteArray> result;
    if (xml.size() > 1024 * 1024) { fail(error, QObject::tr("Action exceeds the 1 MB limit.")); return {}; }
    QXmlStreamReader validation(xml);
    static const QSet<QString> modes = {"keyboard", "mousebutton", "mousemovement", "pause", "hold", "cycle", "distance",
        "release", "mousespeedmod", "keypress", "delay", "loadprofile", "setchange", "textentry", "execute", "mix"};
    int slotCount = 0, modeCount = 0;
    while (!validation.atEnd())
    {
        validation.readNext();
        if (validation.isStartElement() && validation.name() == QStringLiteral("slot")) ++slotCount;
        if (validation.isStartElement() && validation.name() == QStringLiteral("mode"))
        {
            ++modeCount;
            if (!modes.contains(validation.readElementText()))
            { fail(error, QObject::tr("Unknown action step type.")); return {}; }
        }
    }
    if (validation.hasError() || slotCount != modeCount || slotCount > 4096)
    { fail(error, QObject::tr("Invalid action step structure.")); return {}; }
    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement() || reader.name() != QStringLiteral("slots"))
    { fail(error, QObject::tr("Invalid action steps.")); return {}; }
    while (reader.readNextStartElement())
    {
        if (reader.name() != QStringLiteral("slot") || result.size() >= 512)
        { fail(error, QObject::tr("Invalid step or more than 512 steps.")); return {}; }
        QByteArray bytes;
        QXmlStreamWriter writer(&bytes);
        int depth = 0;
        do
        {
            if (reader.isStartElement()) ++depth;
            else if (reader.isEndElement()) --depth;
            if (depth > 16) { fail(error, QObject::tr("Action nesting exceeds the supported limit.")); return {}; }
            writer.writeCurrentToken(reader);
            if (depth == 0) break;
            reader.readNext();
        } while (!reader.atEnd());
        if (reader.hasError()) break;
        // Preserve raw slot fields, including unavailable profile or executable paths.
        // The normal device parser validates such references only when assigning.
        result.append(bytes);
    }
    while (!reader.atEnd()) reader.readNext();
    if (reader.hasError()) { fail(error, reader.errorString()); return {}; }
    return result;
}

QByteArray ActionLibrary::combine(const QList<QByteArray> &items)
{
    QByteArray result("<slots>");
    for (const QByteArray &step : items) result += step;
    return result + "</slots>";
}

QByteArray ActionLibrary::snapshot(JoyButton *button)
{
    QList<QByteArray> result;
    QMetaObject::invokeMethod(button, [&]() {
        for (JoyButtonSlot *slot : *button->getAssignedSlots()) result.append(serialize(slot));
    }, synchronousConnectionType(button));
    return combine(result);
}

bool ActionLibrary::apply(JoyButton *button, const QByteArray &xml, bool append, QString *error)
{
    QString problem;
    const auto items = steps(xml, &problem);
    if (!problem.isEmpty()) return fail(error, problem);
    const QByteArray original = snapshot(button);
    bool okay = true;
    const auto previousItems = steps(original);
    const auto validationItems = append ? previousItems + items : items;
    if (validationItems.size() > 512) return fail(error, QObject::tr("Assignment exceeds 512 steps."));
    int distance = 0;
    for (const auto &item : validationItems)
    {
        JoyButtonSlot slot;
        QXmlStreamReader reader(item); reader.readNextStartElement();
        JoyButtonSlotXml parser(&slot); parser.readConfig(&reader);
        const auto mode = slot.getSlotMode();
        bool valid = !reader.hasError();
        if (mode == JoyButtonSlot::JoyKeyboard || mode == JoyButtonSlot::JoyMouseButton)
            valid &= slot.getSlotCode() > 0;
        if (mode == JoyButtonSlot::JoyDistance)
        { distance += slot.getSlotCode(); valid &= slot.getSlotCode() > 0 && distance <= 100; }
        if (mode == JoyButtonSlot::JoySetChange) valid &= slot.getSlotCode() >= 0 && slot.getSlotCode() < 8;
        if (mode == JoyButtonSlot::JoyExecute || mode == JoyButtonSlot::JoyLoadProfile || mode == JoyButtonSlot::JoyTextEntry)
            valid &= !slot.getTextData().isEmpty();
        if (mode == JoyButtonSlot::JoyMix) valid &= slot.getMixSlots()->size() >= 2;
        slot.cleanMixSlots();
        if (!valid) return fail(error, QObject::tr("Invalid step or unavailable referenced file. Existing assignments were not changed."));
    }
    QMetaObject::invokeMethod(button, [&]() {
        const QSignalBlocker blocker(button);
        auto install = [&](const QList<QByteArray> &entries, bool keep) {
            button->eventReset();
            if (!keep) button->clearSlotsEventReset();
            bool valid = true;
            for (const auto &item : entries)
            {
                JoyButtonSlot slot;
                QXmlStreamReader reader(item); reader.readNextStartElement();
                JoyButtonSlotXml parser(&slot); parser.readConfig(&reader);
                valid &= button->setAssignedSlot(&slot, button->getAssignedSlots()->size());
                slot.cleanMixSlots();
            }
            return valid;
        };
        okay = install(items, append);
        if (!okay) install(previousItems, false);
    }, synchronousConnectionType(button));
    QMetaObject::invokeMethod(button, [button]() { emit button->slotsChanged(); }, synchronousConnectionType(button));
    if (!okay) return fail(error, QObject::tr("One or more steps could not be assigned. The previous steps were restored."));
    return true;
}

bool ActionLibrary::read(const QString &path, LibraryAction &action, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
        return fail(error, QObject::tr("Cannot read action: %1").arg(path));
    QJsonParseError parse;
    const auto object = QJsonDocument::fromJson(file.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError || object.value("schema").toInt() != 1)
        return fail(error, QObject::tr("Unsupported or damaged action file: %1").arg(path));
    action.id = object.value("id").toString();
    action.name = object.value("name").toString();
    action.tags = object.value("tags").toString();
    action.description = object.value("description").toString();
    action.favorite = object.value("favorite").toBool();
    action.revision = object.value("revision").toInt();
    action.assignments = object.value("slotsXml").toString().toUtf8();
    QString problem;
    const auto entries = steps(action.assignments, &problem);
    if (QUuid(action.id).isNull() || action.name.trimmed().isEmpty() || entries.isEmpty() || !problem.isEmpty())
        return fail(error, QObject::tr("Invalid action: %1 (%2)").arg(path, problem));
    return true;
}

QList<LibraryAction> ActionLibrary::list(QString *error) const
{
    if (error) error->clear();
    QList<LibraryAction> actions;
    for (const QString &file : QDir(root).entryList({"*.json"}, QDir::Files, QDir::Name))
    {
        LibraryAction action;
        QString problem;
        if (read(QDir(root).filePath(file), action, &problem)) actions.append(action);
        else if (error) *error += problem + "\n";
    }
    return actions;
}

bool ActionLibrary::save(LibraryAction &action, QString *error)
{
    if (!QDir().mkpath(root)) return fail(error, QObject::tr("Cannot create action library directory."));
    QLockFile lock(QDir(root).filePath(".write-lock"));
    if (!lock.tryLock(1000)) return fail(error, QObject::tr("Action library is being saved in another window. Try again."));
    QString problem;
    if (action.name.trimmed().isEmpty() || steps(action.assignments, &problem).isEmpty() || !problem.isEmpty())
        return fail(error, QObject::tr("Enter a name and at least one valid step. %1").arg(problem));
    LibraryAction updated = action;
    if (updated.id.isEmpty()) updated.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (QUuid(updated.id).isNull()) return fail(error, QObject::tr("Invalid action ID."));
    updated.id = QUuid(updated.id).toString(QUuid::WithoutBraces);
    const QString path = QDir(root).filePath(updated.id + ".json");
    if (QFileInfo::exists(path))
    {
        LibraryAction previous;
        if (!read(path, previous, error)) return false;
        if (previous.revision != action.revision)
            return fail(error, QObject::tr("This action changed in another window. Reload it or save a duplicate."));
        const QString history = QDir(root).filePath(".history/" + updated.id);
        if (!QDir().mkpath(history) || !QFile::copy(path, QDir(history).filePath(
            QString::number(previous.revision) + "-" + QUuid::createUuid().toString(QUuid::Id128) + ".json")))
            return fail(error, QObject::tr("Could not back up the previous action version. Save cancelled."));
    } else if (action.revision != 0)
        return fail(error, QObject::tr("Original action file is missing. Save a duplicate instead."));
    updated.revision++;
    updated.name = updated.name.trimmed();
    if (!writeFile(path, document(updated), error)) return false;
    action = updated;
    return true;
}

bool ActionLibrary::exportAction(const QString &path, const LibraryAction &action, QString *error)
{
    LibraryAction exported = action;
    if (exported.id.isEmpty()) exported.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return writeFile(path, document(exported), error);
}

QByteArray ActionLibrary::example()
{
    QList<QByteArray> items;
    for (int key : {Qt::Key_Left, Qt::Key_Right})
    {
        JoyButtonSlot hold(300, JoyButtonSlot::JoyKeyPress);
        JoyButtonSlot press(AntKeyMapper::getInstance()->returnVirtualKey(key), key, JoyButtonSlot::JoyKeyboard);
        JoyButtonSlot pause(200, JoyButtonSlot::JoyPause);
        items << serialize(&hold) << serialize(&press) << serialize(&pause);
    }
    return combine(items);
}

bool ActionLibrary::preview(const QByteArray &xml, QList<ActionPreviewEvent> &events, int &duration, QString *error,
                            bool sampleRandom)
{
    events.clear(); duration = 0;
    QString problem;
    const auto items = steps(xml, &problem);
    if (!problem.isEmpty() || items.isEmpty()) return fail(error, QObject::tr("Add valid steps before previewing. %1").arg(problem));
    QList<ActionPreviewEvent> held;
    int pressTime = 100, pressedAt = 0;
    auto release = [&]() {
        if (held.isEmpty()) return;
        duration = qMax(duration, pressedAt + pressTime);
        for (auto item = held.crbegin(); item != held.crend(); ++item)
        { auto event = *item; event.time = duration; event.pressed = false; events.append(event); }
        held.clear();
    };
    auto press = [&](JoyButtonSlot *slot) {
        if (held.isEmpty()) pressedAt = duration;
        for (const auto &event : held)
            if (event.code == slot->getSlotCode() && event.mode == slot->getSlotMode()) return;
        ActionPreviewEvent event{duration, slot->getSlotCode(), slot->getSlotCodeAlias(), int(slot->getSlotMode()), true,
                                 slot->getSlotString()};
        events.append(event); held.append(event);
    };
    for (int index = 0; index < items.size(); ++index)
    {
        JoyButtonSlot slot;
        QXmlStreamReader reader(items.at(index)); reader.readNextStartElement();
        JoyButtonSlotXml parser(&slot); parser.readConfig(&reader);
        switch (slot.getSlotMode())
        {
        case JoyButtonSlot::JoyKeyboard:
        case JoyButtonSlot::JoyMouseButton: press(&slot); break;
        case JoyButtonSlot::JoyMix:
            for (JoyButtonSlot *key : *slot.getMixSlots())
            {
                if (key->getSlotMode() != JoyButtonSlot::JoyKeyboard)
                { slot.cleanMixSlots(); return fail(error, QObject::tr("Preview supports keyboard chords only.")); }
                press(key);
            }
            break;
        case JoyButtonSlot::JoyKeyPress: release(); pressTime = qBound(1, slot.getSlotCode(), 600000); break;
        case JoyButtonSlot::JoyPause: release(); duration += qBound(0, slot.getSlotCode(), 600000); break;
        case JoyButtonSlot::JoyDelay:
        {
            int low = slot.isUsingRandomDelay() ? qMax(10, qMin(slot.getRandomDelayMinimum(), slot.getRandomDelayMaximum()))
                                               : slot.getSlotCode();
            int high = slot.isUsingRandomDelay() ? qMax(low, qMax(slot.getRandomDelayMinimum(), slot.getRandomDelayMaximum())) : low;
            low = qBound(0, low, 600000); high = qBound(low, high, 600000);
            duration += sampleRandom ? QRandomGenerator::global()->bounded(low, high + 1) : high;
            break;
        }
        default:
            slot.cleanMixSlots();
            return fail(error, QObject::tr("Step %1 uses an advanced action. It can be saved and assigned, but cannot be trialled here.").arg(index + 1));
        }
        slot.cleanMixSlots();
        if (duration > 600000) return fail(error, QObject::tr("Trial is limited to 10 minutes."));
    }
    release();
    if (duration > 600000) return fail(error, QObject::tr("Trial is limited to 10 minutes."));
    return true;
}
