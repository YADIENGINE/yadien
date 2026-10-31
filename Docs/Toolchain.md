# Bundled tools

The launcher bundles Python (official embeddable distribution), CMake, and Ninja; users do not install them (D7, D8).
MSVC is not bundled (license); System check installs it.

- **Source:** `Toolchain.lock.json`: per-tool `version`, `url`, `sha256` (archive), `root` (folder to strip from the
  archive), `executable` (relative to tool folder). Binaries are not tracked in git.
- **Libraries:** the lock's `libraries` section lists what the launcher links but does not ship as a tool, same fields
  with `check` (a file that must exist) instead of `executable`. Today: Velopack's C library (`velopack_libc`, the
  installer's hooks, D15); `<engine>.exe` links its DLL and the build copies the DLL beside it.
- **Download:** `python Scripts/fetch_toolchain.py` → `ToolCache/<tool>/` + `ToolCache/Toolchain.json` (read by the
  launcher: version + executable path; tools only). Stops on hash mismatch; nothing unverified is extracted. Internet
  only here. Building the launcher needs it too (the Velopack library).
- **Upgrade:** version + address in the lock file, clear `sha256`, `python Scripts/fetch_toolchain.py --pin`; compare
  with the publisher's hash (CMake publishes `cmake-<v>-SHA-256.txt`), then commit.
- **Python:** the embeddable distribution's `python3XX._pth` stays unchanged: the interpreter is isolated (system Python
  registry entries and `PYTHONPATH` do not interfere). Engine tools accommodate this: they do not rely on the working
  folder and pass their own interpreter to CMake (engine `Docs/ToolsContract.md`).
- **Runtime:** `EngineCommand` prepends tool folders to the child process's `PATH`; engine tools find cmake / ninja /
  python there. `-X utf8` makes output UTF-8.
- **Validation:** System check runs each tool with `--version` and compares its version with `Toolchain.json`.

**Audio tools (D7):** AudioGeneration's NumPy / SciPy are not in the package. The lock's `audio` section pins their
wheels (version, address, SHA-256); `fetch_toolchain.py` does not download them, it copies the section into
`Toolchain.json`. The launcher downloads them only when the user asks (Settings → Audio tools, or
`<engine> launcher audio-tools install`), checks each hash, extracts the wheels with Windows' `tar.exe` into
`<data>/AudioTools/site/` (beside `Settings.json`, so updates keep them) and records the versions in
`AudioTools.json`. `<engine> launcher python SCRIPT [ARGS]` runs a script with the bundled Python and that folder on
`sys.path` (the bundled Python is isolated, so PYTHONPATH would not work). Upgrading: new wheel address + `sha256`
(PyPI publishes it) in the lock; the launcher then shows the installed tools as outdated.