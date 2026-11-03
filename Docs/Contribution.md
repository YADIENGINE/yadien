# Contributing to YADIEN

YADIEN is split between the launcher and the engine. Change the repository that owns the behavior, then follow its
development and validation instructions.

## Launcher

The [launcher repository](https://github.com/YADIENGINE/yadien) owns project management, engine installation, the
launcher UI and CLI, and the Windows installer.

- [Development](Development.md) — local setup, build, and tests.
- [Architecture](Architecture.md) — repository layout and ownership boundaries.
- [Core API](CoreApi.md) — headless services shared by UI and CLI.
- [UI screens](UiScreens.md) — screen ownership and UI rules.
- [Engine versions](Engines.md) — project types, engine discovery, integrity, and upgrades.
- [Toolchain](Toolchain.md) — bundled and optional tools.
- [Packaging](Packaging.md) — installer, update testing, and releases.

Keep changes focused, update the owning document, and run the smallest relevant validation described by
[Development](Development.md).

## Engine

The [engine repository](https://github.com/YADIENGINE/engine) owns the runtime, renderer, editor, project tools, optional
modules, and engine documentation. Start with its
[documentation index](https://github.com/YADIENGINE/engine/blob/main/Docs/README.md) and
[contribution rules](https://github.com/YADIENGINE/engine/blob/main/Docs/Contributing/RepositoryLayout.md).
