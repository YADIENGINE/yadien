"""Which engine checkout the launcher's developer scripts use (Docs/Development.md).

- Standalone clone: the launcher's own Engine/ submodule.
- Inside a project (the launcher repository is a submodule next to the project's Engine/, e.g. <game>/Launcher):
  the project's Engine/. One engine checkout serves the game and the launcher; the launcher's own Engine/
  submodule is left uninitialized there and its pointer is set with `dev.py pin-engine`.
- `--engine PATH` (dev.py / import_engine.py) overrides both.
"""

from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
MANIFEST_FILE = "EngineManifest.json"
PROJECT_FILE = "Project.json"


def own_engine() -> Path:
    return REPOSITORY / "Engine"


def enclosing_project_engine() -> Path | None:
    parent = REPOSITORY.parent
    engine = parent / "Engine"
    if (parent / PROJECT_FILE).is_file() and (engine / MANIFEST_FILE).is_file():
        return engine
    return None


def default_engine() -> Path:
    return enclosing_project_engine() or own_engine()
