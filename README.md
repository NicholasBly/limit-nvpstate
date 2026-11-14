# limit-nvpstate

[![Downloads](https://img.shields.io/github/downloads/valleyofdoom/limit-nvpstate/total.svg)](https://github.com/valleyofdoom/limit-nvpstate/releases)

This program acts as a lightweight alternative to NVIDIA Inspector's "Multi-Display Power Saver" feature. The logic behind the program is to limit the GPU's P-State unless a process specified in the exceptions list is the foreground window (e.g. a video game), in which the P-State will be unlimited.

Place the program folder somewhere safe and enable "Add To Startup" in the File menu. Populate the process exclusion list with applications you do not wish P-States to be limited for while the application's window is in the foreground.

Note that closing the application by clicking "exit" in the tray menu will unlimit P-States before closing. If the program ends abruptly (ending the process in task manager), the unlimit action will not be executed when exiting.

Disclaimer: applications running in fullscreen mode (legacy flip), may not be detected when in the foreground. If this occurs, switch the unlimit trigger to running processes instead.

## Per-program "When behind" P-State

Each program in the exceptions list has a **When behind** dropdown. It sets the P-State to use while that program is open but not in front (alt-tabbed, minimized, behind another window). In front, a listed program is always unlimited.

Example: add `FortniteClient-Win64-Shipping.exe` (the game itself, not the Epic launcher) and set **When behind** to P3, with **Limit P-State** at P8:

- Fortnite in front: unlimited (P0)
- Fortnite open but behind: P3
- Fortnite closed: P8

**Default** keeps the old behaviour: with the "Process Running" trigger a running program is unlimited, with "Process Foreground" it only counts while in front. If several listed programs count at once, the fastest (lowest) P-State wins. Any row that isn't Default makes the program watch both running processes and the foreground window, whichever trigger is selected. The same fullscreen caveat applies: a game the foreground check can't see counts as behind.
