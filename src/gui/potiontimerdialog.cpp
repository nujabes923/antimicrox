#include "potiontimerdialog.h"
#include "antimicrosettings.h"
#include "antkeymapper.h"
#include "event.h"
#include "joybuttonslot.h"
#include <QApplication>
#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QCloseEvent>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
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
    auto *help = new QLabel(QStringLiteral("选择一种药水并设置游戏内按键，软件自动安排补用时间。\n"
        "怀旧版基础持续时间：攻击药水 3 分钟，龙血 5 分钟，均提前 5 秒补用。\n"
        "点击开始后，3 秒内切换到游戏；修改药水或按键会停止计时，需重新开始。\n"
        "切到其他窗口时暂停发送，返回游戏后继续；关闭本窗口会停止。\n"
        "Ctrl+Alt+F10 可紧急停止所有自动操作。"), this);
    help->setWordWrap(true);
    layout->addWidget(help, 0, 0, 1, 4);
    layout->addWidget(new QLabel(QStringLiteral("选择药水（二选一）"), this), 1, 0);
    layout->addWidget(new QLabel(QStringLiteral("游戏内按键（单键）"), this), 1, 1);
    layout->addWidget(new QLabel(QStringLiteral("间隔"), this), 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("下次使用"), this), 1, 3);
    auto *selection = new QButtonGroup(this);
    selection->setExclusive(true);
    for (int i = 0; i < 2; ++i) {
        auto &entry = entries[i];
        const QString prefix = QString("PotionTimer/%1/").arg(i);
        entry.enabled = new QRadioButton(i == 0 ? QStringLiteral("攻击药水") : QStringLiteral("龙血"), this);
        selection->addButton(entry.enabled, i);
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
        entry.enabled->setChecked(settings->value("PotionTimer/Selected", 0).toInt() == i);
        entry.key->setKeySequence(QKeySequence::fromString(settings->value(prefix + "Key").toString(), QKeySequence::PortableText));
        entry.seconds->setValue(i == 0 ? 175 : 295);
        entry.seconds->setReadOnly(true);
        entry.seconds->setButtonSymbols(QAbstractSpinBox::NoButtons);
        entry.seconds->setToolTip(i == 0 ? QStringLiteral("攻击药水／攻击神水：180 秒效果，提前 5 秒补用")
                                           : QStringLiteral("龙血／暴龙之血：300 秒效果，提前 5 秒补用"));
        layout->addWidget(entry.enabled, i + 2, 0);
        layout->addWidget(entry.key, i + 2, 1);
        layout->addWidget(entry.seconds, i + 2, 2);
        layout->addWidget(entry.remaining, i + 2, 3);
    }
    for (auto &entry : entries) {
        connect(entry.enabled, &QRadioButton::toggled, this, [this]() { stop(); save(); });
        connect(entry.key, &QKeySequenceEdit::keySequenceChanged, this, [this]() { stop(); save(); });
    }
    immediate = new QCheckBox(QStringLiteral("开始时先使用一次"), this);
    immediate->setObjectName("potionImmediate");
    immediate->setChecked(settings->value("PotionTimer/Immediate", true).toBool());
    layout->addWidget(immediate, 4, 0, 1, 4);
    startButton = new QPushButton(QStringLiteral("开始（3 秒后）"), this);
    stopButton = new QPushButton(QStringLiteral("停止"), this);
    stopButton->setEnabled(false);
    layout->addWidget(startButton, 5, 0, 1, 2);
    layout->addWidget(stopButton, 5, 2, 1, 2);
    status = new QLabel(QStringLiteral("设置会自动保存；每次打开程序后需手动开始。"), this);
    status->setWordWrap(true);
    layout->addWidget(status, 6, 0, 1, 4);
    auto *sources = new QLabel(QStringLiteral("持续时间资料：<a href=\"https://mxdzlk.com/item/2002004/\">攻击药水</a> · "
        "<a href=\"https://mxdzlk.com/item/2012000/\">龙血</a>（按基础时长，不含技能延长）"), this);
    sources->setOpenExternalLinks(true);
    layout->addWidget(sources, 7, 0, 1, 4);
    auto *popupButton = new QPushButton(QStringLiteral("小浮窗 · 状态 / 开关"), this);
    popupButton->setObjectName("showBuffPopupButton");
    layout->addWidget(popupButton, 8, 0, 1, 4);
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
            for (const auto &entry : entries) if (entry.code) {
                qint64 seconds = qMax(0LL, (entry.due - now + 999) / 1000);
                countdown = seconds == 0 && paused ? QStringLiteral("已到时间，返回游戏后补用")
                    : QStringLiteral("下次用药 %1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
            }
        }
    } else if (status->text() != QStringLiteral("已停止。")) {
        countdown = status->text();
    }
    QString selected;
    for (const auto &entry : entries) if (entry.enabled->isChecked())
        selected = entry.enabled->text() + QStringLiteral(" · ") + (entry.key->keySequence().isEmpty()
            ? QStringLiteral("未设置按键") : entry.key->keySequence().toString(QKeySequence::NativeText));
    popupStatus->setText(state);
    popupDetails->setWordWrap(true);
    popupDetails->setText(selected + (countdown.isEmpty() ? QString() : "\n" + countdown));
    popupDetails->setMinimumWidth(250);
    popupToggle->setText(running ? QStringLiteral("■ 停止") : QStringLiteral("▶ 开始（3 秒）"));
}

void PotionTimerDialog::save()
{
    for (int i = 0; i < 2; ++i) {
        const auto &entry = entries[i];
        const QString prefix = QString("PotionTimer/%1/").arg(i);
        settings->setValue(prefix + "Enabled", entry.enabled->isChecked());
        if (entry.enabled->isChecked()) settings->setValue("PotionTimer/Selected", i);
        settings->setValue(prefix + "Key", entry.key->keySequence().toString(QKeySequence::PortableText));
        settings->setValue(prefix + "Seconds", entry.seconds->value());
    }
    settings->setValue("PotionTimer/Immediate", immediate->isChecked());
    settings->sync();
}

void PotionTimerDialog::setEditing(bool enabled)
{
    immediate->setEnabled(enabled);
    startButton->setEnabled(enabled); stopButton->setEnabled(!enabled);
}

void PotionTimerDialog::start()
{
    if (running) return;
    bool any = false;
    for (auto &entry : entries) {
        entry.code = 0;
        if (!entry.enabled->isChecked()) continue;
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
    for (int i = 0; i < 2; ++i) {
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
