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
3. **Packs** with `vpk pack` into `build/Package/Releases/`, with the build's icon and splash (`build/Brand/`): the
   installer plus the update files (`*-full.nupkg`, `*-delta.nupkg` when an earlier release is in the folder,
   `releases.win.json`, `RELEASES`, `assets.win.json`).

Packing the same version again (a test build) makes vpk ask whether to overwrite the earlier one in
`build/Package/Releases/`; answer `y`, or pass `dev.py package --yes`. A real release always raises the version, so the
question guards published versions. `--notes <file>` puts Markdown release notes in the package (see Publish).

The version is `version` in `LauncherManifest.json` (CMake reads it into `LAUNCHER_VERSION`); the Velopack id and title
are the product name from the engine manifest. A release that ships a new engine, launcher change or tool upgrade raises
the launcher's version (D15).

**Icon and splash** are drawn from code (engine D1) by `Scripts/brand.py` when the build is configured: the product
name's first letter in a 5x7 arcade pixel font on a dark tile (icon, 16-256 px, also the window and taskbar icon via the
`GLFW_ICON` resource), and the whole name on the splash; colours are the tool UI's. Nothing generated is committed; a
renamed product gets new images on the next configure.

When a license text changes (a tool or library upgrade), update `Packaging/Notices.json` or `Packaging/Licenses/` in
the same commit. Velopack's and Ninja's archives have no license file; their texts are in `Packaging/Licenses/`.

## Try it

Run the installer, start the launcher from the desktop shortcut, check Settings (PATH on, notices open) and
`<engine> launcher version` in a terminal (restart the terminal's app first: tabs inherit the PATH it started with),
then
uninstall from Windows Settings → Apps. Velopack closes every program running from the install folder when a
version is installed over it (D15): close the launcher and let builds finish first.

## Publish

Edit the manifests, then run one command:

```powershell
# EngineManifest.json:   version + notes (rules: engine Docs/ToolsContract.md -> Manifest)
# LauncherManifest.json: version + notes + engines
python Scripts/dev.py release --dry-run   # the plan and every check; changes nothing
python Scripts/dev.py release
```

```json
{
  "version": "0.3.0",
  "engines": ["0.4.0", "0.3.1"],
  "notes": [ { "version": "0.3.0", "date": "2026-11-02", "changes": ["..."] } ]
}
```

`engines` lists the exact engine versions the installer carries, one per series: a listed version is imported, a series
that is not listed is removed. `Scripts/Tests/test_release.py` checks that it matches `Engines/`.

The manifests are the desired state. An engine version without a pushed tag and a launcher version without a published
GitHub release get released; the rest is left alone. If a run stops, fix the cause and run the same command again.

1. **Checks.** Manifest rules, branch `main`, not behind origin, tools (`gh`, `vpk` at the locked version, `ToolCache/`).
   Changes outside the manifests stop the run, as do unpushed engine commits: a release never commits someone else's work.
2. **Engine.** Its tests (`python . test` in the checkout), commit the manifest, push, annotated tag, push the tag.
3. **Launcher.** Import or remove engines, move the Engine pointer up to the newest carried engine when it is behind
   (a pointer past that tag stays; `dev.py pin-engine` moves it further), commit locally.
4. **Installer.** Tests, `dev.py package` in `build/ReleaseBuild/`, against a worktree of the pinned engine commit in
   `build/ReleaseEngine/`, so the shared engine checkout and the development build tree do not matter. If the previous
   release's full package is gone from `build/Package/Releases/`, `vpk download github` brings it back for the delta.
5. **Your try.** The script prints the installer's path and asks before publishing (see Try it). Answering no keeps
   the local commits; the next run reuses the installer while the commits and the notes are the same.
6. **Publish.** Push, annotated tag, then a GitHub release: a draft until every file is up, pre-release for 0.x. It holds
   this version's installer and packages and the update feed (`releases.win.json`, `RELEASES`, `assets.win.json`).
7. **Project.** When the launcher is a submodule of a project, the released `Engine` / `Launcher` pointers are committed
   there, and nothing else. They are not pushed: that branch can hold other work.

An engine-only release stops after step 2 and goes on to step 7.

**Release notes** are generated into `build/Package/ReleaseNotes.md`: a preview line for 0.x, the launcher's newest note,
the engines it carries, and the engine notes since the previous launcher release (breaking changes first). The same file
goes to `vpk pack --releaseNotes` and to the GitHub release, so the notes also travel in the package and in
`releases.win.json`, where in-app update notes read them.

Releases are unsigned until signing is set up (D19), so Windows SmartScreen asks users to confirm.
