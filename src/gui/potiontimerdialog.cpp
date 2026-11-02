#include "potiontimerdialog.h"
#include "antimicrosettings.h"
#include "antkeymapper.h"
#include "event.h"
#include "eventhandlerfactory.h"
#include "eventhandlers/baseeventhandler.h"
#include "joybuttonslot.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <array>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
class BuffPopup : public QDialog
{
public:
    explicit BuffPopup(QWidget *parent)
        : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
    }
    std::function<void()> onClose;
protected:
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override
#else
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override
#endif
    {
        const auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            // Keep the game foreground, but deliver the mouse click to controls.
            *result = MA_NOACTIVATE;
            return true;
        }
        return QDialog::nativeEvent(eventType, message, result);
    }
#endif
    void closeEvent(QCloseEvent *event) override
    {
        if (onClose) onClose();
        event->accept();
    }
    void reject() override { close(); }
};
// QKeySequenceEdit normally waits for a non-modifier key. This editor records
// one physical key, including Shift/Ctrl/Alt, without shortcut modifiers.
class PotionKeyEdit : public QKeySequenceEdit
{
public:
    explicit PotionKeyEdit(QWidget *parent) : QKeySequenceEdit(parent)
    {
        for (auto *field : findChildren<QLineEdit *>()) field->installEventFilter(this);
    }
protected:
    bool event(QEvent *event) override
    {
        if (capture(event)) return true;
        return QKeySequenceEdit::event(event);
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (capture(event)) return true;
        return QKeySequenceEdit::eventFilter(object, event);
    }
private:
    bool capture(QEvent *event)
    {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyRelease) {
            event->accept(); return true;
        }
        if (event->type() != QEvent::KeyPress) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (!key->isAutoRepeat() && key->key() != Qt::Key_unknown && key->key() != 0)
            setKeySequence(QKeySequence(key->key()));
        event->accept(); return true;
    }
};
}

PotionTimerDialog::PotionTimerDialog(AntiMicroSettings *settings, QWidget *parent)
    : QDialog(parent), settings(settings)
{
    setObjectName("potionTimerDialog");
    setWindowTitle(QStringLiteral("定时用药"));
    auto *layout = new QGridLayout(this);
    auto *help = new QLabel(QStringLiteral("选择药水或添加多个自定义 buff 槽位，设置游戏内单键和补用周期；可同时运行一种预设和多个自定义 buff。\n"
        "默认补用间隔：攻击药水 175 秒，龙血 295 秒（基础时长提前 5 秒）。\n"
        "可按增益后的实际持续时间自定义间隔，建议提前 5 秒补用。\n"
        "点击开始后，3 秒内切换到游戏；修改药水、按键或间隔会停止计时，需重新开始。\n"
        "切到其他窗口时暂停发送，返回游戏后继续；关闭本窗口会停止。\n"
        "Ctrl+Alt+F10 可紧急停止所有自动操作。"), this);
    help->setWordWrap(true);
    layout->addWidget(help, 0, 0, 1, 5);
    layout->addWidget(new QLabel(QStringLiteral("启用"), this), 1, 0);
    layout->addWidget(new QLabel(QStringLiteral("buff 名称"), this), 1, 1);
    layout->addWidget(new QLabel(QStringLiteral("游戏内按键（单键）"), this), 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("补用周期"), this), 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("下次使用"), this), 1, 4);
    for (int i = 0; i < 2; ++i) {
        entries.emplace_back();
        auto &entry = entries.back();
        const QString prefix = QString("PotionTimer/%1/").arg(i);
        entry.enabled = new QCheckBox(QStringLiteral("启用"), this);
        const QString defaultName = i == 0 ? QStringLiteral("攻击药水") : QStringLiteral("龙血");
        entry.name = new QLineEdit(settings->value(prefix + "Name", defaultName).toString(), this);
        entry.name->setObjectName(QString("potionName%1").arg(i));
        entry.name->setMaxLength(40);
        entry.name->setPlaceholderText(QStringLiteral("buff 名称"));
        entry.key = new PotionKeyEdit(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        entry.key->setMaximumSequenceLength(1);
#endif
        entry.seconds = new QSpinBox(this);
        entry.seconds->setRange(1, 86400);
        entry.seconds->setSuffix(QStringLiteral(" 秒"));
        entry.remaining = new QLabel(QStringLiteral("未启动"), this);
        entry.enabled->setObjectName(QString("potionEnabled%1").arg(i));
        entry.key->setObjectName(QString("potionKey%1").arg(i));
        entry.seconds->setObjectName(QString("potionInterval%1").arg(i));
        entry.enabled->setChecked(settings->value(prefix + "Enabled", settings->value("PotionTimer/Selected", 0).toInt() == i).toBool());
        entry.key->setKeySequence(QKeySequence::fromString(settings->value(prefix + "Key").toString(), QKeySequence::PortableText));
        entry.seconds->setValue(settings->value(prefix + "Seconds", i == 0 ? 175 : 295).toInt());
        entry.seconds->setToolTip(i == 0 ? QStringLiteral("攻击药水／攻击神水：180 秒效果，提前 5 秒补用")
            : QStringLiteral("龙血／暴龙之血默认 295 秒补用，可按增益后的持续时间调整"));
        const int row = i + 2;
        layout->addWidget(entry.enabled, row, 0);
        layout->addWidget(entry.name, row, 1);
        layout->addWidget(entry.key, row, 2);
        layout->addWidget(entry.seconds, row, 3);
        layout->addWidget(entry.remaining, row, 4);
    }
    for (auto &entry : entries) {
        connect(entry.enabled, &QAbstractButton::toggled, this, [this]() { stop(); save(); });
        connect(entry.name, &QLineEdit::textChanged, this, [this]() { stop(); save(); refreshPopup(); });
        connect(entry.key, &QKeySequenceEdit::keySequenceChanged, this, [this]() { stop(); save(); });
        connect(entry.seconds, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { stop(); save(); });
    }
    customRows = new QWidget(this);
    customRows->setObjectName("customBuffRows");
    customLayout = new QGridLayout(customRows);
    customLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(customRows, 4, 0, 1, 5);
    int customCount = qBound(0, settings->value("PotionTimer/CustomCount", 1).toInt(), 20);
    for (int i = 0; i < customCount; ++i) addCustomEntry(false);
    addBuffButton = new QPushButton(QStringLiteral("＋ 添加自定义 buff 槽位"), this);
    addBuffButton->setObjectName("addCustomBuffButton");
    layout->addWidget(addBuffButton, 5, 0, 1, 5);
    connect(addBuffButton, &QPushButton::clicked, this, [this]() { addCustomEntry(); });
    immediate = new QCheckBox(QStringLiteral("开始时先使用一次"), this);
    immediate->setObjectName("potionImmediate");
    immediate->setChecked(settings->value("PotionTimer/Immediate", true).toBool());
    layout->addWidget(immediate, 6, 0, 1, 5);
    startButton = new QPushButton(QStringLiteral("开始（3 秒后）"), this);
    stopButton = new QPushButton(QStringLiteral("停止"), this);
    stopButton->setEnabled(false);
    layout->addWidget(startButton, 7, 0, 1, 2);
    layout->addWidget(stopButton, 7, 2, 1, 3);
    status = new QLabel(QStringLiteral("设置会自动保存；每次打开程序后需手动开始。"), this);
    status->setWordWrap(true);
    layout->addWidget(status, 8, 0, 1, 5);
    auto *sources = new QLabel(QStringLiteral("持续时间资料：<a href=\"https://mxdzlk.com/item/2002004/\">攻击药水</a> · "
        "<a href=\"https://mxdzlk.com/item/2012000/\">龙血</a>（基础时长供参考，间隔可自定义）"), this);
    sources->setOpenExternalLinks(true);
    layout->addWidget(sources, 9, 0, 1, 5);
    auto *popupButton = new QPushButton(QStringLiteral("小浮窗 · 状态 / 开关"), this);
    popupButton->setObjectName("showBuffPopupButton");
    layout->addWidget(popupButton, 10, 0, 1, 5);
    connect(popupButton, &QPushButton::clicked, this, &PotionTimerDialog::showPopup);
    foreground = []() {
#ifdef Q_OS_WIN
        HWND window = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (!window || !process || process == GetCurrentProcessId()) return Target{};
        return Target{reinterpret_cast<quintptr>(window), process};
#else
        return Target{};
#endif
    };
    elapsed = [this]() { return clock.elapsed(); };
    timer.setInterval(20);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &PotionTimerDialog::tick);
    connect(startButton, &QPushButton::clicked, this, &PotionTimerDialog::start);
    connect(stopButton, &QPushButton::clicked, this, &PotionTimerDialog::stop);
    connect(qApp, &QApplication::aboutToQuit, this, &PotionTimerDialog::stop);
}

PotionTimerDialog::~PotionTimerDialog() { stop(); }

void PotionTimerDialog::showPopup()
{
    if (!popup) {
        auto *panel = new BuffPopup(this);
        popup = panel;
        panel->setObjectName("autoBuffPopup");
        panel->setWindowTitle(QStringLiteral("auto buff"));
        panel->setAttribute(Qt::WA_ShowWithoutActivating);
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12, 10, 12, 10);
        popupStatus = new QLabel(panel);
        popupStatus->setObjectName("autoBuffPopupStatus");
        QFont font = popupStatus->font(); font.setBold(true); font.setPointSize(font.pointSize() + 2);
        popupStatus->setFont(font);
        popupDetails = new QLabel(panel);
        popupDetails->setObjectName("autoBuffPopupDetails");
        layout->addWidget(popupStatus); layout->addWidget(popupDetails);
        auto *buttons = new QHBoxLayout();
        popupToggle = new QPushButton(panel);
        popupToggle->setFocusPolicy(Qt::NoFocus);
        popupToggle->setAutoDefault(false);
        popupToggle->setObjectName("autoBuffPopupToggle");
        auto *configure = new QPushButton(QStringLiteral("设置"), panel);
        configure->setFocusPolicy(Qt::NoFocus);
        configure->setAutoDefault(false);
        buttons->addWidget(popupToggle); buttons->addWidget(configure);
        layout->addLayout(buttons);
        auto *hint = new QLabel(QStringLiteral("关闭浮窗即停止 · Ctrl+Alt+F10 急停"), panel);
        layout->addWidget(hint);
        connect(popupToggle, &QPushButton::clicked, this, [this]() { if (running) stop(); else start(); });
        connect(configure, &QPushButton::clicked, this, [this]() { showNormal(); raise(); activateWindow(); });
        panel->onClose = [this]() {
            settings->setValue("PotionTimer/PopupPosition", popup->pos());
            stop();
        };
        panel->adjustSize();
        if (settings->contains("PotionTimer/PopupPosition"))
            panel->move(settings->value("PotionTimer/PopupPosition").toPoint());
    }
    refreshPopup();
    // Hiding the settings window retains the scheduler; closing it still stops it.
    hide();
    popup->show(); popup->raise();
}

void PotionTimerDialog::setStatus(const QString &message)
{
    status->setText(message);
    refreshPopup();
}

void PotionTimerDialog::refreshPopup()
{
    if (!popup) return;
    QString state = QStringLiteral("已停止");
    QString countdown;
    if (running) {
        const qint64 now = elapsed();
        if (!acquired) {
            state = QStringLiteral("准备开始 · %1 秒").arg(qMax(0LL, (3000 - now + 999) / 1000));
            countdown = QStringLiteral("请切回游戏窗口");
        } else {
            const bool paused = !(foreground() == target);
            state = paused ? QStringLiteral("已暂停 · 等待返回游戏") : QStringLiteral("运行中");
            bool first = true;
            for (const auto &entry : entries) if (entry.code) {
                qint64 seconds = qMax(0LL, (entry.due - now + 999) / 1000);
                const QString line = QStringLiteral("%1：%2:%3").arg(entry.name ? entry.name->text() : entry.enabled->text())
                    .arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
                countdown += (first ? QString() : QStringLiteral("\n")) + line;
                first = false;
            }
            if (paused && !countdown.isEmpty()) countdown += QStringLiteral("\n已到时间的 buff 将在返回游戏后补用");
        }
    } else if (status->text() != QStringLiteral("已停止。")) {
        countdown = status->text();
    }
    QString selected;
    for (const auto &entry : entries) if (entry.enabled->isChecked()) {
        if (!selected.isEmpty()) selected += QStringLiteral("\n");
        selected += (entry.name ? entry.name->text().trimmed() : entry.enabled->text()) + QStringLiteral(" · ") +
            (entry.key->keySequence().isEmpty() ? QStringLiteral("未设置按键") : entry.key->keySequence().toString(QKeySequence::NativeText));
    }
    popupStatus->setText(state);
    popupDetails->setWordWrap(true);
    popupDetails->setText(selected + (countdown.isEmpty() ? QString() : "\n" + countdown));
    popupDetails->setMinimumWidth(250);
    popupToggle->setText(running ? QStringLiteral("■ 停止") : QStringLiteral("▶ 开始（3 秒）"));
}

void PotionTimerDialog::addCustomEntry(bool saveAfter)
{
    if (entries.size() >= 22) return;
    const int index = static_cast<int>(entries.size());
    const QString prefix = QString("PotionTimer/%1/").arg(index);
    Entry entry{};
    entry.enabled = new QCheckBox(QStringLiteral("启用"), customRows);
    entry.enabled->setObjectName(QString("potionEnabled%1").arg(index));
    entry.enabled->setChecked(settings->value(prefix + "Enabled", false).toBool());
    entry.name = new QLineEdit(settings->value(prefix + "Name", QStringLiteral("自定义 buff %1").arg(index - 1)).toString(), customRows);
    entry.name->setObjectName(QString("potionName%1").arg(index));
    entry.name->setMaxLength(40);
    entry.name->setPlaceholderText(QStringLiteral("buff 名称"));
    entry.key = new PotionKeyEdit(customRows);
    entry.key->setObjectName(QString("potionKey%1").arg(index));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    entry.key->setMaximumSequenceLength(1);
#endif
    entry.key->setKeySequence(QKeySequence::fromString(settings->value(prefix + "Key").toString(), QKeySequence::PortableText));
    entry.seconds = new QSpinBox(customRows);
    entry.seconds->setObjectName(QString("potionInterval%1").arg(index));
    entry.seconds->setRange(1, 86400);
    entry.seconds->setSuffix(QStringLiteral(" 秒"));
    entry.seconds->setValue(settings->value(prefix + "Seconds", 60).toInt());
    entry.remaining = new QLabel(QStringLiteral("未启动"), customRows);
    entry.remove = new QPushButton(QStringLiteral("删除此槽位"), customRows);
    entry.remove->setObjectName(QString("removeCustomBuff%1").arg(index));
    entry.row = customRows;
    for (int i = 2; i < static_cast<int>(entries.size()); ++i) entries[i].remove->hide();
    entries.push_back(entry);
    const int row = index - 2;
    customLayout->addWidget(entry.enabled, row, 0);
    customLayout->addWidget(entry.name, row, 1);
    customLayout->addWidget(entry.key, row, 2);
    customLayout->addWidget(entry.seconds, row, 3);
    customLayout->addWidget(entry.remaining, row, 4);
    customLayout->addWidget(entry.remove, row, 5);
    connect(entry.enabled, &QAbstractButton::toggled, this, [this]() { stop(); save(); });
    connect(entry.name, &QLineEdit::textChanged, this, [this]() { stop(); save(); refreshPopup(); });
    connect(entry.key, &QKeySequenceEdit::keySequenceChanged, this, [this]() { stop(); save(); });
    connect(entry.seconds, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { stop(); save(); });
    connect(entry.remove, &QPushButton::clicked, this, &PotionTimerDialog::removeLastCustomEntry);
    if (addBuffButton) addBuffButton->setEnabled(entries.size() < 22);
    if (saveAfter) save();
    adjustSize();
}

void PotionTimerDialog::removeLastCustomEntry()
{
    if (entries.size() <= 2 || running) return;
    const int index = static_cast<int>(entries.size()) - 1;
    auto entry = entries.back();
    const std::array<QWidget *, 6> widgets = {entry.enabled, entry.name, entry.key, entry.seconds, entry.remaining, entry.remove};
    for (QWidget *widget : widgets) {
        customLayout->removeWidget(widget);
        delete widget;
    }
    entries.pop_back();
    if (entries.size() > 2) entries.back().remove->show();
    settings->remove(QString("PotionTimer/%1").arg(index));
    if (addBuffButton) addBuffButton->setEnabled(true);
    save();
    adjustSize();
}

void PotionTimerDialog::save()
{
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &entry = entries[i];
        const QString prefix = QString("PotionTimer/%1/").arg(i);
        settings->setValue(prefix + "Enabled", entry.enabled->isChecked());
        if (i < 2 && entry.enabled->isChecked()) settings->setValue("PotionTimer/Selected", i);
        settings->setValue(prefix + "Key", entry.key->keySequence().toString(QKeySequence::PortableText));
        settings->setValue(prefix + "Seconds", entry.seconds->value());
        settings->setValue(prefix + "Name", entry.name->text());
    }
    settings->setValue("PotionTimer/CustomCount", static_cast<int>(entries.size()) - 2);
    settings->setValue("PotionTimer/Immediate", immediate->isChecked());
    settings->sync();
}

void PotionTimerDialog::setEditing(bool enabled)
{
    immediate->setEnabled(enabled);
    startButton->setEnabled(enabled); stopButton->setEnabled(!enabled);
    if (addBuffButton) addBuffButton->setEnabled(enabled && entries.size() < 22);
    for (int i = 2; i < static_cast<int>(entries.size()); ++i) entries[i].remove->setEnabled(enabled);
}

void PotionTimerDialog::start()
{
    if (running) return;
    bool any = false;
    for (auto &entry : entries) {
        entry.code = 0;
        if (!entry.enabled->isChecked()) continue;
        if (entry.name->text().trimmed().isEmpty()) {
            setStatus(QStringLiteral("请填写自定义 buff 名称。")); return;
        }
        const auto sequence = entry.key->keySequence();
        if (sequence.count() != 1) {
            setStatus(QStringLiteral("请为启用的药水设置一个单独按键。")); return;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        entry.alias = sequence[0].toCombined();
#else
        entry.alias = sequence[0];
#endif
        if ((entry.alias & Qt::KeyboardModifierMask) ||
            !(entry.code = AntKeyMapper::getInstance()->returnVirtualKey(entry.alias))) {
            setStatus(QStringLiteral("仅支持单独按键，例如 Shift、Ctrl、A、1 或 F1，请勿使用组合键。")); return;
        }
        any = true;
    }
    if (!any) { setStatus(QStringLiteral("请至少启用一种药水，并设置游戏内对应按键。")); return; }
    save(); target = {}; acquired = false; running = true; held = -1; nextPressAt = 0;
    clock.start(); setEditing(false); timer.start(); tick();
}

void PotionTimerDialog::releaseKey()
{
    if (held < 0) return;
    const auto &entry = entries[held];
    JoyButtonSlot slot(entry.code, entry.alias, JoyButtonSlot::JoyKeyboard);
    sendevent(&slot, false); held = -1;
}

void PotionTimerDialog::tick()
{
    if (!running) return;
    qint64 now = elapsed();
    if (!acquired) {
        if (now < 3000) {
            setStatus(QStringLiteral("%1 秒后开始，请切换到游戏窗口…").arg((3000 - now + 999) / 1000)); return;
        }
        target = foreground();
        if (!target.window || !target.process) {
            stop(); setStatus(QStringLiteral("未选中游戏窗口，已取消。请重新开始并在倒计时内切换到游戏。")); return;
        }
        acquired = true;
        for (auto &entry : entries) entry.due = now + (immediate->isChecked() ? 0 : entry.seconds->value() * 1000LL);
    }
    if (!(foreground() == target)) {
        releaseKey();
        setStatus(QStringLiteral("已暂停发送：返回开始时选中的游戏窗口后继续。")); return;
    }
    if (held >= 0 && now >= releaseAt) { releaseKey(); nextPressAt = now + 250; }
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        auto &entry = entries[i];
        if (!entry.code) { entry.remaining->setText(QStringLiteral("未启用")); continue; }
        entry.remaining->setText(QStringLiteral("%1 秒").arg(qMax(0LL, (entry.due - now + 999) / 1000)));
        if (held < 0 && now >= nextPressAt && now >= entry.due) {
            JoyButtonSlot slot(entry.code, entry.alias, JoyButtonSlot::JoyKeyboard);
            sendevent(&slot, true);
            held = i; releaseAt = now + 100;
            // Skip missed repetitions after switching windows or waking from sleep.
            entry.due = now + entry.seconds->value() * 1000LL;
        }
    }
    setStatus(QStringLiteral("运行中 · 仅向选中的游戏窗口发送按键 · Ctrl+Alt+F10 紧急停止"));
}

void PotionTimerDialog::stop()
{
    timer.stop(); releaseKey(); running = false; acquired = false;
    setEditing(true);
    for (auto &entry : entries) entry.remaining->setText(QStringLiteral("已停止"));
    setStatus(QStringLiteral("已停止。"));
}

void PotionTimerDialog::closeEvent(QCloseEvent *event) { stop(); save(); if (popup) popup->hide(); event->accept(); }
void PotionTimerDialog::reject() { close(); }


AutoShoutDialog::AutoShoutDialog(AntiMicroSettings *settings, QWidget *parent)
    : QDialog(parent), settings(settings)
{
    setObjectName("autoShoutDialog");
    setWindowTitle(QStringLiteral("自动喊话 / 牧师开花"));
    auto *layout = new QGridLayout(this);
    auto *help = new QLabel(QStringLiteral("首次手动按开花键对时，之后由宠物每 120 秒续花；软件只提前喊话，绝不自动按开花键。\n"
        "开始后切回游戏手动开花；通知固定跟随首次开花时间。临时手动补花会重新对时并喊话。聊天框需处于关闭状态；使用当前聊天频道，可在文字前加频道命令。\n"
        "切出游戏时暂停；喊话中途切出会停止，返回后请手动关闭聊天框再重新开始。Ctrl+Alt+F10 急停。"), this);
    help->setWordWrap(true); layout->addWidget(help, 0, 0, 1, 2);
    message = new QLineEdit(settings->value("AutoShout/Message", QStringLiteral("5秒后开花，请靠近我！")).toString(), this);
    message->setObjectName("shoutMessage"); message->setMaxLength(200); message->setMinimumWidth(350);
    layout->addWidget(new QLabel(QStringLiteral("喊话内容"), this), 1, 0); layout->addWidget(message, 1, 1);
    interval = new QSpinBox(this); interval->setObjectName("shoutInterval"); interval->setRange(1, 86400);
    settings->setValue("AutoShout/TimingDefaultsVersion", 2);
    interval->setSuffix(QStringLiteral(" 秒")); interval->setValue(settings->value("AutoShout/Interval", 120).toInt());
    interval->setToolTip(QStringLiteral("自动开花固定 120 秒；喊话输入提前安排，不会延长开花周期。仅喊话模式可自定义间隔。"));
    layout->addWidget(new QLabel(QStringLiteral("每轮间隔"), this), 2, 0); layout->addWidget(interval, 2, 1);
    lead = new QSpinBox(this); lead->setObjectName("shoutLead"); lead->setRange(1, 3600);
    lead->setSuffix(QStringLiteral(" 秒")); lead->setValue(settings->value("AutoShout/Lead", 5).toInt());
    layout->addWidget(new QLabel(QStringLiteral("宠物续花前多久喊话"), this), 3, 0); layout->addWidget(lead, 3, 1);
    chatKey = new PotionKeyEdit(this); chatKey->setObjectName("shoutChatKey");
    chatKey->setKeySequence(QKeySequence::fromString(settings->value("AutoShout/ChatKey", "Return").toString(), QKeySequence::PortableText));
    layout->addWidget(new QLabel(QStringLiteral("打开 / 发送聊天键"), this), 4, 0); layout->addWidget(chatKey, 4, 1);
    skillEnabled = new QCheckBox(QStringLiteral("宠物续花通知（首次手动开花，后续只喊话）"), this);
    skillEnabled->setObjectName("shoutSkillEnabled"); skillEnabled->setChecked(settings->value("AutoShout/SkillEnabled", true).toBool());
    layout->addWidget(skillEnabled, 5, 0, 1, 2);
    skillKey = new PotionKeyEdit(this); skillKey->setObjectName("shoutSkillKey");
    skillKey->setKeySequence(QKeySequence::fromString(settings->value("AutoShout/SkillKey").toString(), QKeySequence::PortableText));
    layout->addWidget(new QLabel(QStringLiteral("首次 / 临时手动开花键（仅监听）"), this), 6, 0); layout->addWidget(skillKey, 6, 1);
    startButton = new QPushButton(QStringLiteral("开始（3 秒后）"), this); startButton->setObjectName("shoutStart");
    stopButton = new QPushButton(QStringLiteral("停止"), this); stopButton->setObjectName("shoutStop"); stopButton->setEnabled(false);
    layout->addWidget(startButton, 7, 0); layout->addWidget(stopButton, 7, 1);
    status = new QLabel(QStringLiteral("设置自动保存；请先在游戏内选好聊天频道。"), this);
    status->setObjectName("shoutStatus"); status->setWordWrap(true); layout->addWidget(status, 8, 0, 1, 2);
    auto *floating = new QPushButton(QStringLiteral("小浮窗 · 不抢游戏焦点"), this);
    floating->setObjectName("showShoutPopupButton"); layout->addWidget(floating, 9, 0, 1, 2);
    connect(floating, &QPushButton::clicked, this, &AutoShoutDialog::showPopup);
    manualFirst = new QCheckBox(QStringLiteral("首次手动开花后，通知跟随宠物 120 秒周期"), this);
    manualFirst->setObjectName("shoutManualFirst");
    manualFirst->setChecked(settings->value("AutoShout/ManualFirst", false).toBool());
    manualFirst->setToolTip(QStringLiteral("点开始后切回游戏，手动按一次上面的开花键；这次按键正常传给游戏，软件从此刻计时。"));
    layout->addWidget(manualFirst, 10, 0, 1, 2);
    restartHint = new QLabel(QStringLiteral("临时补花：再按一次手动开花键，重新对时并喊话；Ctrl+Alt+F9 重新等待手动开花。"), this);
    restartHint->setWordWrap(true); layout->addWidget(restartHint, 11, 0, 1, 2);
    resyncMessage = new QLineEdit(settings->value("AutoShout/ResyncMessage", QStringLiteral("已手动补花，请靠近我！后续每120秒续花。")).toString(), this);
    resyncMessage->setObjectName("shoutResyncMessage"); resyncMessage->setMaxLength(200);
    layout->addWidget(new QLabel(QStringLiteral("手动补花后喊话"), this), 12, 0); layout->addWidget(resyncMessage, 12, 1);
    auto changed = [this]() { stop(); save(); };
    connect(message, &QLineEdit::textChanged, this, changed);
    connect(resyncMessage, &QLineEdit::textChanged, this, changed);
    connect(interval, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
    connect(lead, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
    connect(chatKey, &QKeySequenceEdit::keySequenceChanged, this, changed);
    connect(skillKey, &QKeySequenceEdit::keySequenceChanged, this, changed);
    auto enforceFlowerPeriod = [this]() {
        interval->setReadOnly(skillEnabled->isChecked());
        manualFirst->setEnabled(!skillEnabled->isChecked());
        if (skillEnabled->isChecked()) { interval->setValue(120); manualFirst->setChecked(true); }
    };
    connect(skillEnabled, &QCheckBox::toggled, this, enforceFlowerPeriod);
    enforceFlowerPeriod();
    connect(skillEnabled, &QCheckBox::toggled, this, changed);
    connect(manualFirst, &QCheckBox::toggled, this, changed);
    auto updateStartLabel = [this]() {
        startButton->setText(manualFirst->isChecked() ? QStringLiteral("开始监听首次开花") : QStringLiteral("开始（3 秒后）"));
        refreshPopup();
    };
    connect(manualFirst, &QCheckBox::toggled, this, updateStartLabel); updateStartLabel();
    foreground = []() {
#ifdef Q_OS_WIN
        HWND window = GetForegroundWindow(); DWORD process = 0; GetWindowThreadProcessId(window, &process);
        if (!window || !process || process == GetCurrentProcessId()) return PotionTimerDialog::Target{};
        return PotionTimerDialog::Target{reinterpret_cast<quintptr>(window), process};
#else
        return PotionTimerDialog::Target{};
#endif
    };
    keyDown = [](int code) {
#ifdef Q_OS_WIN
        return (GetAsyncKeyState(code) & 0x8000) != 0;
#else
        Q_UNUSED(code); return false;
#endif
    };
    restartModifiersDown = []() {
#ifdef Q_OS_WIN
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
               (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
#else
        return false;
#endif
    };
    elapsed = [this]() { return clock.elapsed(); };
    timer.setInterval(20); timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this]() { tick(); refreshPopup(); });
    connect(startButton, &QPushButton::clicked, this, &AutoShoutDialog::start);
    connect(stopButton, &QPushButton::clicked, this, &AutoShoutDialog::stop);
    connect(qApp, &QApplication::aboutToQuit, this, &AutoShoutDialog::stop);
}
AutoShoutDialog::~AutoShoutDialog() { stop(); }
void AutoShoutDialog::showPopup()
{
    if (!popup) {
        auto *panel = new BuffPopup(this); popup = panel;
        panel->setObjectName("autoShoutPopup"); panel->setWindowTitle(QStringLiteral("自动喊话 / 开花"));
        auto *layout = new QVBoxLayout(panel);
        popupStatus = new QLabel(panel); popupStatus->setWordWrap(true); popupStatus->setMinimumWidth(260);
        popupStatus->setObjectName("shoutPopupStatus"); layout->addWidget(popupStatus);
        popupToggle = new QPushButton(panel); popupToggle->setObjectName("shoutPopupToggle");
        popupToggle->setFocusPolicy(Qt::NoFocus); popupToggle->setAutoDefault(false); layout->addWidget(popupToggle);
        auto *restart = new QPushButton(QStringLiteral("重新等待手动开花 · Ctrl+Alt+F9"), panel);
        restart->setObjectName("shoutRestart"); restart->setFocusPolicy(Qt::NoFocus); restart->setAutoDefault(false);
        layout->addWidget(restart); connect(restart, &QPushButton::clicked, this, &AutoShoutDialog::restartCycle);
        auto *configure = new QPushButton(QStringLiteral("设置（暂停）"), panel);
        configure->setFocusPolicy(Qt::NoFocus); configure->setAutoDefault(false); layout->addWidget(configure);
        connect(popupToggle, &QPushButton::clicked, this, [this]() { if (running) stop(); else start(); refreshPopup(); });
        connect(configure, &QPushButton::clicked, this, [this]() { stop(); showNormal(); raise(); activateWindow(); });
        panel->onClose = [this]() { settings->setValue("AutoShout/PopupPosition", popup->pos()); stop(); };
        panel->adjustSize();
        if (settings->contains("AutoShout/PopupPosition")) panel->move(settings->value("AutoShout/PopupPosition").toPoint());
    }
    hide(); refreshPopup(); popup->show(); popup->raise();
}
void AutoShoutDialog::refreshPopup()
{
    if (!popup) return;
    popupStatus->setText(status->text());
    popupToggle->setText(running ? QStringLiteral("■ 停止") : (manualFirst->isChecked() ? QStringLiteral("▶ 监听首次开花") : QStringLiteral("▶ 开始（3 秒）")));
}

void AutoShoutDialog::save()
{
    settings->setValue("AutoShout/Message", message->text());
    settings->setValue("AutoShout/ResyncMessage", resyncMessage->text());
    settings->setValue("AutoShout/Interval", interval->value()); settings->setValue("AutoShout/Lead", lead->value());
    settings->setValue("AutoShout/ChatKey", chatKey->keySequence().toString(QKeySequence::PortableText));
    settings->setValue("AutoShout/SkillKey", skillKey->keySequence().toString(QKeySequence::PortableText));
    settings->setValue("AutoShout/SkillEnabled", skillEnabled->isChecked());
    settings->setValue("AutoShout/ManualFirst", manualFirst->isChecked()); settings->sync();
}
namespace {
int shoutAlias(QKeySequenceEdit *edit)
{
    if (edit->keySequence().count() != 1) return 0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int alias = edit->keySequence()[0].toCombined();
#else
    const int alias = edit->keySequence()[0];
#endif
    return !(alias & Qt::KeyboardModifierMask) && AntKeyMapper::getInstance()->returnVirtualKey(alias) ? alias : 0;
}
}
void AutoShoutDialog::key(int alias, bool down)
{
    JoyButtonSlot slot(AntKeyMapper::getInstance()->returnVirtualKey(alias), alias, JoyButtonSlot::JoyKeyboard);
    sendevent(&slot, down); heldAlias = down ? alias : 0;
}
void AutoShoutDialog::release() { if (heldAlias) key(heldAlias, false); }
void AutoShoutDialog::start()
{
    if (running) return;
    if (message->text().trimmed().isEmpty() || !shoutAlias(chatKey) || (skillEnabled->isChecked() && !shoutAlias(skillKey))) {
        status->setText(QStringLiteral("请填写喊话内容，并设置单独的聊天键和技能键。")); return;
    }
    if (message->text().contains('\n') || message->text().contains('\r') || message->text().contains('\t')) {
        status->setText(QStringLiteral("喊话内容只能是一行文字。")); return;
    }
    if (skillEnabled->isChecked() && interval->value() <= lead->value() + 2) {
        status->setText(QStringLiteral("每轮间隔需比提前秒数多至少 3 秒，以留出聊天输入时间。")); return;
    }
    if (manualFirst->isChecked() && !skillEnabled->isChecked()) {
        status->setText(QStringLiteral("首次手动开花模式需要启用技能键。")); return;
    }
    waitingForFirst = manualFirst->isChecked();
    previousSkillDown = waitingForFirst && keyDown(AntKeyMapper::getInstance()->returnVirtualKey(shoutAlias(skillKey)));
    save(); acquired = false; announced = false; resyncAnnouncement = false; stage = 0; running = true;
#ifdef Q_OS_WIN
    restartHotkeyRegistered = RegisterHotKey(reinterpret_cast<HWND>(winId()), 0x4a52, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F9);
    restartHint->setText(restartHotkeyRegistered ? QStringLiteral("Ctrl+Alt+F9 重新等待手动开花；手动补花时自动重新对时并喊话。")
        : QStringLiteral("Ctrl+Alt+F9 已被占用，请用浮窗的重开循环按钮。"));
#endif
    const bool settingsVisible = isVisible();
    clock.start(); startButton->setEnabled(false); stopButton->setEnabled(true); timer.start(); tick();
    if (settingsVisible) {
        showPopup();
        // Minimize our main window only when it owns focus; floating controls never activate it.
        if (parentWidget() && parentWidget()->window()->isActiveWindow()) parentWidget()->window()->showMinimized();
    }
    refreshPopup();
}
void AutoShoutDialog::stop()
{
#ifdef Q_OS_WIN
    if (restartHotkeyRegistered) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 0x4a52);
#endif
    restartHotkeyRegistered = false; restartPending = false;
    timer.stop(); release(); running = false; acquired = false; waitingForFirst = false; stage = 0;
    startButton->setEnabled(true); stopButton->setEnabled(false); status->setText(QStringLiteral("已停止。")); refreshPopup();
}
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool AutoShoutDialog::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
#else
bool AutoShoutDialog::nativeEvent(const QByteArray &eventType, void *message, long *result)
#endif
{
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 0x4a52) {
        if (running) { restartPending = true; release(); }
        *result = 0; return true;
    }
    return QDialog::nativeEvent(eventType, message, result);
}
#endif
void AutoShoutDialog::restartCycle()
{
    if (!running) return;
    const auto current = foreground();
    if (!current.window || !current.process || (acquired && !(current == target))) {
        status->setText(QStringLiteral("请返回原游戏窗口后重新补花。")); refreshPopup(); return;
    }
    const bool chatOpen = stage >= 1 && stage <= 3;
    release();
    if (chatOpen) { key(Qt::Key_Escape, true); key(Qt::Key_Escape, false); }
    const qint64 now = elapsed();
    target = current; acquired = true; waitingForFirst = false; announced = false; stage = 0;
    due = now + (chatOpen ? 250 : 0);
    restartPending = false;
    if (skillEnabled->isChecked()) {
        waitingForFirst = true; resyncAnnouncement = true;
        previousSkillDown = keyDown(AntKeyMapper::getInstance()->returnVirtualKey(shoutAlias(skillKey)));
        status->setText(QStringLiteral("等待手动补花：按开花键后重新对时并喊话；软件不会替你开花。"));
        refreshPopup(); return;
    }
    nextCast = due + lead->value() * 1000LL + 600;
    tick(); refreshPopup();
}
void AutoShoutDialog::tick()
{
    if (!running) return;
    const qint64 now = elapsed();
    if (restartPending) {
        if (restartModifiersDown()) {
            status->setText(QStringLiteral("已收到补花请求，请松开快捷键…")); return;
        }
        restartPending = false; restartCycle(); return;
    }
    if (waitingForFirst) {
        const bool down = keyDown(AntKeyMapper::getInstance()->returnVirtualKey(shoutAlias(skillKey)));
        const auto current = foreground();
        if (down && !previousSkillDown && current.window && current.process) {
            target = current; acquired = true; waitingForFirst = false;
            // The physical key already reaches the game. Never duplicate the first cast.
            nextCast = now + interval->value() * 1000LL;
            due = resyncAnnouncement ? now : nextCast - lead->value() * 1000LL - 600;
        }
        previousSkillDown = down;
        if (waitingForFirst) {
            status->setText(QStringLiteral("等待首次手动开花：切回游戏，按一次 %1。")
                .arg(skillKey->keySequence().toString(QKeySequence::NativeText))); return;
        }
    }
    if (!waitingForFirst && acquired && skillEnabled->isChecked()) {
        const bool down = keyDown(AntKeyMapper::getInstance()->returnVirtualKey(shoutAlias(skillKey)));
        if (down && !previousSkillDown && !stage && foreground() == target) {
            // Only physical/manual casts re-anchor the pet. No skill key is ever generated.
            nextCast = now + 120000; due = now; announced = false; resyncAnnouncement = true;
        }
        previousSkillDown = down;
    }
    if (!acquired) {
        if (now < 3000) { status->setText(QStringLiteral("%1 秒后开始，请切回游戏…").arg((3000 - now + 999) / 1000)); return; }
        target = foreground();
        if (!target.window || !target.process) { stop(); status->setText(QStringLiteral("未选中游戏窗口，已取消。")); return; }
        acquired = true; due = now;
        nextCast = now + lead->value() * 1000LL + 600;
    }
    if (!(foreground() == target)) {
        release();
        if (stage) { stop(); status->setText(QStringLiteral("喊话中途切出游戏，已停止；请先关闭游戏聊天框再重新开始。")); }
        else {
            // A returning player must receive a fresh warning before a delayed cast.
            if (announced && skillEnabled->isChecked()) { announced = false; due = nextCast - lead->value() * 1000LL - 600; }
            status->setText(QStringLiteral("已暂停，等待返回游戏。"));
        }
        return;
    }
    // Deadlines remain on the initial cadence. Missed cycles are skipped, never rebased on now.
    if (!stage && skillEnabled->isChecked()) {
        const qint64 warningWindow = lead->value() * 1000LL + 600;
        if ((!announced && now > nextCast - lead->value() * 1000LL) ||
            (announced && now > nextCast + 2000)) {
            const qint64 period = interval->value() * 1000LL;
            nextCast += ((now - (nextCast - warningWindow)) / period + 1) * period;
            announced = false; due = nextCast - warningWindow;
        }
    }
    if (stage && now - stepAt > 2000) {
        stop(); status->setText(QStringLiteral("发送中断或系统休眠，已停止；请先关闭游戏聊天框再重新开始。")); return;
    }
    if (stage && now >= stepAt) {
        if (stage == 1) { release(); stage = 2; stepAt = now + 200; }
        else if (stage == 2) {
            EventHandlerFactory::getInstance()->handler()->sendTextEntryEvent(resyncAnnouncement ? resyncMessage->text() : message->text());
            stage = 3; stepAt = now + 200;
        } else if (stage == 3) { key(shoutAlias(chatKey), true); stage = 4; stepAt = now + 100; }
        else if (stage == 4) {
            release(); stage = 0; announced = true;
            // Input time is already reserved before the fixed cast deadline.
            if (skillEnabled->isChecked() && resyncAnnouncement) {
                resyncAnnouncement = false; announced = false;
                due = nextCast - lead->value() * 1000LL - 600;
            } else due = skillEnabled->isChecked() ? nextCast : cycleStart + interval->value() * 1000LL;
        } else if (stage == 5) {
            release(); stage = 0; announced = false;
            nextCast += interval->value() * 1000LL;
            due = nextCast - lead->value() * 1000LL - 600;
        }
    }
    if (stage == 0 && now >= due) {
        if (announced && skillEnabled->isChecked()) {
            // The pet casts independently. Advance notifications without sending any skill input.
            announced = false; nextCast += 120000;
            due = nextCast - lead->value() * 1000LL - 600;
        }
        else {
            // Shout-only mode also advances from its planned deadline, not completion time.
            if (!skillEnabled->isChecked() && now - due >= interval->value() * 1000LL)
                due += ((now - due) / (interval->value() * 1000LL)) * interval->value() * 1000LL;
            cycleStart = due; key(shoutAlias(chatKey), true); stage = 1; stepAt = now + 100;
        }
    }
    status->setText(stage ? QStringLiteral("正在发送按键 / 喊话…") :
        QStringLiteral("%1 · %2 秒").arg(announced && skillEnabled->isChecked() ? QStringLiteral("下次宠物续花（仅计时）") : QStringLiteral("下次喊话"))
            .arg(qMax(0LL, (due - now + 999) / 1000)));
}
void AutoShoutDialog::closeEvent(QCloseEvent *event) { stop(); save(); if (popup) popup->hide(); event->accept(); }
void AutoShoutDialog::reject() { close(); }
