#include "actionlibrarydialog.h"
#include "advancebuttondialog.h"
#include "antimicrosettings.h"
#include "antkeymapper.h"
#include "common.h"
#include "event.h"
#include "gamecontroller/gamecontroller.h"
#include "joybuttontypes/joybutton.h"
#include "mainwindow.h"
#include "qtconnectionhelper.h"
#include "setjoystick.h"
#include "xml/joybuttonslotxml.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QFileInfo>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include "winextras.h"
#endif

namespace {
class ActionStepTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    std::function<void(int, int)> reorder;
protected:
    void dropEvent(QDropEvent *event) override
    {
        if (event->source() != this) { event->ignore(); return; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        int row = indexAt(event->position().toPoint()).row();
#else
        int row = indexAt(event->pos()).row();
#endif
        if (row < 0) row = rowCount() - 1;
        if (reorder) reorder(currentRow(), row);
        event->acceptProposedAction();
    }
};
}

QString ActionLibraryDialog::libraryDirectory(AntiMicroSettings *settings)
{
    return settings->value("ActionLibrary/Directory", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                                                     "/AntiMicroX-RandomTurbo/actions").toString();
}

ActionLibraryDialog::ActionLibraryDialog(AntiMicroSettings *settings, JoyButton *target, QWidget *parent)
    : QDialog(parent, Qt::Window), settings(settings), library(libraryDirectory(settings)), target(target)
{
    setObjectName("actionLibraryDialog");
    setWindowTitle(tr("Custom Action Library"));
    resize(1060, 720);
    scratch = new GameController(nullptr, 0, settings, 0, this);
    draft = scratch->getActiveSetJoystick()->getJoyButton(0);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("CUSTOM ACTION LIBRARY"), this);
    heading->setStyleSheet("font-size: 19px; font-weight: 600;");
    layout->addWidget(heading);
    layout->addWidget(new QLabel(tr("Create once. Save independently. Copy to any controller button."), this));
    auto *splitter = new QSplitter(this);
    layout->addWidget(splitter, 1);
    libraryPanel = new QWidget(splitter);
    auto *left = new QVBoxLayout(libraryPanel);
    search = new QLineEdit(libraryPanel);
    search->setObjectName("actionSearch");
    search->setPlaceholderText(tr("Search names and tags"));
    left->addWidget(search);
    favoritesOnly = new QCheckBox(tr("Favorites only"), libraryPanel);
    favoritesOnly->setObjectName("favoriteActionsOnly");
    left->addWidget(favoritesOnly);
    actions = new QListWidget(libraryPanel);
    actions->setObjectName("savedActions");
    left->addWidget(actions, 1);
    auto *newRow = new QHBoxLayout;
    auto add = [](QBoxLayout *row, const QString &label, const std::function<void()> &fn) {
        auto *button = new QPushButton(label); row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, button, fn); return button;
    };
    left->addLayout(newRow);
    add(newRow, tr("New"), [this]() { newAction(); });
    add(newRow, tr("Duplicate"), [this]() { duplicateAction(); });
    auto *files = new QHBoxLayout;
    left->addLayout(files);
    add(files, tr("Import"), [this]() { importAction(); });
    add(files, tr("Export"), [this]() { exportAction(); });
    auto *folder = new QPushButton(tr("Open Library Folder"), libraryPanel);
    left->addWidget(folder);
    connect(folder, &QPushButton::clicked, this, [this]() {
        QDir().mkpath(library.directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(library.directory()));
    });
    editingPanel = new QWidget(splitter);
    auto *right = new QVBoxLayout(editingPanel);
    auto *form = new QFormLayout;
    actionName = new QLineEdit; actionName->setObjectName("actionName");
    tags = new QLineEdit; tags->setPlaceholderText(tr("Movement, farming, utility…"));
    description = new QLineEdit; description->setPlaceholderText(tr("What this action does"));
    favorite = new QCheckBox(tr("Favorite"));
    favorite->setObjectName("actionFavorite");
    form->addRow(tr("Name"), actionName); form->addRow(tr("Tags"), tags);
    form->addRow(tr("Notes"), description); form->addRow("", favorite); right->addLayout(form);
    auto *tools = new QHBoxLayout;
    right->addLayout(tools);
    add(tools, tr("Add Key + Timing"), [this]() { addTimedKey(); });
    auto *edit = add(tools, tr("Edit Steps…"), [this]() { editSteps(); });
    edit->setObjectName("editActionSteps");
    add(tools, tr("↑"), [this]() { moveStep(-1); });
    add(tools, tr("↓"), [this]() { moveStep(1); });
    auto *stepTable = new ActionStepTable(0, 3, editingPanel);
    stepTable->reorder = [this](int source, int destination) { moveStepTo(source, destination); };
    table = stepTable;
    table->setObjectName("actionSteps");
    table->setHorizontalHeaderLabels({tr("Step"), tr("Action"), tr("Value")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setDragDropMode(QAbstractItemView::InternalMove);
    table->setDefaultDropAction(Qt::MoveAction);
    table->setDragDropOverwriteMode(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    right->addWidget(table, 1);
    auto *rowTools = new QHBoxLayout;
    right->addLayout(rowTools);
    undoButton = add(rowTools, tr("Undo"), [this]() { undoDraft(); });
    redoButton = add(rowTools, tr("Redo"), [this]() { redoDraft(); });
    add(rowTools, tr("Duplicate Step"), [this]() { duplicateStep(); });
    add(rowTools, tr("Delete Step"), [this]() { removeStep(); });
    auto *undo = new QShortcut(QKeySequence::Undo, this);
    auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, this, &ActionLibraryDialog::undoDraft);
    connect(redo, &QShortcut::activated, this, &ActionLibraryDialog::redoDraft);
    connect(table, &QTableWidget::cellDoubleClicked, this, [this]() { editSteps(); });
    summary = new QLabel; summary->setObjectName("actionDuration"); summary->setWordWrap(true);
    right->addWidget(summary);
    auto *sample = new QHBoxLayout;
    right->addLayout(sample);
    add(sample, tr("Example: Left / Right · 1 s"), [this]() { useExample(); });
#ifdef Q_OS_WIN
    auto *record = add(sample, QString::fromUtf8("快速录制…"), [this]() {
        stopTrial();
        for (QWidget *ancestor = parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget())
            if (auto *main = qobject_cast<MainWindow *>(ancestor)) { main->emergencyStopAll(); break; }
        const QString executable = QCoreApplication::applicationDirPath() + "/ActionRecorder.exe";
        if (!QFileInfo::exists(executable))
        { report(tr("ActionRecorder.exe is missing from the application folder."), true); return; }
        auto *process = new QProcess(qApp);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { refreshLibrary(); });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { report(process->errorString(), true); process->deleteLater(); }
        });
        process->start(executable, {"--library", library.directory()});
        report(QString::fromUtf8("录制窗口已打开。F8 录制，F10 停止；保存并关闭后，动作会出现在左侧列表。"));
    });
    record->setObjectName("quickRecordAction");
#endif
    auto *capture = add(sample, tr("Capture Current Button"), [this]() { captureTarget(); });
    capture->setEnabled(target != nullptr);
    splitter->setSizes({280, 760});
    targetSummary = new QLabel(this); targetSummary->setWordWrap(true);
    targetSummary->setText(target ? tr("Target: %1 · %2\nAssignment copies the steps; existing toggle/turbo settings stay with the button.")
                                     .arg(target->getPartialName(), target->getActionName())
                                  : tr("To assign an action, open a controller button and choose Action Library."));
    layout->addWidget(targetSummary);
    status = new QLabel(this); status->setObjectName("actionLibraryStatus"); status->setWordWrap(true);
    layout->addWidget(status);
    auto *bottom = new QHBoxLayout;
    layout->addLayout(bottom);
    saveButton = add(bottom, tr("Save Action"), [this]() { saveAction(); });
    saveButton->setObjectName("saveLibraryAction");
    add(bottom, tr("Preview"), [this]() { previewAction(false); });
    add(bottom, tr("Trial Once · 3 s"), [this]() { previewAction(true); });
    stopButton = add(bottom, tr("Stop"), [this]() { stopTrial(); }); stopButton->setEnabled(false);
    stopButton->setObjectName("stopActionTrial");
    bottom->addStretch();
    assignButton = add(bottom, tr("Replace Target"), [this]() { assignAction(false); });
    appendButton = add(bottom, tr("Append to Target"), [this]() { assignAction(true); });
    assignButton->setEnabled(target != nullptr); appendButton->setEnabled(target != nullptr);
    add(bottom, tr("Close"), [this]() { close(); });
    connect(search, &QLineEdit::textChanged, this, [this]() { refreshLibrary(); });
    connect(favoritesOnly, &QCheckBox::toggled, this, [this]() { refreshLibrary(); });
    connect(actions, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { loadAction(item->data(Qt::UserRole).toString()); });
    for (QLineEdit *field : {actionName, tags, description})
        connect(field, &QLineEdit::textEdited, this, [this]() { markDirty(); });
    connect(favorite, &QCheckBox::toggled, this, [this]() { markDirty(); });
    connect(draft, &JoyButton::slotsChanged, this, [this]() { markDirty(); refreshSteps(); }, Qt::QueuedConnection);
    auto *save = new QShortcut(QKeySequence::Save, this);
    connect(save, &QShortcut::activated, this, &ActionLibraryDialog::saveAction);
    trialTimer.setInterval(10);
    connect(&trialTimer, &QTimer::timeout, this, &ActionLibraryDialog::tickTrial);
    connect(qApp, &QApplication::aboutToQuit, this, &ActionLibraryDialog::stopTrial);
    if (target)
        connect(target, &QObject::destroyed, this, [this]() {
            stopTrial(); assignButton->setEnabled(false); appendButton->setEnabled(false);
            targetSummary->setText(tr("Target disconnected. Your independent action draft is still available."));
        });
    setDraft(LibraryAction{});
    refreshLibrary();
    report(tr("Create a new action, capture a button, or start with the 1-second example."));
}

ActionLibraryDialog::~ActionLibraryDialog() { stopTrial(); }

void ActionLibraryDialog::report(const QString &message, bool error)
{
    status->setStyleSheet(error ? "color: #b3261e;" : ""); status->setText(message);
}

void ActionLibraryDialog::markDirty()
{
    if (loading) return;
    // Queued assignment notifications may arrive after loading a clean draft.
    const auto current = currentAction();
    dirty = current.name != selected.name || current.tags != selected.tags || current.description != selected.description ||
            current.favorite != selected.favorite || current.assignments != selected.assignments ||
            (selected.revision == 0 && (!current.name.isEmpty() || !draft->getAssignedSlots()->isEmpty()));
    setWindowTitle(tr("Custom Action Library") + (dirty ? " *" : ""));
}

LibraryAction ActionLibraryDialog::currentAction() const
{
    LibraryAction result = selected;
    result.name = actionName->text(); result.tags = tags->text(); result.description = description->text();
    result.favorite = favorite->isChecked(); result.assignments = ActionLibrary::snapshot(draft);
    return result;
}

bool ActionLibraryDialog::setDraft(const LibraryAction &action)
{
    stopTrial();
    const auto assignments = action.assignments.isEmpty() ? ActionLibrary::combine({}) : action.assignments;
    QString problem;
    loading = true;
    if (!ActionLibrary::apply(draft, assignments, false, &problem))
    { loading = false; report(problem, true); return false; }
    selected = action;
    if (selected.assignments.isEmpty()) selected.assignments = ActionLibrary::combine({});
    actionName->setText(selected.name); tags->setText(selected.tags); description->setText(selected.description);
    favorite->setChecked(selected.favorite);
    history = {ActionLibrary::snapshot(draft)}; historyIndex = 0;
    dirty = false; loading = false; refreshSteps();
    setWindowTitle(tr("Custom Action Library"));
    return true;
}

bool ActionLibraryDialog::discardDraft()
{
    markDirty();
    if (!dirty) return true;
    auto answer = QMessageBox::question(this, tr("Unsaved Action"), tr("Save this action before continuing?"),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) { saveAction(); return !dirty; }
    return true;
}

void ActionLibraryDialog::refreshLibrary()
{
    const QSignalBlocker blocker(actions);
    actions->clear();
    QString problem;
    auto items = library.list(&problem);
    std::sort(items.begin(), items.end(), [](const LibraryAction &a, const LibraryAction &b) {
        if (a.favorite != b.favorite) return a.favorite;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    for (const auto &action : items)
    {
        if (favoritesOnly->isChecked() && !action.favorite) continue;
        if (!(action.name + " " + action.tags).contains(search->text(), Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem((action.favorite ? QString::fromUtf8("★ ") : "") + action.name +
                                        QString("  · v%1").arg(action.revision), actions);
        item->setData(Qt::UserRole, action.id); item->setToolTip(action.tags + "\n" + action.description);
        if (action.id == selected.id) actions->setCurrentItem(item);
    }
    if (!problem.isEmpty()) report(problem, true);
}

void ActionLibraryDialog::loadAction(const QString &id)
{
    if (!discardDraft()) { refreshLibrary(); return; }
    for (const auto &action : library.list())
        if (action.id == id) { if (!setDraft(action)) return; report(tr("Loaded %1 · version %2").arg(action.name).arg(action.revision)); return; }
    report(tr("Action no longer exists. Refresh the library."), true);
}

void ActionLibraryDialog::newAction() { if (discardDraft()) { setDraft({}); actionName->setFocus(); } }

void ActionLibraryDialog::useExample()
{
    if (!discardDraft()) return;
    LibraryAction action; action.name = QString::fromUtf8("原地左右防呆 1s");
    action.tags = tr("Movement, example");
    action.description = tr("Left 300 ms, wait 200 ms, right 300 ms, wait 200 ms.");
    action.assignments = ActionLibrary::example(); if (!setDraft(action)) return; dirty = true;
    report(tr("Example is a draft. Save it to add it to your library."));
}

void ActionLibraryDialog::captureTarget()
{
    if (!target || !discardDraft()) return;
    LibraryAction action; action.name = target->getActionName().isEmpty() ? target->getPartialName() : target->getActionName();
    action.assignments = ActionLibrary::snapshot(target); if (!setDraft(action)) return; dirty = true;
    report(tr("Copied the current button's steps. Name the action and save it."));
}

void ActionLibraryDialog::duplicateAction()
{
    auto action = currentAction(); action.id.clear(); action.revision = 0; action.name += tr(" (copy)");
    if (!setDraft(action)) return; dirty = true; report(tr("Duplicate created. Save it as a new independent action."));
}

void ActionLibraryDialog::saveAction()
{
    if (trialRunning) return;
    auto action = currentAction(); QString problem;
    if (!library.save(action, &problem)) { report(problem, true); return; }
    selected = action; dirty = false; setWindowTitle(tr("Custom Action Library"));
    refreshLibrary(); report(tr("Saved %1 · version %2. Existing assignments were not changed.").arg(action.name).arg(action.revision));
}

void ActionLibraryDialog::importAction()
{
    if (!discardDraft()) return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Import Action"), {}, tr("Action files (*.json)"));
    if (file.isEmpty()) return;
    LibraryAction action; QString problem;
    if (!ActionLibrary::read(file, action, &problem)) { report(problem, true); return; }
    action.id.clear(); action.revision = 0; if (!setDraft(action)) return; dirty = true;
    report(tr("Imported as a new draft. Save to keep it; existing actions are not overwritten."));
}

void ActionLibraryDialog::exportAction()
{
    const auto action = currentAction(); QString problem;
    if (action.name.trimmed().isEmpty() || ActionLibrary::steps(action.assignments).isEmpty())
    { report(tr("Add a name and steps before exporting."), true); return; }
    const QString file = QFileDialog::getSaveFileName(this, tr("Export Action"), "custom-action.json", tr("Action files (*.json)"));
    if (!file.isEmpty() && !ActionLibrary::exportAction(file, action, &problem)) report(problem, true);
}

void ActionLibraryDialog::refreshSteps()
{
    const QByteArray snapshot = ActionLibrary::snapshot(draft);
    if (history.isEmpty()) { history.append(snapshot); historyIndex = 0; }
    else if (!loading && history.at(historyIndex) != snapshot)
    {
        while (history.size() > historyIndex + 1) history.removeLast();
        history.append(snapshot);
        if (history.size() > 101) history.removeFirst();
        historyIndex = history.size() - 1;
    }
    undoButton->setEnabled(historyIndex > 0); redoButton->setEnabled(historyIndex + 1 < history.size());
    const auto items = ActionLibrary::steps(snapshot);
    const int row = table->currentRow(); table->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        JoyButtonSlot slot; QXmlStreamReader reader(items.at(i)); reader.readNextStartElement();
        JoyButtonSlotXml parser(&slot); parser.readConfig(&reader);
        QString kind = tr("Key / action"), value = slot.getSlotString();
        switch (slot.getSlotMode())
        {
        case JoyButtonSlot::JoyKeyPress: kind = tr("Hold duration"); value = tr("%1 ms (next key group)").arg(slot.getSlotCode()); break;
        case JoyButtonSlot::JoyPause: kind = tr("Release + wait"); value = tr("%1 ms").arg(slot.getSlotCode()); break;
        case JoyButtonSlot::JoyDelay:
            kind = tr("Delay (keep held)");
            value = slot.isUsingRandomDelay() ? tr("Random %1–%2 ms").arg(slot.getRandomDelayMinimum()).arg(slot.getRandomDelayMaximum())
                                             : tr("%1 ms").arg(slot.getSlotCode()); break;
        case JoyButtonSlot::JoyMix: kind = tr("Simultaneous keys"); break;
        default: break;
        }
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        table->setItem(i, 1, new QTableWidgetItem(kind)); table->setItem(i, 2, new QTableWidgetItem(value));
        slot.cleanMixSlots();
    }
    if (row >= 0 && row < table->rowCount()) table->selectRow(row);
    QList<ActionPreviewEvent> events; int duration; QString problem;
    const bool simple = ActionLibrary::preview(ActionLibrary::snapshot(draft), events, duration, &problem, false);
    summary->setText(simple ? tr("%1 steps · One pass: up to %2 s · Double-click a row to edit. Default key hold: 100 ms.")
                                    .arg(items.size()).arg(duration / 1000.0, 0, 'f', 2)
                           : tr("%1 steps · %2").arg(items.size()).arg(problem));
}

void ActionLibraryDialog::editSteps()
{
    if (trialRunning) return;
    if (stepEditor) { stepEditor->raise(); return; }
    auto *editor = new AdvanceButtonDialog(draft, this);
    editor->setWindowTitle(tr("Edit Library Action Steps"));
    editor->setWindowModality(Qt::WindowModal);
    editor->setProperty("actionLibraryEditor", true);
    stepEditor = editor;
    // The library saves actions, rather than controller profiles.
    for (const QString &name : {QStringLiteral("saveProfileButton"), QStringLiteral("openProfileBackupsButton")})
        if (auto *button = editor->findChild<QPushButton *>(name)) button->hide();
    const int row = table->currentRow();
    if (row >= 0)
    {
        auto *items = editor->findChild<QListWidget *>("slotListWidget");
        if (items && row < items->count())
        {
            items->setCurrentRow(row);
            QMetaObject::invokeMethod(editor, "performStatsWidgetRefresh", Q_ARG(QListWidgetItem *, items->item(row)));
        }
    }
    connect(editor, &QDialog::finished, this, [this]() { markDirty(); refreshSteps(); });
    editor->show();
}

void ActionLibraryDialog::moveStep(int direction)
{
    moveStepTo(table->currentRow(), table->currentRow() + direction);
}

void ActionLibraryDialog::moveStepTo(int source, int destination)
{
    auto items = ActionLibrary::steps(ActionLibrary::snapshot(draft));
    if (source < 0 || source >= items.size() || destination < 0 || destination >= items.size()) return;
    items.move(source, destination); QString problem;
    ActionLibrary::apply(draft, ActionLibrary::combine(items), false, &problem);
    refreshSteps(); table->selectRow(destination); markDirty();
}

void ActionLibraryDialog::duplicateStep()
{
    auto items = ActionLibrary::steps(ActionLibrary::snapshot(draft)); const int row = table->currentRow();
    if (row < 0 || row >= items.size()) return;
    items.insert(row + 1, items.at(row)); QString problem;
    ActionLibrary::apply(draft, ActionLibrary::combine(items), false, &problem);
    refreshSteps(); table->selectRow(row + 1); markDirty();
}

void ActionLibraryDialog::removeStep()
{
    auto items = ActionLibrary::steps(ActionLibrary::snapshot(draft)); const int row = table->currentRow();
    if (row < 0 || row >= items.size()) return;
    items.removeAt(row); QString problem;
    ActionLibrary::apply(draft, ActionLibrary::combine(items), false, &problem);
    refreshSteps(); markDirty();
}

void ActionLibraryDialog::undoDraft()
{
    if (trialRunning) return;
    refreshSteps();
    if (historyIndex > 0) { --historyIndex; restoreHistory(); }
}

void ActionLibraryDialog::redoDraft()
{
    if (trialRunning) return;
    refreshSteps();
    if (historyIndex + 1 < history.size()) { ++historyIndex; restoreHistory(); }
}

void ActionLibraryDialog::restoreHistory()
{
    QString problem;
    ActionLibrary::apply(draft, history.at(historyIndex), false, &problem);
    refreshSteps(); markDirty();
}

void ActionLibraryDialog::addTimedKey()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("Add Key or Chord"));
    auto *form = new QFormLayout(&dialog);
    auto *key = new QKeySequenceEdit(&dialog);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    key->setMaximumSequenceLength(1);
#endif
    auto *hold = new QSpinBox(&dialog); hold->setRange(1, 600000); hold->setValue(300); hold->setSuffix(" ms");
    auto *wait = new QSpinBox(&dialog); wait->setRange(0, 600000); wait->setValue(200); wait->setSuffix(" ms");
    form->addRow(tr("Key / simultaneous chord"), key); form->addRow(tr("Hold"), hold); form->addRow(tr("Wait after release"), wait);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || key->keySequence().isEmpty()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int combined = key->keySequence()[0].toCombined();
#else
    const int combined = key->keySequence()[0];
#endif
    QList<int> keys;
    for (auto modifier : {qMakePair(int(Qt::CTRL), int(Qt::Key_Control)), qMakePair(int(Qt::ALT), int(Qt::Key_Alt)),
                          qMakePair(int(Qt::SHIFT), int(Qt::Key_Shift)), qMakePair(int(Qt::META), int(Qt::Key_Meta))})
        if (combined & modifier.first) keys.append(modifier.second);
    keys.append(combined & ~int(Qt::KeyboardModifierMask));
    QList<QByteArray> items = ActionLibrary::steps(ActionLibrary::snapshot(draft));
    auto serialize = [](JoyButtonSlot &slot) { QByteArray bytes; QXmlStreamWriter writer(&bytes); JoyButtonSlotXml xml(&slot); xml.writeConfig(&writer); return bytes; };
    JoyButtonSlot length(hold->value(), JoyButtonSlot::JoyKeyPress); items.append(serialize(length));
    for (int qtKey : keys)
    {
        const int native = AntKeyMapper::getInstance()->returnVirtualKey(qtKey);
        if (native == 0) { report(tr("This key is not supported by the current keyboard backend."), true); return; }
        JoyButtonSlot slot(native, qtKey, JoyButtonSlot::JoyKeyboard); items.append(serialize(slot));
    }
    JoyButtonSlot pause(wait->value(), JoyButtonSlot::JoyPause); items.append(serialize(pause));
    QString problem; ActionLibrary::apply(draft, ActionLibrary::combine(items), false, &problem); refreshSteps(); markDirty();
}

void ActionLibraryDialog::assignAction(bool append)
{
    if (!target || trialRunning) return;
    const auto action = currentAction(); QString problem;
    if (ActionLibrary::steps(action.assignments).isEmpty()) { report(tr("Add action steps first."), true); return; }
    if (!append && !target->getAssignedSlots()->isEmpty())
    {
        if (QMessageBox::question(this, tr("Replace Button Steps"),
            tr("Replace %1's %2 existing steps with %3's %4 steps?\nThe controller profile is not saved automatically.")
            .arg(target->getPartialName()).arg(target->getAssignedSlots()->size()).arg(action.name)
            .arg(ActionLibrary::steps(action.assignments).size())) != QMessageBox::Yes) return;
    }
    if (!ActionLibrary::apply(target, action.assignments, append, &problem)) { report(problem, true); return; }
    report(tr("Copied to %1. Save the controller profile to keep this assignment. Library updates will not change it.").arg(target->getPartialName()));
}

void ActionLibraryDialog::setTrialUi(bool running)
{
    editingPanel->setEnabled(!running); libraryPanel->setEnabled(!running); saveButton->setEnabled(!running);
    assignButton->setEnabled(!running && target); appendButton->setEnabled(!running && target); stopButton->setEnabled(running);
}

void ActionLibraryDialog::previewAction(bool real)
{
    stopTrial(); QString problem;
    if (!ActionLibrary::preview(ActionLibrary::snapshot(draft), trialEvents, trialDuration, &problem))
    { report(problem, true); return; }
    if (real)
        for (QWidget *ancestor = parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget())
            if (auto *main = qobject_cast<MainWindow *>(ancestor)) { main->emergencyStopAll(); break; }
    realTrial = real; countdown = real ? 3000 : 0; trialIndex = 0; trialRunning = true;
    trialTargetWindow = 0;
    trialClock.start(); setTrialUi(true); trialTimer.start(); tickTrial();
}

void ActionLibraryDialog::tickTrial()
{
    if (!trialRunning) return;
    int elapsed = int(trialClock.elapsed()) - countdown;
    if (elapsed < 0) { report(tr("Trial starts in %1 s. Focus the target window now. Ctrl+Alt+F10 stops everything.").arg((-elapsed + 999) / 1000)); return; }
#ifdef Q_OS_WIN
    if (realTrial && countdown > 0)
    {
        DWORD process = 0; GetWindowThreadProcessId(GetForegroundWindow(), &process);
        if (process == 0 || process == GetCurrentProcessId())
        { stopTrial(); report(tr("Trial cancelled: focus a different target window during the countdown."), true); return; }
        targetSummary->setText(tr("Trial target: %1").arg(WinExtras::getForegroundWindowExePath()));
        trialTargetWindow = reinterpret_cast<quintptr>(GetForegroundWindow());
        countdown = 0; trialClock.restart(); elapsed = 0;
    }
    if (realTrial && trialTargetWindow != reinterpret_cast<quintptr>(GetForegroundWindow()))
    { stopTrial(); report(tr("Trial stopped because the target window changed.")); return; }
#endif
    while (trialIndex < trialEvents.size() && trialEvents.at(trialIndex).time <= elapsed)
    {
        const auto event = trialEvents.at(trialIndex++);
        if (realTrial)
        {
            JoyButtonSlot slot(event.code, event.alias, JoyButtonSlot::JoySlotInputAction(event.mode));
            sendevent(&slot, event.pressed);
            if (event.pressed) trialHeld.append(event);
            else for (int i = trialHeld.size() - 1; i >= 0; --i)
                if (trialHeld.at(i).code == event.code && trialHeld.at(i).mode == event.mode) trialHeld.removeAt(i);
        }
        report(tr("%1 · %2 s · %3 %4").arg(realTrial ? tr("Trial") : tr("Preview — no output"))
            .arg(event.time / 1000.0, 0, 'f', 2).arg(event.pressed ? tr("Press") : tr("Release"), event.label));
    }
    if (elapsed >= trialDuration && trialIndex == trialEvents.size())
    { stopTrial(); report(realTrial ? tr("One pass completed. All trial keys released.") : tr("Preview completed — no keys were sent.")); }
}

void ActionLibraryDialog::stopTrial()
{
    trialTimer.stop(); trialRunning = false;
    for (auto i = trialHeld.crbegin(); i != trialHeld.crend(); ++i)
    { JoyButtonSlot slot(i->code, i->alias, JoyButtonSlot::JoySlotInputAction(i->mode)); sendevent(&slot, false); }
    trialHeld.clear();
    if (stopButton) { setTrialUi(false); report(tr("Stopped. Trial keys released.")); }
}

void ActionLibraryDialog::closeEvent(QCloseEvent *event)
{
    stopTrial();
    if (stepEditor) { stepEditor->raise(); event->ignore(); return; }
    if (discardDraft()) { QDialog::done(QDialog::Accepted); event->accept(); } else event->ignore();
}

void ActionLibraryDialog::reject() { close(); }
