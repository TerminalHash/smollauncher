#ifndef IMPORTCONTROLLER_H
#define IMPORTCONTROLLER_H

class MainWindow;
class AppItem;

class ImportController
{
public:
    explicit ImportController(MainWindow *window);

    void fromSteam();
    void fromHeroicEpic();
    void fromHeroicGog();
    void fromItch();
    void fromRetroArch();
    void fromPrism();

private:
    void configureLaunchMode(AppItem *app);

    MainWindow *m_w;
};

#endif // IMPORTCONTROLLER_H
