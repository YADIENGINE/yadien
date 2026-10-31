# Packaging and releases

The launcher ships as one installer, `<Name>-Setup-<version>.exe`, built with [Velopack](https://velopack.io)
(engine `Docs/Design/README.md` D15). Releases are made by hand for now; CI/CD and code signing are deferred (D19).

## Requirements

- Everything in [Development](Development.md), with `python Scripts/fetch_toolchain.py` run (tools + Velopack library).
- .NET SDK 8 or newer and Velopack's packaging tool, same version as the library in `Toolchain.lock.json`
  (`libraries.velopack`): `dotnet tool install -g vpk --version 1.2.161`.

## Build the installer

```powershell
python Scripts/dev.py package
```

It builds the launcher in Release, then `Scripts/package.py`:

1. **Stages** `build/Package/Stage/` (installed as `current/`): `<engine>.exe`, `<engine>.com`, the Velopack DLL,
   `Engines/`, and `Tools/` from `ToolCache/` without the lock's `exclude` entries (CMake's HTML docs, man pages,
   `cmake-gui.exe`, `cpack.exe`; license files stay).
2. **Writes `ThirdPartyNotices.txt`**: part 1 is the engine (its own license and the components every engine
   program contains, from the engine's `ThirdParty/Notices.json`), part 2 the launcher and its tools
   (`Packaging/Notices.json`; tool versions come from the lock). Settings → Open source licenses opens this file.
3. **Packs** with `vpk pack` into `build/Package/Releases/`: the installer plus the update files (`*-full.nupkg`,
   `*-delta.nupkg` when an earlier release is in the folder, `releases.win.json`, `RELEASES`, `assets.win.json`).

Packing the same version again (a test build) makes vpk ask whether to overwrite the earlier one in
`build/Package/Releases/`; answer `y`. A real release always raises the version, so the question guards published
versions. The version is `LAUNCHER_VERSION` in `CMakeLists.txt`; the Velopack id and title are the product name from the engine
manifest. A release that ships a new engine, launcher change or tool upgrade raises the launcher's version (D15).

When a license text changes (a tool or library upgrade), update `Packaging/Notices.json` or `Packaging/Licenses/` in
the same commit. Velopack's and Ninja's archives have no license file; their texts are in `Packaging/Licenses/`.

## Try it

Run the installer, start the launcher from the desktop shortcut, check Settings (PATH on, notices open), then
uninstall from Windows Settings → Apps. Velopack closes every program running from the install folder when a
version is installed over it (D15): close the launcher and let builds finish first.

## Publish

Keep `build/Package/Releases/` from the previous release so the next one gets a delta package. Then:

```powershell
git tag v<version>; git push origin v<version>
gh release create v<version> build/Package/Releases/<Name>-Setup-<version>.exe build/Package/Releases/*.nupkg `
  build/Package/Releases/releases.win.json build/Package/Releases/RELEASES build/Package/Releases/assets.win.json `
  --title "<Name> <version>" --notes "..."
```

The release notes list the launcher's own changes and the engines it carries ("contains: engine 0.1.1, 0.2.0");
engine changes are in each engine's manifest notes. Releases are unsigned until signing is set up (D19), so Windows
SmartScreen asks users to confirm.
