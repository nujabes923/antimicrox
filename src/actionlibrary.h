#ifndef ACTIONLIBRARY_H
#define ACTIONLIBRARY_H

#include <QByteArray>
#include <QList>
#include <QString>

class JoyButton;
class JoyButtonSlot;

struct LibraryAction
{
    QString id, name, tags, description;
    bool favorite = false;
    int revision = 0;
    QByteArray assignments;
};

struct ActionPreviewEvent
{
    int time = 0, code = 0, alias = 0, mode = 0;
    bool pressed = false;
    QString label;
};

class ActionLibrary
{
public:
    explicit ActionLibrary(const QString &directory);
    QString directory() const { return root; }
    QList<LibraryAction> list(QString *error = nullptr) const;
    bool save(LibraryAction &action, QString *error);
    static bool read(const QString &path, LibraryAction &action, QString *error);
    static bool exportAction(const QString &path, const LibraryAction &action, QString *error);
    static QByteArray snapshot(JoyButton *button);
    static bool apply(JoyButton *button, const QByteArray &xml, bool append, QString *error);
    static QList<QByteArray> steps(const QByteArray &xml, QString *error = nullptr);
    static QByteArray combine(const QList<QByteArray> &steps);
    static QByteArray example();
    static bool preview(const QByteArray &xml, QList<ActionPreviewEvent> &events, int &duration, QString *error,
                        bool sampleRandom = true);
private:
    QString root;
};
#endif
