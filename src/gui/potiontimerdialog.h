#ifndef POTIONTIMERDIALOG_H
#define POTIONTIMERDIALOG_H

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <vector>

class AntiMicroSettings;
class QCheckBox;
class QRadioButton;
class QKeySequenceEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QAbstractButton;
class QGridLayout;
class QWidget;

class PotionTimerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PotionTimerDialog(AntiMicroSettings *settings, QWidget *parent = nullptr);
    ~PotionTimerDialog() override;
    // Injectable window query lets regression tests use the recording input backend.
    struct Target { quintptr window = 0; quint32 process = 0; bool operator==(const Target &other) const
        { return window == other.window && process == other.process; } };
    std::function<Target()> foreground;
    std::function<qint64()> elapsed;
    bool isRunning() const { return running; }
public slots:
    void start();
    void stop();
    void showPopup();
private:
    struct Entry {
        QAbstractButton *enabled;
        QKeySequenceEdit *key;
        QSpinBox *seconds;
        QLabel *remaining;
        QLineEdit *name = nullptr;
        QWidget *row = nullptr;
        QPushButton *remove = nullptr;
        int code = 0, alias = 0;
        qint64 due = 0;
    };
    void tick();
    void releaseKey();
    void save();
    void setEditing(bool enabled);
    void setStatus(const QString &message);
    void refreshPopup();
    void addCustomEntry(bool saveAfter = true);
    void removeLastCustomEntry();
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
    AntiMicroSettings *settings;
    std::vector<Entry> entries;
    QCheckBox *immediate;
    QPushButton *startButton, *stopButton;
    QPushButton *addBuffButton = nullptr;
    QWidget *customRows = nullptr;
    QGridLayout *customLayout = nullptr;
    QLabel *status;
    QDialog *popup = nullptr;
    QLabel *popupStatus = nullptr, *popupDetails = nullptr;
    QPushButton *popupToggle = nullptr;
    QTimer timer;
    QElapsedTimer clock;
    Target target;
    bool running = false, acquired = false;
    int held = -1;
    qint64 releaseAt = 0, nextPressAt = 0;
};
// Independent skill/shout scheduler; uses the same target-window contract as buffs.
class AutoShoutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AutoShoutDialog(AntiMicroSettings *settings, QWidget *parent = nullptr);
    ~AutoShoutDialog() override;
    std::function<PotionTimerDialog::Target()> foreground;
    std::function<qint64()> elapsed;
    std::function<bool(int)> keyDown;
    bool isRunning() const { return running; }
public slots:
    void start();
    void stop();
    void showPopup();
    void restartCycle();
protected:
#ifdef Q_OS_WIN
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif
#endif
private:
    void refreshPopup();
    QDialog *popup = nullptr;
    QLabel *popupStatus = nullptr;
    QPushButton *popupToggle = nullptr;
    void tick();
    void save();
    void key(int alias, bool down);
    void release();
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
    AntiMicroSettings *settings;
    QLineEdit *message, *resyncMessage;
    QSpinBox *interval, *lead;
    QKeySequenceEdit *chatKey, *skillKey;
    QCheckBox *skillEnabled, *manualFirst;
    QPushButton *startButton, *stopButton;
    QLabel *status;
    QTimer timer;
    QElapsedTimer clock;
    PotionTimerDialog::Target target;
    bool running = false, acquired = false, announced = false;
    bool waitingForFirst = false, previousSkillDown = false;
    bool restartHotkeyRegistered = false, restartPending = false, resyncAnnouncement = false;
    QLabel *restartHint;
    std::function<bool()> restartModifiersDown = []() { return false; };
    int stage = 0, heldAlias = 0;
    qint64 due = 0, stepAt = 0, cycleStart = 0, nextCast = 0;
};
#endif
