# limit-nvpstate

[![Downloads](https://img.shields.io/github/downloads/valleyofdoom/limit-nvpstate/total.svg)](https://github.com/valleyofdoom/limit-nvpstate/releases)

This program acts as a lightweight alternative to NVIDIA Inspector's "Multi-Display Power Saver" feature. The logic behind the program is to limit the GPU's P-State unless a process specified in the exceptions list is the foreground window (e.g. a video game), in which the P-State will be unlimited.

Place the program folder somewhere safe and enable "Add To Startup" in the File menu. Populate the process exclusion list with applications you do not wish P-States to be limited for while the application's window is in the foreground.

Note that closing the application by clicking "exit" in the tray menu will unlimit P-States before closing. If the program ends abruptly (ending the process in task manager), the unlimit action will not be executed when exiting.

Disclaimer: applications running in fullscreen mode (legacy flip), may not be detected when in the foreground. If this occurs, switch the unlimit trigger to running processes instead.

## Per-app P-State

Each program in the exceptions list has a **Per-app** dropdown. It sets the P-State to use while that program is open but not in front (alt-tabbed, minimized, behind another window). In front, a listed program is always unlimited.

Example: add `FortniteClient-Win64-Shipping.exe` (the game itself, not the Epic launcher) and set **Per-app** to P3, with **Limit P-State** at P8:

- Fortnite in front: unlimited (P0)
- Fortnite open but behind: P3
- Fortnite closed: P8

**Default** keeps the old behaviour: with the "Process Running" trigger a running program is unlimited, with "Process Foreground" it only counts while in front. If several listed programs count at once, the fastest (lowest) P-State wins. Any row that isn't Default makes the program watch both running processes and the foreground window, whichever trigger is selected. The same fullscreen caveat applies: a game the foreground check can't see counts as behind.

## Updates

**Help → Check for Updates...** looks for a newer release on this project's GitHub releases page. It only checks when you click it. If there is one, **Update now** downloads it, installs it and restarts the program.

How the update is installed:

- A small separate program, `limit-nvpstate-updater.exe`, swaps the files once limit-nvpstate has closed. It backs up every file it replaces first.
- The new version then has to start and keep running for a few seconds. Its settings are greyed out until that check is done. If it doesn't pass, the update is undone and the previous version starts again, with a message saying why. Your settings are put back too.
- If the computer crashes or loses power in the middle, the next start of limit-nvpstate finishes undoing the update. If that can't be finished either, limit-nvpstate shows what to do and doesn't run until it's fixed. You can also run `limit-nvpstate-updater.exe` in the program folder yourself to undo an unfinished update.

**One Windows session at a time:** if limit-nvpstate is also running in another user session on this PC (Fast User Switching, Remote Desktop), close it there before you update.

**Test builds from before the updater:** early 5.3.0 development builds that installed updates by themselves can't update to a release. Download it and unzip it over the program folder instead.

**What the update trusts:** the download comes over HTTPS from this project's GitHub releases, and its SHA-256 checksum file comes from the same release. The checksum catches a broken or incomplete download. It does **not** prove who made the release: anyone who could publish a release on this GitHub account could also publish a harmful update, and it would install with administrator rights, as limit-nvpstate itself runs with them. Releases are not code-signed. If that matters to you, download releases yourself from the releases page and check them before you run them, instead of using **Update now**.
