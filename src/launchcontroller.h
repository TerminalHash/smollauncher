#ifndef LAUNCHCONTROLLER_H
#define LAUNCHCONTROLLER_H

class MainWindow;
class AppItem;

class LaunchController
{
public:
    explicit LaunchController(MainWindow *window);

    void launch(AppItem *app);

private:
    MainWindow *m_w;
};

#endif // LAUNCHCONTROLLER_H
