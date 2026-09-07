// Standalone regression test linked against the application objects (see run_offline_controls.py).
#define SDL_MAIN_HANDLED
#include "advancebuttondialog.h"
#include "antimicrosettings.h"
#include "antkeymapper.h"
#include "eventhandlerfactory.h"
#include "eventhandlers/baseeventhandler.h"
#include "gamecontroller/gamecontroller.h"
#include "joybuttontypes/joybutton.h"
#include "joytabwidget.h"
#include "logger.h"
#include "xmlconfigwriter.h"
#include "xml/inputdevicexml.h"
#include "mainwindow.h"
#include "commandlineutility.h"
#include "actionlibrary.h"
#include "actionlibrarydialog.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QAction>
#include <qt_windows.h>
#include "setjoystick.h"
#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QLabel>
#include <QDir>
#include <QFontDatabase>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char *message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    std::fprintf(stderr, "OK: %s\n", message);
}

// Record generated keys without sending anything to the user's desktop.
class RecordingHandler : public BaseEventHandler
{
public:
    struct KeyEvent { int code; bool down; qint64 time; };
    QList<KeyEvent> keyEvents;
    int presses = 0, releases = 0;
    bool init() override { return true; }
    bool cleanup() override { return true; }
    void sendKeyboardEvent(JoyButtonSlot *slot, bool down) override
    { down ? ++presses : ++releases; keyEvents.append({slot->getSlotCode(), down, QDateTime::currentMSecsSinceEpoch()}); }
    void sendMouseButtonEvent(JoyButtonSlot *, bool) override {}
    void sendMouseEvent(int, int) override {}
    QString getName() override { return "Recording"; }
    QString getIdentifier() override { return "sendinput"; }
};

class RecordingFactory : public EventHandlerFactory
{
public:
    RecordingFactory() : EventHandlerFactory("sendinput")
    {
        delete eventHandler;
        eventHandler = new RecordingHandler;
        instance = this;
    }
    RecordingHandler *recorder() { return static_cast<RecordingHandler *>(eventHandler); }
};

static void pump(int milliseconds = 30)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
    app.setFont(QFont("Segoe UI", 9));
    QTextStream logStream(stderr);
    Logger::createInstance(&logStream, Logger::LOG_ERROR);
    RecordingFactory factory;
    AntKeyMapper::getInstance("sendinput");
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory");
    AntiMicroSettings settings(temporary.filePath("settings.ini"), QSettings::IniFormat);
    GameController controller(nullptr, 0, &settings, 0, nullptr);
    controller.setOfflineUniqueID("regression-offline");
    require(controller.getController() == nullptr, "no physical controller required");
    JoyButton *first = controller.getActiveSetJoystick()->getJoyButton(0);
    if (app.arguments().contains("--native-hotkey"))
    {
        QMap<SDL_JoystickID, InputDevice *> devices;
        CommandLineUtility commandLine;
        MainWindow window(&devices, &commandLine, &settings, false);
        JoyTabWidget tab(&controller, &settings, &window);
        auto *action = window.findChild<QAction *>("emergencyStopAction");
        require(action != nullptr && action->text().contains("Ctrl+Alt+F10"), "Windows global hotkey registered");
        first->setAssignedSlot(0x41, JoyButtonSlot::JoyKeyboard);
        first->setToggle(true);
        first->setExternalToggleState(true);
        pump();
        require(first->getButtonState(), "native hotkey test has active toggle");
        PostMessage(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, 0x4a51, 0);
        pump(100);
        require(!first->getButtonState() && factory.recorder()->presses == factory.recorder()->releases,
                "Windows hotkey message reaches emergency stop and releases keys");
        std::puts("PASS: Windows global hotkey registration and native message dispatch");
        return 0;
    }
    {
        AdvanceButtonDialog editor(first);
        auto *list = editor.findChild<QListWidget *>("slotListWidget");
        require(list != nullptr, "assignment editor list");
        editor.clearAllSlots(); // Previously deadlocked on the GUI thread.
        list->setCurrentRow(0);
        editor.placeNewSlot(new JoyButtonSlot(0x41, JoyButtonSlot::JoyKeyboard));
        pump();
        list->setCurrentRow(1);
        editor.placeNewSlot(new JoyButtonSlot(0x42, JoyButtonSlot::JoyKeyboard));
        pump();
        require(first->getAssignedSlots()->size() == 2, "offline combination insertion");
        list->setCurrentRow(0);
        editor.placeNewSlot(new JoyButtonSlot(0x43, JoyButtonSlot::JoyKeyboard));
        pump();
        require(first->getAssignedSlots()->first()->getSlotCode() == 0x43, "offline assignment replacement");
        editor.clearAllSlots();
        require(first->getAssignedSlots()->isEmpty(), "offline combination clear");
        pump();
        editor.undoSlots();
        require(first->getAssignedSlots()->size() == 2 && first->getAssignedSlots()->first()->getSlotCode() == 0x43,
                "undo restores cleared combination");
        pump();
        editor.redoSlots();
        require(first->getAssignedSlots()->isEmpty(), "redo clears restored combination");
        pump();
        editor.undoSlots();
        pump();
        list->setCurrentRow(0);
        editor.placeNewSlot(new JoyButtonSlot(0x45, JoyButtonSlot::JoyKeyboard));
        pump();
        require(!editor.findChild<QPushButton *>("redoSlotsButton")->isEnabled(), "new edit discards redo branch");
        editor.clearAllSlots();
        pump();
        list->setCurrentRow(0);
        editor.placeNewSlot(new JoyButtonSlot(200, JoyButtonSlot::JoyDelay));
        first->updateAssignedDelaySlot(0, 200, true, 120, 280);
        pump();
        editor.clearAllSlots();
        pump();
        editor.undoSlots();
        require(first->getAssignedSlots()->size() == 1 && first->getAssignedSlots()->first()->isUsingRandomDelay() &&
                first->getAssignedSlots()->first()->getRandomDelayMinimum() == 120 &&
                first->getAssignedSlots()->first()->getRandomDelayMaximum() == 280,
                "undo preserves random delay settings");
        const QString artifacts = qEnvironmentVariable("ANTIMICROX_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty())
        {
            editor.show();
            pump();
            editor.grab().save(QDir(artifacts).filePath("usability-editor.png"));
        }
        editor.clearAllSlots();
        pump();
        auto *mix = new JoyButtonSlot(0, JoyButtonSlot::JoyMix, first);
        mix->setTextData("A+B");
        mix->appendMiniSlot(new JoyButtonSlot(0x41, JoyButtonSlot::JoyKeyboard));
        mix->appendMiniSlot(new JoyButtonSlot(0x42, JoyButtonSlot::JoyKeyboard));
        require(first->insertAssignedSlot(mix), "create simultaneous combination");
        pump();
        editor.clearAllSlots();
        pump();
        editor.undoSlots();
        require(first->getAssignedSlots()->size() == 1 &&
                first->getAssignedSlots()->first()->getSlotMode() == JoyButtonSlot::JoyMix &&
                first->getAssignedSlots()->first()->getMixSlots()->size() == 2,
                "undo preserves simultaneous combination members");
        editor.clearAllSlots();
    }
    pump();
    first->setAssignedSlot(0x41, JoyButtonSlot::JoyKeyboard);
    first->setToggle(true);
    first->setUseTurbo(true);
    first->setTurboInterval(30);
    JoyButton *second = controller.getActiveSetJoystick()->getJoyButton(1);
    second->setAssignedSlot(0x42, JoyButtonSlot::JoyKeyboard);
    const QString group = "AlternatingSuites/regression-offline/Set1/";
    settings.setValue(group + "FirstButton", "button/0");
    settings.setValue(group + "SecondButton", "button/1");
    for (const QString &key : {"FirstMinimum", "FirstMaximum", "SecondMinimum", "SecondMaximum"})
        settings.setValue(group + key, 100);
    {
        JoyTabWidget tab(&controller, &settings);
        auto *start = tab.findChild<QPushButton *>("alternatingSuitesStartButton");
        require(start != nullptr, "alternate start button");
        start->click();
        pump(50);
        require(start->isChecked() && first->getButtonState(), "offline alternate starts first suite");
        require(factory.recorder()->presses > 0, "offline alternate generates keyboard output");
        pump(100);
        require(!first->getButtonState() && second->getButtonState(), "timer switches to second suite");
        start->click();
        pump();
        require(!first->getButtonState() && !second->getButtonState(), "stop releases both suites");
        require(factory.recorder()->presses == factory.recorder()->releases, "no keys left held after stop");
        require(QMetaObject::invokeMethod(&tab, "showAlternatingSuitesPopup"), "open alternate popup");
        QWidget *popup = nullptr;
        for (QWidget *window : QApplication::topLevelWidgets())
            if (window->objectName() == "alternatingSuitesPopup") popup = window;
        require(popup != nullptr, "alternate popup available offline");
        QPushButton *popupStart = nullptr;
        QPushButton *popupStop = nullptr;
        for (QPushButton *button : popup->findChildren<QPushButton *>())
        {
            if (button->text() == "Start") popupStart = button;
            if (button->text() == "Stop") popupStop = button;
        }
        require(popupStart && popupStop, "popup start and stop controls");
        settings.setValue(group + "FirstMinimum", 3000);
        settings.setValue(group + "FirstMaximum", 3000);
        popupStart->click();
        pump(30);
        require(start->isChecked() && first->getButtonState(), "popup starts offline alternate");
        require(popup->findChild<QLabel *>()->text().contains("Remaining:") &&
                popup->findChild<QLabel *>()->text().contains("Next"), "countdown shows remaining time and next suite");
        const QString artifacts = qEnvironmentVariable("ANTIMICROX_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty())
            popup->grab().save(QDir(artifacts).filePath("usability-popup.png"));
        popupStop->click();
        pump(30);
        require(!start->isChecked() && !first->getButtonState(), "popup stops offline alternate");
        JoyButton *independent = controller.getActiveSetJoystick()->getJoyButton(2);
        independent->setAssignedSlot(0x43, JoyButtonSlot::JoyKeyboard);
        independent->setToggle(true);
        independent->setExternalToggleState(true);
        popupStart->click();
        pump(30);
        auto *emergency = popup->findChild<QPushButton *>("emergencyStopButton");
        require(emergency != nullptr, "emergency stop control");
        emergency->click();
        pump(30);
        require(!start->isChecked() && !first->getButtonState() && !independent->getButtonState(),
                "emergency stops rotation and independent toggle");
        require(factory.recorder()->presses == factory.recorder()->releases, "emergency releases all output keys");
        popupStart->click();
        pump(30);
    }
    pump();
    require(!first->getButtonState() && !second->getButtonState(), "closing offline tab releases keys");
    require(factory.recorder()->presses == factory.recorder()->releases, "no keys left held after closing tab");
    {
        const QString profile = temporary.filePath("profile.amgp");
        require(QFile::copy("user-config/maple-max.gamecontroller.amgp", profile), "copy profile fixture");
        QFile original(profile);
        require(original.open(QIODevice::ReadOnly), "read original profile");
        const QByteArray originalBytes = original.readAll();
        original.close();
        JoyTabWidget tab(&controller, &settings);
        tab.loadConfigFile(profile);
        pump();
        JoyButton *button = controller.getActiveSetJoystick()->getJoyButton(0);
        {
            AdvanceButtonDialog editor(button);
            editor.clearAllSlots();
            auto *list = editor.findChild<QListWidget *>("slotListWidget");
            list->setCurrentRow(0);
            editor.placeNewSlot(new JoyButtonSlot(0x44, JoyButtonSlot::JoyKeyboard));
        }
        require(QMetaObject::invokeMethod(&tab, "saveConfigFile"), "save edited offline profile");
        const QDir backups(temporary.filePath(".antimicrox-backups"));
        const QStringList backupNames = backups.entryList(QDir::Files);
        require(backupNames.size() == 1, "save creates one versioned backup");
        QFile backup(backups.filePath(backupNames.first()));
        require(backup.open(QIODevice::ReadOnly) && backup.readAll() == originalBytes, "backup exactly preserves previous profile");
        button->clearSlotsEventReset();
        controller.revertProfileEdited(); // Discard the deliberate in-memory mutation without an interactive prompt.
        tab.unloadConfig();
        pump();
        tab.loadConfigFile(profile);
        pump();
        button = controller.getActiveSetJoystick()->getJoyButton(0);
        require(button->getAssignedSlots()->size() == 1 && button->getAssignedSlots()->first()->getSlotCode() == 0x44,
                "offline combination survives save and reload");
        tab.saveDeviceSettings(true);
        require(settings.value("OfflineControllerID").toString() == "regression-offline", "remember offline identity");
    }
    {
        QTemporaryDir blocked;
        const QString profile = blocked.filePath("profile.amgp");
        QFile file(profile);
        require(file.open(QIODevice::WriteOnly), "create backup failure fixture");
        file.write("original profile bytes");
        file.close();
        QFile obstruction(blocked.filePath(".antimicrox-backups"));
        require(obstruction.open(QIODevice::WriteOnly), "block backup directory creation");
        obstruction.close();
        XMLConfigWriter writer;
        writer.setFileName(profile);
        InputDeviceXml deviceXml(&controller);
        writer.write(&deviceXml);
        require(writer.hasError(), "backup failure prevents save");
        require(file.open(QIODevice::ReadOnly) && file.readAll() == "original profile bytes",
                "backup failure preserves original file");
    }
    {
        const QString directory = temporary.filePath("action-library");
        ActionLibrary library(directory);
        LibraryAction action;
        action.name = QString::fromUtf8("原地左右防呆 1s");
        action.tags = "Movement"; action.favorite = true;
        action.assignments = ActionLibrary::example();
        QString error;
        require(ActionLibrary::steps(action.assignments, &error).size() == 6 && error.isEmpty(), "one-second example has six steps");
        QList<ActionPreviewEvent> preview; int duration = 0;
        require(ActionLibrary::preview(action.assignments, preview, duration, &error) && duration == 1000,
                "example preview is exactly one second");
        require(preview.size() == 4 && preview.at(0).time == 0 && preview.at(1).time == 300 &&
                preview.at(2).time == 500 && preview.at(3).time == 800, "preview press/release timeline matches holds and waits");
        {
            GameController replay(nullptr, 0, &settings, 0, nullptr);
            JoyButton *button = replay.getActiveSetJoystick()->getJoyButton(0);
            require(ActionLibrary::apply(button, action.assignments, false, &error), "apply example to native sequence engine");
            const int begin = factory.recorder()->keyEvents.size();
            button->setExternalToggleState(true); pump(1150); button->setExternalToggleState(false); pump();
            const auto observed = factory.recorder()->keyEvents.mid(begin);
            require(observed.size() == 4 && observed.at(0).down && !observed.at(1).down &&
                    observed.at(2).down && !observed.at(3).down, "native example sends two complete presses");
            const auto held = observed.at(1).time - observed.at(0).time;
            const auto next = observed.at(2).time - observed.at(0).time;
            require(held >= 250 && held < 450 && next >= 450 && next < 650, "native example timing agrees with preview");
        }
        require(library.save(action, &error) && action.revision == 1 && !action.id.isEmpty(), "save independent action");
        const LibraryAction stale = action;
        action.description = "Updated description";
        require(library.save(action, &error) && action.revision == 2, "save action revision");
        LibraryAction conflict = stale;
        require(!library.save(conflict, &error), "stale action edit cannot overwrite newer version");
        require(QDir(directory + "/.history/" + action.id).entryList(QDir::Files).size() == 1, "action revision backed up");
        const auto saved = library.list(&error);
        require(saved.size() == 1 && saved.first().favorite && saved.first().name == action.name &&
                saved.first().assignments == action.assignments, "load action metadata and full steps");
        const QString exported = temporary.filePath("exported-action.json");
        require(ActionLibrary::exportAction(exported, action, &error), "export action");
        LibraryAction imported;
        require(ActionLibrary::read(exported, imported, &error) && imported.assignments == action.assignments, "import action losslessly");
        imported.id.clear(); imported.revision = 0; imported.name += " copy";
        require(library.save(imported, &error) && library.list().size() == 2, "import as new action preserves original");
        JoyButton *target = controller.getActiveSetJoystick()->getJoyButton(5);
        target->setToggle(true);
        require(ActionLibrary::apply(target, action.assignments, false, &error) && target->getToggleState(), "assign action without changing toggle");
        const QByteArray assigned = ActionLibrary::snapshot(target);
        require(!ActionLibrary::apply(target, "<slots><broken>", false, &error) && ActionLibrary::snapshot(target) == assigned,
                "malformed action does not destroy target assignment");
        require(ActionLibrary::apply(target, action.assignments, true, &error) && target->getAssignedSlots()->size() == 12,
                "append action retains existing steps");
        require(library.list().first().assignments == assigned, "target edits do not change library action");
        target->clearSlotsEventReset();
        settings.setValue("ActionLibrary/Directory", directory);
        ActionLibraryDialog studio(&settings, target);
        studio.useExample(); studio.saveAction();
        pump();
        auto *table = studio.findChild<QTableWidget *>("actionSteps");
        require(table && table->rowCount() == 6, "workbench shows independent action steps");
        studio.assignAction(false);
        require(target->getAssignedSlots()->size() == 6, "workbench assigns to chosen button");
        studio.draftButton()->clearSlotsEventReset(); pump();
        studio.undoDraft(); pump();
        require(studio.draftButton()->getAssignedSlots()->size() == 6, "workbench undo restores steps");
        studio.redoDraft(); pump();
        require(studio.draftButton()->getAssignedSlots()->isEmpty() && target->getAssignedSlots()->size() == 6,
                "workbench redo edits only draft, not assigned target");
        studio.undoDraft(); pump();
        const int beforePreview = factory.recorder()->presses;
        studio.previewAction(false); pump(1100);
        require(factory.recorder()->presses == beforePreview, "preview sends no keyboard output");
        studio.previewAction(true); pump(50); studio.stopTrial(); pump(50);
        require(factory.recorder()->presses == beforePreview, "cancel countdown prevents trial output");
        studio.previewAction(true); pump(3140);
        require(factory.recorder()->presses > beforePreview, "recorded real trial starts after countdown");
        studio.stopTrial(); pump();
        require(factory.recorder()->presses == factory.recorder()->releases, "stopping trial releases held keys");
        studio.findChild<QLineEdit *>("actionSearch")->setText("does-not-exist");
        require(studio.findChild<QListWidget *>("savedActions")->count() == 0, "search filters action library");
        studio.findChild<QLineEdit *>("actionSearch")->clear();
        studio.findChild<QCheckBox *>("actionFavorite")->setChecked(true); studio.saveAction();
        studio.findChild<QCheckBox *>("favoriteActionsOnly")->setChecked(true);
        require(studio.findChild<QListWidget *>("savedActions")->count() > 0, "favorites filter finds saved favorite");
        const QString artifacts = qEnvironmentVariable("ANTIMICROX_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) { studio.show(); pump(); studio.grab().save(QDir(artifacts).filePath("action-library.png")); }
        target->clearSlotsEventReset();
    }
    std::puts("PASS: offline controls, usability, independent action library, assignment isolation and preview");
    return 0;
}
