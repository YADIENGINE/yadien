"""Which engine checkout the launcher's developer scripts use (Docs/Development.md).

- Inside a project (the launcher repository is a submodule next to the project's Engine/, e.g. <game>/Launcher):
  the project's Engine/. One engine checkout serves the game and the launcher.
- Standalone clone: ToolCache/Engine, a clone of the engine repository at the tag of the version LauncherManifest.json
  ships, made when first needed.
- `--engine PATH` (dev.py / import_engine.py) overrides both.
"""

import json
import subprocess
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
MANIFEST_FILE = "EngineManifest.json"
PROJECT_FILE = "Project.json"
LAUNCHER_MANIFEST = "LauncherManifest.json"
FETCHED_ENGINE = REPOSITORY / "ToolCache" / "Engine"


def enclosing_project_engine() -> Path | None:
    parent = REPOSITORY.parent
    engine = parent / "Engine"
    if (parent / PROJECT_FILE).is_file() and (engine / MANIFEST_FILE).is_file():
        return engine
    return None


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], capture_output=True, text=True)


def fetched_engine(url: str, version: str, folder: Path = FETCHED_ENGINE) -> Path:
    """`folder` as a clone of `url` at tag v<version>; touches the network only when the tag is not there yet."""
    tag = f"v{version}^{{commit}}"
    if not (folder / ".git").exists():
        print(f"Cloning the engine from {url} into {folder}", flush=True)
        result = git("clone", "--no-checkout", url, str(folder))
        if result.returncode != 0:
            raise SystemExit(f"error: cannot clone the engine from {url}: {result.stderr.strip()}")
    if git("-C", str(folder), "rev-parse", "--verify", "--quiet", tag).returncode != 0:
        result = git("-C", str(folder), "fetch", "--tags", "origin")
        if result.returncode != 0:
            raise SystemExit(f"error: cannot fetch the engine's tags: {result.stderr.strip()}")
    result = git("-C", str(folder), "checkout", "--force", "--detach", tag)
    if result.returncode != 0:
        raise SystemExit(f"error: engine tag v{version} not found in {url}; release it first or pass --engine PATH\n"
                         f"{result.stderr.strip()}")
    return folder


def default_engine() -> Path:
    project_engine = enclosing_project_engine()
    if project_engine is not None:
        return project_engine
    manifest = json.loads((REPOSITORY / LAUNCHER_MANIFEST).read_text(encoding="utf-8"))
    return fetched_engine(manifest["engineRepository"], manifest["engine"])
