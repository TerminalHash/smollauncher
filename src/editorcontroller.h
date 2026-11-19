#ifndef EDITORCONTROLLER_H
#define EDITORCONTROLLER_H

#include <QString>

class MainWindow;
class QStandardItem;

class EditorController
{
public:
    enum class Field {
        Name,
        Arguments,
        Icon,
        WorkDir,
        Binary,
        Terminal,
        Debug,
        Single,
        Environment,
        CategoryName,
        CategoryIcon,
    };

    explicit EditorController(MainWindow *window);

    void applyMode(int type);
    void loadItem(QStandardItem *item);
    void clear();
    void save(const QString &reason = QString(), bool markDirty = false);

    // Write one edited field back to the current item. Unlike save(), this
    // never copies the other editor widgets over the item, so a stale or
    // empty unrelated field cannot wipe a path.
    void saveField(Field field);

    void chooseBinary();
    void chooseWorkDir();
    void chooseIcon();
    void chooseCategoryIcon();
    void addEnvVar();
    void removeEnvVar();

private:
    void updateScriptState();

    MainWindow *m_w;
};

#endif // EDITORCONTROLLER_H
