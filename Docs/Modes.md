# User mode and developer mode

Mode is the launcher's authority; files determine project type (engine `Docs/Design/README.md` D4).

| Type (`ProjectInfo::kind`) | Detection | Read-only check | Engine update |
| --- | --- | --- | --- |
| `Copy` | `Engine/EngineCopy.json` exists | yes | from `Engines/`, with confirmation (both modes) |
| `Git` | `Engine/.git` exists (submodule) | no | none in user mode; git operations in developer mode |
| `Unregistered` | neither exists | no, warning | "replace with a version from `Engines/`", with confirmation |
| `Missing` | no usable `Engine/` | — | same "replace" if `Project.json` exists |

Build / run / editor work for every type. User mode never runs git (D14).

## Developer mode

- A plain toggle in Settings; enabling shows a warning confirmation ("unstable engine on `main`; for engine
  contributions"). No password (engine D19): the engine repo is public; GitHub push permissions protect publishing.
- Developer mode adds "dev (main)" for new projects (submodule), "Update engine" and "Publish engine changes" for `Git`
  projects, and a git item in System check.