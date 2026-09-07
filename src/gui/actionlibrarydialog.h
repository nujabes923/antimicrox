#ifndef ACTIONLIBRARYDIALOG_H
#define ACTIONLIBRARYDIALOG_H

#include "actionlibrary.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

class AntiMicroSettings;
class GameController;
class JoyButton;
class QCheckBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QLabel;
class QPushButton;

class ActionLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ActionLibraryDialog(AntiMicroSettings *settings, JoyButton *target = nullptr, QWidget *parent = nullptr);
    ~ActionLibraryDialog();
    static QString libraryDirectory(AntiMicroSettings *settings);
    JoyButton *draftButton() const { return draft; }
public slots:
    void stopTrial();
    void saveAction();
    void editSteps();
    void newAction();
    void useExample();
    void captureTarget();
    void assignAction(bool append = false);
    void previewAction(bool real = false);
    void undoDraft();
    void redoDraft();
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    void refreshLibrary();
    void refreshSteps();
    void loadAction(const QString &id);
    bool discardDraft();
    void markDirty();
    bool setDraft(const LibraryAction &action);
    LibraryAction currentAction() const;
    void duplicateAction();
    void importAction();
    void exportAction();
    void addTimedKey();
    void moveStep(int direction);
    void moveStepTo(int source, int destination);
    void duplicateStep();
    void removeStep();
    void restoreHistory();
    void tickTrial();
    void setTrialUi(bool running);
    void report(const QString &message, bool error = false);
    AntiMicroSettings *settings;
    ActionLibrary library;
    LibraryAction selected;
    GameController *scratch;
    JoyButton *draft;
    QPointer<JoyButton> target;
    QPointer<QDialog> stepEditor;
    QLineEdit *search, *actionName, *tags, *description;
    QCheckBox *favoritesOnly, *favorite;
    QListWidget *actions;
    QTableWidget *table;
    QLabel *summary, *status, *targetSummary;
    QWidget *editingPanel, *libraryPanel;
    QPushButton *stopButton, *saveButton, *assignButton, *appendButton;
    QPushButton *undoButton, *redoButton;
    bool loading = false, dirty = false, realTrial = false, trialRunning = false;
    QList<ActionPreviewEvent> trialEvents, trialHeld;
    QTimer trialTimer;
    QElapsedTimer trialClock;
    int trialIndex = 0, trialDuration = 0, countdown = 0;
    quintptr trialTargetWindow = 0;
    QList<QByteArray> history;
    int historyIndex = 0;
};
#endif
