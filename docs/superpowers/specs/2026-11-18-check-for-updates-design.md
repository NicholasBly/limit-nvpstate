# Check for updates: design

## Why

Update limit-nvpstate from inside the app instead of downloading and unzipping
a release by hand.

## Decided with the user

- Source: GitHub releases of **NicholasBly/limit-nvpstate** (a constant).
- **Button only** (Help → Check for updates…). No automatic checks, no
  network traffic unless asked.
- One click installs: download, verify, swap files, restart.
- After the second review (bac0835): keep one-click updates, but harden them
  (R2–R10 and the small items) rather than ship check-only.

## Behaviour

1. Help → Check for updates… asks the GitHub REST API for the latest
   (non-pre-release) release, in the background with a progress dialog.
   The tag must be a plain release version (1–4 parts of 1–9 digits).
2. Up to date → "You have the latest version (X)". Error → the reason plus a
   link to the releases page. All updater messages are plain text.
3. Newer → "Version Y is available (you have X)", release-notes link,
   [Update now] [Later]. A renamed exe is not offered Update now.
4. Update now (Cancel is honoured between every step, and checked again
   before anything is installed): download exactly `limit-nvpstate-Y.zip` and
   its `.sha256` over HTTPS into `limit-nvpstate-update\` (inside the program
   folder, not %TEMP%), refuse a checksum mismatch, refuse the zip unless its
   own file list passes the package rules, unpack with Windows' `tar.exe`,
   check the result matches that list.
5. **Installing is the helper's job** — see
   `2026-11-18-updater-helper-design.md` (supersedes the in-app install of the
   second review round): the app releases the GPU, starts
   `limit-nvpstate-updater.exe` and exits; the helper swaps the files while
   nothing is running, keeps the new version only if it says it is up and is
   still running 3 s later, and otherwise restores and restarts the previous
   one. The program shows the outcome and retires the work folder.

## Version

One number, compiled in: `APP_VERSION` from the MSBuild property
`AppVersion` (default in the project, overridden by `build.ps1 -Version`,
which the release workflow passes). The About box shows it. Versions compare
numerically by dotted parts ("5.2.10" > "5.2.9", leading "v" ignored).

## Release workflow

Validates the version input (`^[0-9]{1,9}(\.[0-9]{1,9}){0,3}$`, the same rule
the app applies to tags), builds with it, publishes `limit-nvpstate-<ver>.zip`
plus `limit-nvpstate-<ver>.zip.sha256` ("<hex>  <file name>", one line).

## Approaches considered

- **Open the download page only:** no file handling, but not what was asked
  ("update right from limit-nvpstate itself").
- **Qt Network + private QZipReader:** brings back Qt5Network.dll and OpenSSL
  DLLs (pruned today) and a private Qt API.
- **WinHTTP + BCrypt + tar.exe (chosen):** all built into Windows; no new
  DLLs to ship.
- **Versioned install folders + a separate launcher (review suggestion):**
  atomic switch, but a second program to ship and maintain and a changed
  install layout. The journal + handshake cover the same failures (crash
  mid-install, broken new version, failed undo) in place.

## Honest limits

- The checksum catches a broken or truncated download. It cannot catch a
  compromised GitHub account or release workflow, since both files come from
  the same release. Signing (a code-signing certificate, or a key kept off
  GitHub) would; not done.
- If the program folder is writable by normal programs (e.g. in Downloads),
  so is the work folder — and the exe. Keep it in a protected folder if that
  matters.
- If the new version passes the handshake and then crashes, or both copies
  die before the handshake and the new exe can't even start, nothing is left
  running to undo it; the backups stay in `limit-nvpstate-update\backup`.
- A stalled download is bounded by WinHTTP's per-step timeouts (15/30 s), not
  an overall deadline; Cancel takes effect after the current chunk.
- Needs `tar.exe` (Windows 10 1803 or newer); otherwise it says so and links
  the release page.

## Builder's calls

- Pure, unit-tested rules (versions, release tags, asset choice, checksum
  file, safe paths, journal format, package checks) in
  `updaterules.{h,cpp}`; Windows work in `updater.{h,cpp}`.
- Size caps: API reply 1 MiB, zip 200 MiB, unpacked 500 MiB / 1000 files.
- Test builds (the Wine rig) may point the API at a local server and use the
  pauses/fault switches in `testhooks.h`; release builds only accept
  `https://` URLs and compile the hooks to nothing.
