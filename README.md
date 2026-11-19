# SmolLauncher
Simple and not overloaded application manager without any extra dependencies.

Features:
- Launcher is not locked to one app list, you can do a multiple lists and change them via menu;
- Easy and convenient settings for application - settings panel has only important parameters;
- Simple design without hidden settings;
- Supports importing applications/games from next sources: Steam, Heroic Launcher/Legendary, Itch, RetroArch and system menu.

Written with help of DeepSeek 4.1 Flash LLM.

Building instructions in BUILDINGS.md.

## Environment Variables
- `SL_CONFIG_DIR` - if set, saves all files (configuration, lists, logs) in the selected directory, also allows to use launcher in portable mode (by default, saving in `$XDG_HOME_CONFIG`);
- `SL_DEBUG` - if set, it enabling additional debug logging.

## About Windows/MacOS
I don't have devices with MacOS or Windows, so i can't test my launcher under these systems and they can have specific problems or regressions on them. You can help by testing launcher on these systems and by writting into Issues. All tickets, which done with AI help, would be ignored, write them manually, please.

## FAQ
1. What is launcher for, if Lutris/Bottles/OVER9000 other launchers exists, that allow for the same thing, but with greater flexibility?

In short, i need a very simple application manager, which won’t offer more than it should, and won’t meddle in the application startup process.

2. Why that launcher don't have support for search Steam, Heroic, etc. from Flatpak/Snap?

I'm not using Flatpak and Snap and don't see the point to make checks for them.
