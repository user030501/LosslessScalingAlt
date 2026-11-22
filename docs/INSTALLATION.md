# Installation

Two distribution options, both built from the same source. Neither
requires a license key, account, or network access to run.

## Option A: Portable

No installation, no admin rights, no registry changes. Good for trying it
out or running from a USB drive.

1. Build it (see [`BUILD.md`](BUILD.md)), or obtain a pre-built portable
   folder.
2. If building yourself, produce the portable layout with:
   ```powershell
   cmake --preset windows-x64
   cmake --build --preset windows-x64-release
   cmake --install build/windows-x64 --prefix dist/portable --config Release
   ```
3. Copy the `dist/portable` folder anywhere you like.
4. Run `LSAlt.exe` from inside it.

The portable folder is self-contained: `LSAlt.exe`, a `shaders/` directory
(HLSL source compiled at runtime and cached next to itself), `README.md`,
`LICENSE`, `CHANGELOG.md`. It needs the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)
installed, same as most native Windows applications — this is almost
certainly already on your system if you've installed any other game or
application recently.

On first run it creates, under `%LOCALAPPDATA%\LSAlt\`:
- `config.json` — app-wide settings (plus a `.bak` backup)
- `profiles.json` — game profiles (seeded with 4 built-in samples)
- `history/sessions.json` — performance session history
- `logs/lsalt.log` — log file

Nothing is written outside that folder and the portable folder itself.

## Option B: Installer

1. Build the portable layout as in steps 1–3 above.
2. Compile `packaging/installer.iss` with
   [Inno Setup 6](https://jrsoftware.org/isinfo.php) (free):
   ```powershell
   & "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\installer.iss
   ```
3. Run the resulting `packaging/output/LSAlt-Setup.exe`.

The installer:
- Lets you choose the installation directory.
- Offers an optional desktop shortcut; always creates a Start Menu entry.
- Installs a proper uninstaller (Windows Settings → Apps, or Control
  Panel → Programs).
- **Never touches `%LOCALAPPDATA%\LSAlt`** on install, upgrade, or
  uninstall — your settings, profiles, and session history survive
  reinstalling or uninstalling the application itself.

## Verifying your installation

Either option can self-test from a terminal:

```powershell
LSAlt.exe --selftest
```

This runs the full automated diagnostic suite (27 scenarios covering
Frame Generation, GPU behavior, profiles, and Scaling) against your actual
hardware and exits with code `0` if everything passes. Results are logged
to `%LOCALAPPDATA%\LSAlt\logs\lsalt.log`.

## Uninstalling

- **Portable**: delete the folder. Optionally also delete
  `%LOCALAPPDATA%\LSAlt` to remove saved settings/profiles/history.
- **Installer**: use Windows Settings → Apps, or run the uninstaller from
  the Start Menu entry. This removes the installed files but — by
  design — leaves `%LOCALAPPDATA%\LSAlt` alone; delete it yourself if you
  want a completely clean removal.
