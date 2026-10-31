# YADIEN

A C++ 2D game engine for Windows, with a launcher that sets everything up for you. This repository is the
**launcher**: the project manager and command line you install.

- **Projects:** create a project, open it in the editor, run it, keep a list of your projects.
- **No setup:** the launcher bundles Python, CMake and Ninja, and installs the MSVC compiler on first start.
- **Your engine stays yours:** every project gets its own copy of a stable engine version, so it keeps building the
  same way even after newer engines come out. Updating a project's engine is always your choice, with the release
  notes and any breaking changes shown first.
- **Command line:** `<engine> build`, `<engine> run`, `<engine> test`, `<engine> editor` from any project folder,
  with exit codes scripts and AI agents can read.

The engine source lives in its own repository: <https://github.com/YADIENGINE/engine>.

## Download

Installers will be published on the [Releases](https://github.com/YADIENGINE/yadien/releases) page. Releases are not
code-signed yet, so Windows SmartScreen may ask you to confirm ("More info" → "Run anyway").

## Building the launcher

See [Docs/Development.md](Docs/Development.md): Visual Studio 2022 Build Tools (C++ workload), CMake 3.24+,
Python 3.10+ and git, then `python Scripts/fetch_toolchain.py` and `python Scripts/dev.py build`.

Making the installer: [Docs/Packaging.md](Docs/Packaging.md). Architecture and design:
[Docs/Architecture.md](Docs/Architecture.md); design decisions are in the engine
repository under `Docs/Design/`. The documentation is being translated from Turkish to English.

## License

MIT, see [LICENSE](LICENSE).
