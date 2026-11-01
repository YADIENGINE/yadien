# Engine versions

The launcher carries two engines (engine `Docs/Design/README.md` D16):

- `Engine/`: submodule of the engine repo (`main`). Active development; the launcher builds against this. Not delivered
  to users. Changes take two steps: commit + push on `main` inside `Engine/`, then commit the pointer here.
- `Engines/Engine<MAJOR.MINOR>/`: stable engine copies. These go into the installer package and are copied to projects
  in user mode. A series folder holds that series' latest patch; the repo keeps 2–3 series. Never edit them manually.

## New stable version

Edit `EngineManifest.json` (`version` + `notes` at the top; rules in engine `Docs/ToolsContract.md` → Manifest) and
`LauncherManifest.json` (`engines`, usually a new launcher `version` + note), then run `python Scripts/dev.py release`
([Packaging](Packaging.md#publish)). It commits, pushes and tags the engine, then runs `Scripts/import_engine.py` and
removes series `engines` no longer lists. `import_engine.py` remains available by hand; `engines` must match afterwards.

Patch flow: fix an older series on a branch from that tag in the engine repo, tag the new patch, and import it; the
series folder is replaced with the new version.

## What import does

- Checks that the tag exists and points to the same commit on origin (every engine copy is traceable).
- Gets content with `git archive <tag>`: excludes `.git`, uncommitted changes, and engine `.gitattributes`
  `export-ignore` files (engine `CLAUDE.md`, `Docs/Design/`) (D14). The engine knows what to exclude.
- Manifest version must match the tag (`v0.2.0` ↔ `0.2.0`), and the newest note must be for that version.
- Writes `EngineCopy.json` beside it: `{"format": 1, "source": {"tag", "commit"}, "files": {"<path>": "sha256:<hex>"}}`.
  The launcher uses it to check project engine copies (read-only rule) and show what an update would lose.
- Writes beside the current folder first, then swaps folders; a series folder advances only to a newer patch.

`Engines/**` is `-text` in git: files retain the bytes produced by `git archive`, so hashes match.