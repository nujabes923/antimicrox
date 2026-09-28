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
#include "joystick.h"
#include "logger.h"
#include "xmlconfigwriter.h"
#include "xml/inputdevicexml.h"
#include "mainwindow.h"
#include "commandlineutility.h"
#include "actionlibrary.h"
#include "actionlibrarydialog.h"
#include "potiontimerdialog.h"
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QRadioButton>
#include <QSpinBox>
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
#include <QTabWidget>
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

class EmptyInterfaceController : public GameController
{
public:
    explicit EmptyInterfaceController(AntiMicroSettings *settings)
        : GameController(nullptr, 0, settings, 0, nullptr) {}
    int getNumberRawButtons() override { return 0; }
    int getNumberRawAxes() override { return 0; }
    int getNumberRawHats() override { return 0; }
    bool hasSensor(JoySensorType) override { return false; }
    QString getUniqueIDString() const override { return "usable-interface"; }
};

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
    if (app.arguments().contains("--native-buff")) {
        PotionTimerDialog potion(&settings);
        potion.findChild<QKeySequenceEdit *>("potionKey0")->setKeySequence(QKeySequence("F6"));
        const HWND originalForeground = GetForegroundWindow();
        potion.showPopup(); pump();
        auto *popup = potion.findChild<QDialog *>("autoBuffPopup");
        auto *toggle = popup->findChild<QPushButton *>("autoBuffPopupToggle");
        HWND handle = reinterpret_cast<HWND>(popup->winId());
        require((GetWindowLongPtr(handle, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0,
                "native buff popup has Windows no-activate style");
        require(GetForegroundWindow() == originalForeground, "showing buff popup preserves foreground window");
        require(SendMessage(handle, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(handle),
                            MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE,
                "native mouse activation preserves focus without discarding click");
        auto click = [&]() {
            const QPoint point = toggle->mapTo(popup, toggle->rect().center());
            PostMessage(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x(), point.y()));
            PostMessage(handle, WM_LBUTTONUP, 0, MAKELPARAM(point.x(), point.y()));
            pump(50);
        };
        click();
        require(potion.isRunning(), "native popup click starts scheduler");
        require(GetForegroundWindow() == originalForeground, "start click preserves foreground window");
        pump(QApplication::doubleClickInterval() + 50);
        click();
        require(!potion.isRunning(), "native popup click stops scheduler");
        require(GetForegroundWindow() == originalForeground, "stop click preserves foreground window");
        require(factory.recorder()->presses == 0, "native focus test sends no game input");
        std::puts("PASS: native auto buff non-activating window and clickable controls");
        return 0;
    }
    GameController controller(nullptr, 0, &settings, 0, nullptr);
    controller.setOfflineUniqueID("regression-offline");
    require(controller.getController() == nullptr, "no physical controller required");
    JoyButton *first = controller.getActiveSetJoystick()->getJoyButton(0);
    {
        EmptyInterfaceController empty(&settings);
        GameController usable(nullptr, 2, &settings, 0, nullptr);
        usable.setOfflineUniqueID("usable-interface");
        QMap<SDL_JoystickID, InputDevice *> devices;
        devices.insert(0, &empty);
        devices.insert(2, &usable);
        CommandLineUtility commandLine;
        MainWindow window(&devices, &commandLine, &settings, false);
        window.makeJoystickTabs();
        auto *tabs = window.findChild<QTabWidget *>("tabWidget");
        require(tabs && tabs->count() == 2, "empty interfaces retain stable tab indices");
        require(!tabs->isTabVisible(0) && tabs->isTabVisible(1), "only usable input interface is visible");
        require(tabs->currentIndex() == 1, "usable tab selected instead of hidden first interface");
        window.fillButtonsMap(&devices);
        require(!tabs->isTabVisible(0) && tabs->isTabVisible(1) && tabs->currentIndex() == 1,
                "refresh preserves interface visibility and selection");
        window.addJoyTab(&empty);
        require(!tabs->isTabVisible(2) && tabs->currentIndex() == 1,
                "hotplug auxiliary interface stays hidden without stealing selection");
        settings.setValue("Controllers/Controllerusable-interfaceLastSelected", "preserved-profile.amgp");
        settings.setValue("Controllers/Controllerusable-interfaceConfigFile1", "preserved-profile.amgp");
        auto *emptyTab = qobject_cast<JoyTabWidget *>(tabs->widget(0));
        emptyTab->saveDeviceSettings(true);
        require(settings.value("Controllers/Controllerusable-interfaceLastSelected").toString() ==
                    "preserved-profile.amgp" &&
                settings.value("Controllers/Controllerusable-interfaceConfigFile1").toString() ==
                    "preserved-profile.amgp",
                "empty interface cannot overwrite shared controller profile settings");
    }
    {
        AntiMicroSettings rawSettings(temporary.filePath("dinput.ini"), QSettings::IniFormat);
        const QString profile = temporary.filePath("dinput.joystick.amgp");
        QFile fixture(profile);
        require(fixture.open(QIODevice::WriteOnly), "create DInput offline fixture");
        fixture.write("<joystick configversion=\"19\"><sets><set index=\"1\">"
                      "<button index=\"40\"><slots><slot><code>0x41</code><mode>keyboard</mode></slot></slots></button>"
                      "<button index=\"20\"><slots><slot><code>0x42</code><mode>keyboard</mode></slot></slots></button>"
                      "</set></sets></joystick>");
        fixture.close();
        rawSettings.setValue("OfflineControllerID", "dinput-regression");
        rawSettings.setValue("Controllers/Controllerdinput-regressionLastSelected", profile);
        rawSettings.setValue("Controllers/Controllerdinput-regressionConfigFile1", profile);
        const QString rawGroup = "AlternatingSuites/dinput-regression/Set1/";
        rawSettings.setValue(rawGroup + "FirstButton", "button/39");
        rawSettings.setValue(rawGroup + "SecondButton", "button/19");
        for (const QString &key : {"FirstMinimum", "FirstMaximum", "SecondMinimum", "SecondMaximum"})
            rawSettings.setValue(rawGroup + key, 100);
        QMap<SDL_JoystickID, InputDevice *> devices;
        CommandLineUtility commandLine;
        MainWindow window(&devices, &commandLine, &rawSettings, true);
        pump();
        auto *tabs = window.findChild<QTabWidget *>("tabWidget");
        auto *tab = qobject_cast<JoyTabWidget *>(tabs->widget(0));
        require(tab && tab->getJoystick()->getXmlName() == "joystick", "DInput startup retains raw profile schema");
        require(tab->getJoystick()->getUniqueIDString() == "dinput-regression", "DInput retains alternate settings identity");
        auto *rawFirst = tab->getJoystick()->getActiveSetJoystick()->getJoyButton(39);
        auto *rawSecond = tab->getJoystick()->getActiveSetJoystick()->getJoyButton(19);
        require(rawFirst && rawFirst->getAssignedSlots()->size() == 1, "DInput restores buttons beyond standard gamepad range");
        auto *start = tab->findChild<QPushButton *>("alternatingSuitesStartButton");
        const int presses = factory.recorder()->presses;
        start->click();
        pump(50);
        require(start->isChecked() && rawFirst->getButtonState() && factory.recorder()->presses > presses,
                "DInput alternate produces keys without connected hardware");
        pump(100);
        require(!rawFirst->getButtonState() && rawSecond->getButtonState(), "DInput offline alternate rotates groups");
        start->click();
        pump();
        require(!rawFirst->getButtonState() && !rawSecond->getButtonState(), "DInput offline stop releases keys");
        auto *connected = new Joystick(nullptr, 0, &rawSettings, nullptr, 40, 8, 4);
        connected->setOfflineUniqueID("dinput-regression");
        window.addJoyTab(connected);
        pump();
        window.removeJoyTab(connected->getSDLJoystickID());
        pump();
        tab = qobject_cast<JoyTabWidget *>(tabs->widget(0));
        require(tab && tab->getJoystick()->getXmlName() == "joystick", "DInput disconnect recreates raw offline device");
        rawFirst = tab->getJoystick()->getActiveSetJoystick()->getJoyButton(39);
        start = tab->findChild<QPushButton *>("alternatingSuitesStartButton");
        start->click();
        pump(50);
        require(start->isChecked() && rawFirst->getButtonState(), "DInput alternate works after device removal");
        start->click();
        pump();
    }
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
        HANDLE recorderStop = CreateEventW(nullptr, FALSE, FALSE, L"Local\\AntiMicroX.ActionRecorder.Stop");
        require(recorderStop != nullptr, "create recorder emergency stop event");
        PostMessage(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, 0x4a51, 0);
        pump(100);
        require(WaitForSingleObject(recorderStop, 0) == WAIT_OBJECT_0, "emergency stop signals recorder companion");
        CloseHandle(recorderStop);
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
    {
        AdvanceButtonDialog editor(first);
        editor.clearAllSlots();
        auto *list = editor.findChild<QListWidget *>("slotListWidget");
        auto *type = editor.findChild<QComboBox *>("slotTypeComboBox");
        list->setCurrentRow(0);
        type->setCurrentIndex(2); // Delay, through the same Insert path as the UI.
        editor.findChild<QComboBox *>("actionTenthsComboBox")->setCurrentIndex(5);
        editor.findChild<QComboBox *>("actionHundredthsComboBox")->setCurrentIndex(2);
        require(QMetaObject::invokeMethod(&editor, "insertSlot"), "insert Delay through editor");
        require(first->getAssignedSlots()->size() == 1 &&
                first->getAssignedSlots()->first()->getSlotMode() == JoyButtonSlot::JoyDelay &&
                first->getAssignedSlots()->first()->getSlotCode() == 520,
                "new 0.52-second Delay exists in underlying assignments");
        editor.placeNewSlot(new JoyButtonSlot(0x41, JoyButtonSlot::JoyKeyboard));
        list->setCurrentRow(0);
        type->setCurrentIndex(2);
        editor.findChild<QComboBox *>("actionTenthsComboBox")->setCurrentIndex(5);
        editor.findChild<QComboBox *>("actionHundredthsComboBox")->setCurrentIndex(2);
        editor.findChild<QCheckBox *>("randomDelayCheckBox")->setChecked(true);
        require(QMetaObject::invokeMethod(&editor, "insertSlot"), "replace keyboard slot with Delay");
        require(first->getAssignedSlots()->first()->getSlotMode() == JoyButtonSlot::JoyDelay &&
                first->getAssignedSlots()->first()->isUsingRandomDelay(),
                "replacement Delay preserves random settings");
        require(first->updateAssignedDelaySlot(0, 520, true, 150, 250), "inserted Delay accepts save-time update");
        editor.clearAllSlots();
    }
    first->setAssignedSlot(0x41, JoyButtonSlot::JoyKeyboard);
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
        const QString recorderFixture = qEnvironmentVariable("ANTIMICROX_RECORDER_TEST_FIXTURE");
        if (!recorderFixture.isEmpty())
        {
            auto *button = controller.getActiveSetJoystick()->getJoyButton(0);
            LibraryAction recorded; QString problem;
            require(ActionLibrary::read(recorderFixture, recorded, &problem), "read embedded recorder action");
            require(ActionLibrary::apply(button, recorded.assignments, false, &problem), "assign embedded recording to controller");
            require(button->getAssignedSlots()->size() == 1 &&
                    button->getAssignedSlots()->first()->getSlotMode() == JoyButtonSlot::JoyExecute &&
                    button->getAssignedSlots()->first()->getExtraData().toString().startsWith("--play "),
                    "recorded action preserves executable and embedded playback data");
            button->clearSlotsEventReset();
        }
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
        require(studio.findChild<QPushButton *>("quickRecordAction") != nullptr, "workbench exposes quick recording entry");
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
    {
        PotionTimerDialog potion(&settings);
        qint64 now = 0;
        PotionTimerDialog::Target foreground{42, 100};
        potion.foreground = [&]() { return foreground; };
        potion.elapsed = [&]() { return now; };
        auto advance = [&](qint64 time) { now = time; pump(35); };
        auto *attack = potion.findChild<QKeySequenceEdit *>("potionKey0");
        auto *blood = potion.findChild<QKeySequenceEdit *>("potionKey1");
        auto *chooseBlood = potion.findChild<QRadioButton *>("potionEnabled1");
        require(potion.findChild<QSpinBox *>("potionInterval0")->value() == 175 &&
                potion.findChild<QSpinBox *>("potionInterval1")->value() == 295,
                "potion presets use researched durations minus five seconds");
        potion.start(); require(!potion.isRunning(), "empty potion hotkey cannot start");
        attack->setKeySequence(QKeySequence("Ctrl+A"));
        potion.start(); require(!potion.isRunning(), "potion rejects compound shortcuts");
        for (int modifier : {int(Qt::Key_Shift), int(Qt::Key_Control), int(Qt::Key_Alt)}) {
            QKeyEvent down(QEvent::KeyPress, modifier, Qt::ShiftModifier);
            QKeyEvent up(QEvent::KeyRelease, modifier, Qt::NoModifier);
            QApplication::sendEvent(attack->findChild<QLineEdit *>(), &down);
            QApplication::sendEvent(attack->findChild<QLineEdit *>(), &up);
            require(attack->keySequence() == QKeySequence(modifier), "physical modifier captured as standalone potion key");
            require(QKeySequence::fromString(settings.value("PotionTimer/0/Key").toString(), QKeySequence::PortableText)
                        == QKeySequence(modifier), "standalone modifier survives saved settings round trip");
            now = 0; potion.start(); require(potion.isRunning(), "standalone modifier accepted by potion scheduler");
            advance(3000);
            require(factory.recorder()->keyEvents.last().down && factory.recorder()->keyEvents.last().code ==
                    AntKeyMapper::getInstance()->returnVirtualKey(modifier), "potion sends correct native modifier key");
            potion.stop(); require(factory.recorder()->presses == factory.recorder()->releases,
                                   "stopping potion releases standalone modifier");
        }
        now = 0;
        attack->setKeySequence(QKeySequence("F6")); blood->setKeySequence(QKeySequence("F7"));
        int presses = factory.recorder()->presses;
        potion.start(); advance(2999);
        require(factory.recorder()->presses == presses, "potion countdown sends nothing early");
        potion.stop(); advance(4000);
        require(factory.recorder()->presses == presses, "cancelled countdown stays stopped");
        now = 0; potion.start(); advance(3000);
        require(factory.recorder()->presses == ++presses, "potion uses selected key after countdown");
        advance(3100);
        require(factory.recorder()->presses == factory.recorder()->releases, "potion releases after 100 ms");
        advance(177999); require(factory.recorder()->presses == presses, "attack potion waits full interval");
        advance(178000); require(factory.recorder()->presses == ++presses, "attack potion repeats at 175 seconds");
        foreground = {43, 101}; advance(178010);
        require(factory.recorder()->presses == factory.recorder()->releases, "focus loss releases held potion key");
        advance(1000000); require(factory.recorder()->presses == presses, "no potion output to other windows");
        foreground = {42, 100}; advance(1000020);
        require(factory.recorder()->presses == ++presses, "focus return sends one overdue potion");
        advance(1000400); require(factory.recorder()->presses == presses, "missed intervals are not replayed");
        chooseBlood->click(); require(!potion.isRunning(), "changing potion stops current schedule");
        now = 0; potion.start(); advance(3000); ++presses; advance(3100);
        advance(297999); require(factory.recorder()->presses == presses, "dragon blood waits full interval");
        advance(298000); require(factory.recorder()->presses == ++presses, "dragon blood repeats at 295 seconds");
        blood->setKeySequence(QKeySequence("F5"));
        require(!potion.isRunning() && factory.recorder()->presses == factory.recorder()->releases,
                "editing running hotkey releases old key and stops");
        require(settings.value("PotionTimer/Selected").toInt() == 1 &&
                settings.value("PotionTimer/1/Key").toString() == "F5", "potion choice and hotkey persist");
        potion.findChild<QCheckBox *>("potionImmediate")->setChecked(false);
        now = 0; potion.start(); advance(3000);
        require(factory.recorder()->presses == presses, "optional delayed first dose sends nothing on start");
        advance(298000); require(factory.recorder()->presses == ++presses, "delayed first dose respects preset");
        potion.close();
        require(!potion.isRunning() && factory.recorder()->presses == factory.recorder()->releases,
                "closing potion window stops and releases keys");
        now = 0; foreground = {}; potion.start(); advance(3000);
        require(!potion.isRunning(), "invalid target cancels potion start");
        const QString artifacts = qEnvironmentVariable("ANTIMICROX_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) { potion.show(); pump(); potion.grab().save(QDir(artifacts).filePath("potion-timer.png")); }
    }
    {
        AntiMicroSettings integrationSettings(temporary.filePath("potion-integration.ini"), QSettings::IniFormat);
        QMap<SDL_JoystickID, InputDevice *> devices;
        CommandLineUtility commandLine;
        MainWindow window(&devices, &commandLine, &integrationSettings, false);
        auto *action = window.findChild<QAction *>("openPotionTimerAction");
        require(action != nullptr, "main menu exposes potion timer");
        action->trigger(); action->trigger();
        require(window.findChildren<PotionTimerDialog *>().size() == 1, "potion menu reuses single scheduler");
        auto *potion = window.findChild<PotionTimerDialog *>();
        potion->findChild<QKeySequenceEdit *>("potionKey0")->setKeySequence(QKeySequence("F6"));
        qint64 now = 0;
        potion->elapsed = [&]() { return now; };
        potion->foreground = []() { return PotionTimerDialog::Target{42, 100}; };
        potion->start(); now = 3000; pump(35);
        require(factory.recorder()->presses > factory.recorder()->releases, "integration potion holds test key");
        potion->showPopup();
        auto *buffPopup = potion->findChild<QDialog *>("autoBuffPopup");
        auto *buffToggle = buffPopup->findChild<QPushButton *>("autoBuffPopupToggle");
        require(buffPopup->isVisible() && !potion->isVisible() && buffPopup->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                "buff popup is on top and replaces settings without stopping scheduler");
        require(buffPopup->findChild<QLabel *>("autoBuffPopupStatus")->text().contains(QStringLiteral("运行中")) &&
                buffPopup->findChild<QLabel *>("autoBuffPopupDetails")->text().contains("02:55"),
                "buff popup displays running state and preset countdown");
        buffToggle->click();
        require(!potion->isRunning() && factory.recorder()->presses == factory.recorder()->releases,
                "popup switch stops scheduler and releases held key");
        now = 0; buffToggle->click();
        require(potion->isRunning() && buffPopup->findChild<QLabel *>("autoBuffPopupStatus")->text().contains(QStringLiteral("准备开始")),
                "popup switch starts countdown");
        now = 3000; pump(35);
        potion->foreground = []() { return PotionTimerDialog::Target{43, 101}; }; pump(35);
        require(buffPopup->findChild<QLabel *>("autoBuffPopupStatus")->text().contains(QStringLiteral("已暂停")) &&
                factory.recorder()->presses == factory.recorder()->releases, "popup shows focus pause and key release");
        const QString popupArtifacts = qEnvironmentVariable("ANTIMICROX_TEST_ARTIFACT_DIR");
        if (!popupArtifacts.isEmpty()) buffPopup->grab().save(QDir(popupArtifacts).filePath("auto-buff-popup.png"));
        buffPopup->close();
        require(!potion->isRunning(), "closing buff popup stops scheduler");
        potion->showPopup();
        require(potion->findChildren<QDialog *>("autoBuffPopup").size() == 1, "buff popup is reused");
        potion->foreground = []() { return PotionTimerDialog::Target{42, 100}; };
        now = 0; buffToggle->click(); now = 3000; pump(35);
        window.emergencyStopAll();
        require(!potion->isRunning() && factory.recorder()->presses == factory.recorder()->releases,
                "global emergency stop stops potion scheduler and releases key");
        require(buffPopup->findChild<QLabel *>("autoBuffPopupStatus")->text() == QStringLiteral("已停止"),
                "emergency stop updates buff popup immediately");
        potion->close(); pump(); action->trigger();
        potion = window.findChild<PotionTimerDialog *>();
        require(potion && !potion->isRunning() &&
                potion->findChild<QKeySequenceEdit *>("potionKey0")->keySequence() == QKeySequence("F6"),
                "reopening restores hotkey without automatically running");
    }
    std::puts("PASS: offline controls, usability, independent action library, assignment isolation, preview and potion timers");
    return 0;
}
