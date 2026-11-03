# Packaging and releases

The launcher ships as `<Name>-Setup.exe` through [Velopack](https://velopack.io). Releases are currently manual and
unsigned; CI/CD and code signing remain deferred.

## Requirements

Complete the [development setup](Development.md), fetch the pinned toolchain, and install .NET SDK 8 or newer plus the
`vpk` version locked as `libraries.velopack` in `Toolchain.lock.json`:

```powershell
dotnet tool install -g vpk --version <locked-version>
```

## Build the installer

```powershell
python Scripts/dev.py package
```

The command builds Release and then:

1. Stages the UI and CLI executables, Velopack DLL, the selected engine and prebuilt package, and bundled tools under
   `build/Package/Stage/`.
2. Generates `ThirdPartyNotices.txt` from engine and launcher notice manifests.
3. Runs `vpk pack` into `build/Package/Releases/`, producing the installer, full package, optional delta, and update feed.

The shipped engine is defined by `LauncherManifest.json`; its acquisition and integrity rules are in
[Engine versions](Engines.md). Tool exclusions and versions come from `Toolchain.lock.json`. Generated icon and splash
assets live under `build/Brand/` and are not committed.

Packing the same version again asks before replacing local output; pass `--yes` for a non-interactive test package. A
real release always increments the launcher version. `--notes <file>` embeds Markdown release notes.

When a dependency license changes, update `Packaging/Notices.json` or `Packaging/Licenses/` in the same commit.

## Installer smoke test

Install the package, launch it from the desktop shortcut, verify PATH and open-source notices in Settings, run
`<engine> launcher version` from a newly started terminal, and uninstall it through Windows Settings. Close programs
running from the installation folder before installing over an existing version.

## Test updates

```powershell
python Scripts/dev.py update-test
python Scripts/dev.py update-test --keep
```

The test installs two temporary versions under `build/UpdateTest/` and exercises the real update path against a local
feed. It checks offline errors, delta and note delivery, restart into the new version, persistent data, and PATH cleanup
without touching the real installation. `--keep` stops after installation for manual UI inspection. The release workflow
runs this test automatically.

## Publish

Update the engine manifest and release note when publishing an engine, and update `LauncherManifest.json` when publishing
the launcher or changing its shipped engine. Preview or execute the desired state with:

```powershell
python Scripts/dev.py release --dry-run
python Scripts/dev.py release
```

The workflow is resumable: after fixing a failure, run the same command again. It performs only missing steps:

1. Validate manifests, branch state, tools, notes, and pushed engine commits.
2. Test, commit, tag, push, and publish engine versions that lack a release record or prebuilt package.
3. Commit the launcher manifest, then run launcher tests, update tests, engine migration tests, and packaging in isolated
   release directories against the shipped engine tag.
4. Pause for the installer's manual smoke test before publishing.
5. Push and tag the launcher, upload the installer, packages, and update feed to a draft GitHub release, then publish it
   as the latest release.
6. When this repository is a game submodule, commit only the released engine and launcher pointers in the game; do not
   push that repository.

An engine-only release stops after publishing the engine and updating applicable game pointers. Existing release output
is reused when its commits and notes have not changed; completed releases are never overwritten.

Launcher release notes come from the newest `LauncherManifest.json` note and are used unchanged for the package, GitHub
release, and in-app update feed. Engine-specific notes remain in the engine release and appear in engine views. Until
signing is configured, Windows SmartScreen may require confirmation.
