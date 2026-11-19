#ifndef CONTEXTMENUCONTROLLER_H
#define CONTEXTMENUCONTROLLER_H

#include <QPoint>

class MainWindow;
class AppItem;

class ContextMenuController
{
public:
    explicit ContextMenuController(MainWindow *window);

    void showMenu(const QPoint &pos);

private:
    void copyCommandLine(AppItem *app);
    void showBinaryInFileManager(AppItem *app);
    AppItem *duplicateAppItem(AppItem *src);

    MainWindow *m_w;
};

#endif // CONTEXTMENUCONTROLLER_H
