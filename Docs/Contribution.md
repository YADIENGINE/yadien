# Contributing to YADIEN

YADIEN is split between the launcher and the engine. Choose the repository that owns the part you want to change,
then use the links below to understand its development workflow and architecture.

## Launcher

The [launcher repository](https://github.com/YADIENGINE/yadien) owns project management, engine installation and
updates, the launcher UI, the command-line entry point, and packaging the Windows installer.

- [Development](Development.md) — set up the toolchain, build the launcher, and run its tests.
- [Architecture](Architecture.md) — understand the repository layout, layers, runtime layout, and code rules.
- [Core API](CoreApi.md) — find the headless launcher services used by the UI and CLI.
- [UI screens](UiScreens.md) — understand screen ownership and UI behavior.
- [Engine version management](Engines.md) — work on engine discovery, downloads, integrity checks, and project upgrades.
- [Toolchain](Toolchain.md) — understand the tools bundled with the launcher.
- [Modes](Modes.md) — understand installed, portable, and development behavior.
- [Packaging](Packaging.md) — build and publish the installer and releases.

## Engine

The [engine repository](https://github.com/YADIENGINE/engine) owns the runtime, renderer, editor, optional modules,
project tools, and engine documentation.

- [Repository layout and contribution rules](https://github.com/YADIENGINE/engine/blob/main/Docs/Contributing/RepositoryLayout.md)
- [Testing guide](https://github.com/YADIENGINE/engine/blob/main/Docs/Contributing/Testing.md)
- [Architecture](https://github.com/YADIENGINE/engine/blob/main/Docs/Architecture.md)
- [Design documentation](https://github.com/YADIENGINE/engine/blob/main/Docs/Design/README.md)
- [Engine documentation index](https://github.com/YADIENGINE/engine/blob/main/Docs/README.md)

## Before submitting a change

1. Make the change in the repository that owns the behavior.
2. Read that repository's development and contribution rules before editing.
3. Keep the change focused and update the relevant documentation with it.
4. Run the smallest relevant test or validation described by that repository.
