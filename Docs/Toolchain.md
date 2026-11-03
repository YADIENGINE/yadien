# Bundled tools

The launcher bundles Python's official embeddable distribution, CMake, and Ninja. MSVC is not bundled; System check can
install it separately.

`Toolchain.lock.json` is the source of truth for bundled tools and linked libraries. Each entry pins its version, archive
address, SHA-256, archive root, and executable or check file. Binaries are downloaded into `ToolCache/` and are not
tracked in git.

```powershell
python Scripts/fetch_toolchain.py
```

The script verifies every archive before extraction and writes the runtime `Toolchain.json`. To upgrade an entry, change
its version and address, clear its hash, run with `--pin`, compare the result with the publisher's hash, and commit the
new pin. Velopack's library entry must match the `vpk` packaging tool version.

The embedded Python remains isolated through its unchanged `python3XX._pth`; system registry entries and `PYTHONPATH`
cannot affect it. `EngineCommand` prepends bundled tool folders to the child PATH and uses `-X utf8`. System check runs
each tool's version command and compares it with `Toolchain.json`.

## Optional audio tools

The lock file's `audio` section pins optional NumPy and SciPy wheels. They are copied into `Toolchain.json` but downloaded
only when requested through Settings or `launcher audio-tools install`. The launcher verifies their hashes, extracts them
under `<data>/AudioTools/site/`, and records installed versions in `AudioTools.json`. This persistent location survives
launcher updates. `launcher python` adds the script directory and installed audio packages to `sys.path`.
