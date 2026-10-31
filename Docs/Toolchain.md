# Bundled tools

The launcher bundles Python (official embeddable distribution), CMake, and Ninja; users do not install them (D7, D8).
MSVC is not bundled (license); System check installs it.

- **Source:** `Toolchain.lock.json`: per-tool `version`, `url`, `sha256` (archive), `root` (folder to strip from the
  archive), `executable` (relative to tool folder). Binaries are not tracked in git.
- **Download:** `python Scripts/fetch_toolchain.py` → `ToolCache/<tool>/` + `ToolCache/Toolchain.json` (read by the
  launcher: version + executable path). Stops on hash mismatch; nothing unverified is extracted. Internet only here.
- **Upgrade:** version + address in the lock file, clear `sha256`, `python Scripts/fetch_toolchain.py --pin`; compare
  with the publisher's hash (CMake publishes `cmake-<v>-SHA-256.txt`), then commit.
- **Python:** the embeddable distribution's `python3XX._pth` stays unchanged: the interpreter is isolated (system Python
  registry entries and `PYTHONPATH` do not interfere). Engine tools accommodate this: they do not rely on the working
  folder and pass their own interpreter to CMake (engine `Docs/ToolsContract.md`).
- **Runtime:** `EngineCommand` prepends tool folders to the child process's `PATH`; engine tools find cmake / ninja /
  python there. `-X utf8` makes output UTF-8.
- **Validation:** System check runs each tool with `--version` and compares its version with `Toolchain.json`.

AudioGeneration's NumPy / SciPy dependencies are not included in bundled Python (D7); "install audio tools" is in Stage
4.