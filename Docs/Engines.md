# Engine versions

The launcher uses engines in three distinct ways:

- **Build engine:** the checkout whose `ToolUi` is compiled into the launcher. It is selected only for development and
  is not shipped as that checkout.
- **Shipped engine:** the exact `LauncherManifest.json` `engine` version included in the installer so the first project
  works offline.
- **Downloaded engines:** all other stable versions, fetched on demand and cached in the persistent data folder.

## Project types and modes

Files determine project type; launcher mode determines which operations are allowed.

| Type | Detection | Integrity check | Engine change |
| --- | --- | --- | --- |
| `Copy` | `Engine/EngineCopy.json` | Yes | Any published stable version in both modes |
| `Git` | `Engine/.git` | No | Git operations in developer mode only |
| `Unregistered` | Neither marker | Warning only | Replace with a stable version |
| `Missing` | No usable `Engine/` | — | Replace when `Project.json` exists |

Build, run, and editor commands work for every type. User mode never runs git. Developer mode is an unprotected Settings
toggle with a warning; it adds `dev (main)` projects, engine update/publish actions for git projects, and git checks to
System check. Repository permissions protect publishing.

## Published versions

A stable version is a tag source archive plus a GitHub release asset named `EngineCopy.json`. The record contains the tag,
commit, and SHA-256 of every exported file. `EngineCatalog` lists only releases with that record; downloads are extracted
to a temporary location and accepted only when no file is modified, missing, or extra.

The catalog refreshes in the background when update checks are enabled and when an engine list or change is opened.
Offline or rate-limited launchers continue to offer local versions. `LAUNCHER_ENGINE_API` overrides the catalog address
for tests.

To publish a new stable version, update the engine manifest and release note, then follow
[Packaging → Publish](Packaging.md#publish). Patch releases from older series may be tagged on their own branch;
`python Scripts/dev.py engine-releases` publishes records missing from existing tags. Published engine releases should
be immutable.

## Prebuilt engines

An engine release may also contain `EnginePrebuilt.zip`, built from the same tag. The launcher verifies the asset digest
and exposes the extracted folder through `ENGINE_PREBUILT`; engine tools decide whether it is compatible. The shipped
version's prebuilt package is installed with the launcher, while other versions are cached with their engines.

A missing or failed prebuilt download is not fatal: the project builds the engine from source. Removing a downloaded
engine also removes its prebuilt package.

## Changing a project's version

Stable engine changes use this workflow:

1. **Review:** download the target if needed and show release notes, breaking changes, and files that would be lost.
2. **Trial:** copy the project into the data folder without its engine or build output, install the target engine, and run
   clean game, editor, and test builds through that engine's tools.
3. **Apply:** after a passing trial, atomically replace the real engine and remove stale build output. If files are open,
   the swap fails without changing the project and can be retried without repeating the trial.
4. **Failure:** leave the real project untouched and show errors and migration steps. The user may copy the report, retry,
   or explicitly change anyway to perform required migrations in the real project.

Non-build failures such as a missing compiler are reported as “not tested,” not as project incompatibility. Selecting an
older version additionally warns that newer save data may be incompatible. Repair reapplies the current version without
a trial. Git-managed engines always follow their branch instead of this workflow.

## Validation

```powershell
python Scripts/dev.py engine-test
```

The test uses temporary catalogs and projects to exercise verified downloads, rejected records, passing and failing
trials, forced changes, and build-folder cleanup. It touches nothing outside its temporary folder and is also run by the
release workflow.
