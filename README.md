<div align="center">

# Build the game you wish existed

**A bright, agentic game engine for turning playful ideas into games.**

[![Download for Windows](https://img.shields.io/badge/Download_for_Windows-F4A340?style=for-the-badge&logo=windows11&logoColor=111827)](https://github.com/YADIENGINE/yadien/releases)
[![Early preview](https://img.shields.io/badge/Early_Preview-8B5CF6?style=for-the-badge)](https://github.com/YADIENGINE/yadien/releases)
[![MIT licensed](https://img.shields.io/badge/License-MIT-22C55E?style=for-the-badge)](LICENSE)

**[Download the latest installer](https://github.com/YADIENGINE/yadien/releases)** · Windows desktop

</div>

The engine is built to be driven by your own AI agent. Claude Code, Codex, or any other CLI agent writes the game code, opens
the result in the live editor, looks at it, and fixes what it sees.

> The engine is in early `0.x` preview. Releases are not code-signed yet, so Windows SmartScreen may ask you to choose
> **More info → Run anyway** during installation.

## 🌱 Make your first project

1. Download and run the latest `Setup.exe` from [Releases](https://github.com/YADIENGINE/yadien/releases).
2. Istall and start a **New project**.
3. Connect your agent: add one line to your agent file. For Claude, add in `CLAUDE.md`; for others, add to `AGENTS.md`:

   ```text
   @Engine/AgentDocs/Agent.md
   ```

That is it!

## 🤖 YADIEN is agentic!

An agent usually works blind: it edits code and hopes. YADIEN closes the loop. You keep the editor open and ask
for a change; the agent does the rest!

| | |
| --- | --- |
| 🔌 **Bring your own agent** | The editor's Console runs Claude Code, Codex, or a custom command. No MCP is needed. |
| 👀 **Agents see the result** | Agents can see the editor screen when they need to. |
| 📍 **You point, it reads** | Mark a region in the editor and agent can check it. No more "look at this!" explanations. |
| 🔊 **Agents analyse sounds** | Agents can analyse sounds using engine. |
| 🔊 **Agents can generate sounds** | Agents can generate sound effects using engine. |
| 📝 **Everything can be text** | Graphics can be drawn in code, sounds can be generated with Python, and tuning lives in JSON, so an agent can author content without external tools or ready-made assets. |
| 📟 **Feedback is machine-readable** | Failed builds list `file:line` errors, tests write `results.json`, and `ctl` answers in JSON with fixed exit codes. |
| 🎛️ **You stay in control** | Several Console sessions with live agent state, progress notes, and an Agent control switch that makes the editor read-only to agents. |

You are the brain, agents are the muscles!

## ✨ Why make a game here?

| | |
| --- | --- |
| 🤖 **Agentic** | You design the game, YADIEN gives your agents power to make it easily. |
| 🔓 **Open Source** | YADIEN is open sourced, the license is MIT, its completely free. |
| 🚀 **Lightweight** | YADIEN is around 30 MB, all the tools combined is around 150. |
| **Premade Modules** | YADIEN comes with premade modules that you agents can use to develop games more easily. And we are still adding new modules! |


## 🧩 What comes in the box

**In the engine:**

- **Rendering:** `Painter` shapes and paths, offscreen surfaces, bloom and quality settings, Unicode text and layout.
- **UI:** themes, widgets, backdrops, menu and grid navigation.
- **Effects:** particles, trails, camera shots and shake, transitions, shockwaves, overlays, hitstop, and slow motion.
- **Runtime:** scene lifecycle, input, audio, options and saves, live tuning files.
- **Gameplay helpers:** stat modifiers, timed effects, tiered goals, and weighted picks.
- **Editor:** a separate-process editor that shows production drawing in live, controllable scenes, with Console,
  Assets, and Audio Preview panels.
- **Toolchain:** project-aware commands to create, build, test, edit, and package a game, with generated third-party
  notices.

**Optional modules**, linked only when your game asks for them
([all modules](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/README.md)):

| Module | Use it for |
| --- | --- |
| [JsonIO](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/JsonIO/README.md) | JSON documents and files, atomic writes, encryption, gzip |
| [Persistence](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/Persistence/README.md) | Persistent key/value saves with backup and recovery |
| [Localization](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/Localization/README.md) | Language catalogs, fallback, placeholders, number formatting |
| [ScoreBoard](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/ScoreBoard/README.md) | Score history, ranking, and game-defined validation |
| [RunTracking](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/RunTracking/README.md) | Compressed JSON archive of play sessions |
| [Version](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/Version/README.md) | Installed-version changes and patch-note queries |
| [Popup](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/Popup/README.md) | Animated drawing callbacks with lifetimes, motion, and handles |
| [NumberPopup](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/NumberPopup/README.md) | Numeric feedback, label formatting, accumulation, and local juice |
| [Profiler](https://github.com/YADIENGINE/engine/blob/main/Docs/Modules/Profiler/README.md) | CPU/GPU measurement, frame statistics, and performance comparisons |

## 📚 Pick up where your idea takes you

### Making a game

- [Engine handbook](https://github.com/YADIENGINE/engine/blob/main/Docs/README.md) — the starting point for all engine documentation.
- [Your first game](https://github.com/YADIENGINE/engine/blob/main/Docs/GettingStarted.md) — create a scene, draw, and respond to input.
- [Using an AI agent](https://github.com/YADIENGINE/engine/blob/main/Docs/ProjectSetup.md#using-an-ai-agent) — connect your agent to a project.
- [Editor automation](https://github.com/YADIENGINE/engine/blob/main/Docs/Editor/Automation.md) — every command an agent can use to drive the editor.
- [Editor guide](https://github.com/YADIENGINE/engine/blob/main/Docs/Editor/README.md) — connect live editor scenes and tune your game visually.
- [Graphics guide](https://github.com/YADIENGINE/engine/blob/main/Docs/Graphics/README.md) — rendering, text, themes, effects, and animation.
- [Tuning values](https://github.com/YADIENGINE/engine/blob/main/Docs/Tuning.md) — adjust game feel without rebuilding.
- [Packaging a release](https://github.com/YADIENGINE/engine/blob/main/Docs/Release.md) — prepare a Windows build and its notices.

### Contribution

Want to help improve YADIEN? Start with the [contribution hub](Docs/Contribution.md).
