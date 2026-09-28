#ifndef POTIONTIMERDIALOG_H
#define POTIONTIMERDIALOG_H

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <array>
#include <functional>

class AntiMicroSettings;
class QCheckBox;
class QRadioButton;
class QKeySequenceEdit;
class QSpinBox;
class QLabel;
class QPushButton;

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
        QRadioButton *enabled;
        QKeySequenceEdit *key;
        QSpinBox *seconds;
        QLabel *remaining;
        int code = 0, alias = 0;
        qint64 due = 0;
    };
    void tick();
    void releaseKey();
    void save();
    void setEditing(bool enabled);
    void setStatus(const QString &message);
    void refreshPopup();
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
    AntiMicroSettings *settings;
    std::array<Entry, 2> entries;
    QCheckBox *immediate;
    QPushButton *startButton, *stopButton;
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
#endif
