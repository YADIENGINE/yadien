# Engine versions

The launcher deals with engines in three places (engine `Docs/Design/README.md` D4, D16):

- **The engine it builds against:** the launcher's window uses the engine's `ToolUi`, compiled into the executable. A
  developer's checkout ([Development](Development.md)); a release uses the tag of the shipped engine. Not delivered to
  users.
- **The shipped engine:** `LauncherManifest.json` `engine`, one exact version. `dev.py` imports it from its tag into
  `<build>/Engines/Engine<version>/`, and the installer carries that folder, so the first project works offline. It is
  not in the repository.
- **Every other version** comes from GitHub on demand and stays in the download cache, `<data folder>/Engines/`, which
  a launcher update keeps.

## New stable version

Raise `version` in `EngineManifest.json` and add `ReleaseNotes/<version>.json` (rules in engine `Docs/ToolsContract.md` →
Manifest), and
usually `LauncherManifest.json` (`engine`, a new launcher `version` + note), then run `python Scripts/dev.py release`
([Packaging](Packaging.md#publish)). It commits, pushes and tags the engine, and publishes the tag's record.

Users of any launcher see the new version as soon as its record is published: an engine release needs no launcher
release. A launcher that does not know the version's `toolsContract` says it needs updating first.

Patch flow: fix an older series on a branch from that tag in the engine repo, tag the new patch; `dev.py
engine-releases` publishes its record. Versions live side by side, so nothing is replaced.

## Where a version comes from

A version is a tag's source archive plus a **GitHub release of that tag holding `EngineCopy.json`**: `{"format": 1,
"source": {"tag", "commit"}, "files": {"<path>": "sha256:<hex>"}}`, made from `git archive <tag>` (which excludes `.git`,
uncommitted changes and the engine's `.gitattributes` `export-ignore` files, D14). The launcher lists those releases
(`Engines/EngineCatalog.h`), downloads the archive, extracts it with Windows' `tar.exe` and requires every file to match
the record, with nothing missing or extra; only then does the folder enter the cache. GitHub keeps archive *contents*
stable but not their compressed bytes, which is why files are compared and not the archive.

`python Scripts/dev.py engine-releases [--dry-run]` publishes the record of every tag without one, and `dev.py release`
does the same. A tag without a record is not offered. With **immutable releases** on in the repository's settings, a
published tag and record cannot be changed, which is what makes the record worth checking against.

The list is read in the background at startup (if the update check is on) and when the Engines tab or an engine change
opens. Without internet, or within GitHub's limit of 60 anonymous requests per hour, only the versions on this computer
are offered; nothing else stops working.

`LAUNCHER_ENGINE_API` replaces the list's address, for tests.

## Changing a project's version

Project → **Update engine** or **Change engine version** (newer or older). Git-managed engines stay out: they follow
their branch.

1. **Review:** the chosen version, downloaded if needed; notes and breaking changes, files that would be lost.
2. **Trial** (`Projects/EngineTrial.h`): a temporary copy of the project, in the data folder, without its `Engine/`, build
   and release folders, gets the new engine. Its clean game build, editor build and tests run through the engine's own
   tools. **The project is not touched**, so a crash or a closed window has nothing to undo.
3. **Passed:** `ApplyEngineUpdate` swaps the engine and removes the project's build folders. The first build afterwards
   compiles from scratch. If the editor or game is open, the swap fails, the project stays as it was and the panel offers
   to try again without repeating the trial.
4. **Failed** (exit code 1): the errors and migration steps are shown; **Copy report** gives them to an AI. The project
   stays where it was. **Change anyway** moves it to fix it there. Without it a project could never cross a breaking
   version, since a breaking change is exactly what fails the build. It can go back by choosing the old version again.
5. **Not tested** (any other exit code, such as a missing compiler): says nothing about the project; try again or change
   anyway.

Why it is built this way, each from a real trial between 0.2.0 and 0.3.0:

- A build after swapping the engine under an existing build tree reports success in 0 seconds: the copy's files are older
  than the objects. So the trial builds clean, and a change removes the build folders.
- Restoring the old engine under a build tree the new one has touched fails. So the trial leaves the real tree alone.
- The game build and tests pass while the editor build fails (0.3 renamed the default worker target). So the editor is
  built too.

Going back warns separately: a game's saves written by the newer engine may not load, which no build can detect.

Repair (the same version over edited files) has nothing to try and swaps at once.

## Test it

`python Scripts/dev.py engine-test` (`Scripts/engine_test.py`, a few minutes; `dev.py release` runs it): a temporary
folder plays GitHub with real tags, and `Tests/EngineProbe.cpp` runs the launcher's real code. Downloads are checked and a
wrong record refused; a 0.2.0 project fails at the editor build on 0.3.0 and stays exactly as it was; changing anyway
moves it and removes its build folder; a 0.3.0 project passes. It touches nothing outside its folder.
