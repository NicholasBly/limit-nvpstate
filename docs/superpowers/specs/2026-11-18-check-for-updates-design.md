# Check for updates: design

## Why

Update limit-nvpstate from inside the app instead of downloading and unzipping
a release by hand.

## Decided with the user

- Source: GitHub releases of **NicholasBly/limit-nvpstate** (a constant).
- **Button only** (Help → Check for updates…). No automatic checks, no
  network traffic unless asked.
- One click installs: download, verify, swap files, restart.

## Behaviour

1. Help → Check for updates… asks the GitHub REST API for the latest
   (non-pre-release) release, in the background with a progress dialog.
2. Up to date → "You have the latest version (X)". Error → the reason plus a
   link to the releases page.
3. Newer → "Version Y is available (you have X)", release-notes link,
   [Update now] [Later].
4. Update now:
   - download `limit-nvpstate-Y.zip` and `limit-nvpstate-Y.zip.sha256` over
     HTTPS; refuse unless the SHA-256 matches. A release without a checksum
     file (5.2.3 and older) gets "open the release page" instead;
   - extract to a fresh temp folder with Windows' built-in `tar.exe`
     (Windows 10 1803+); the package folder is the one holding
     `limit-nvpstate.exe`;
   - stop detectors and release the GPU limit;
   - for each file in the package (never `config.json` if one exists): rename
     the current file to `*.old` (allowed even while it is running), move the
     new one in; any failure rolls every rename back;
   - start the new exe with `--after-update <pid>` and exit. The new instance
     waits for the old process to end before taking the single-instance
     mutex, then deletes the `*.old` files;
   - if the new exe can't be started, undo the install (new files out,
     `*.old` back), re-apply the limit and keep running.
5. Cancel during the download stops it and deletes the temp folder. The temp
   folder is deleted after every outcome, retrying for about 3 s when
   something (tar's exit, a virus scanner) still holds it open.

## Version

One number, compiled in: `APP_VERSION` from the MSBuild property
`AppVersion` (default in the project, overridden by `build.ps1 -Version`,
which the release workflow passes). The About box shows it. Versions compare
numerically by dotted parts ("5.2.10" > "5.2.9", leading "v" ignored).

## Release workflow

Validates the version input (digits and dots), builds with it, publishes
`limit-nvpstate-<ver>.zip` plus `limit-nvpstate-<ver>.zip.sha256`
("<hex>  <file name>").

## Approaches considered

- **Open the download page only:** no file handling, but not what was asked
  ("update right from limit-nvpstate itself").
- **Qt Network + private QZipReader:** brings back Qt5Network.dll and OpenSSL
  DLLs (pruned today) and a private Qt API.
- **WinHTTP + BCrypt + tar.exe (chosen):** all built into Windows; no new
  DLLs to ship.

## Honest limits

- The checksum catches a broken or truncated download. It cannot catch a
  compromised GitHub account, since both files come from the same release.
- Needs `tar.exe` (Windows 10 1803 or newer); otherwise it says so and links
  the release page.
- An app running elevated replaces files in its own folder: keep it in a
  folder ordinary users cannot write to.
- If the new version starts but then crashes on its own, the old one has
  already exited and can't undo anything: reinstall by hand.

## Builder's calls

- Pure, unit-tested rules (version compare, asset choice, checksum file
  parsing) in `updaterules.{h,cpp}`; Windows work in `updater.{h,cpp}`.
- Size caps: API reply 1 MiB, zip 200 MiB. Timeouts 15 s connect/response.
- Test builds may point the API at a local server (compile-time define);
  release builds only accept `https://` URLs.
