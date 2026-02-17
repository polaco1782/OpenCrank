# Copilot instructions — OpenCrank C++

Purpose
- Help AI coding agents become productive quickly in this repository: architecture, build/run, patterns, and integration points.

Big picture (what to know first)
- Single native binary (`bin/opencrank`) plus optional shared-library plugins in `src/plugins/*`.
- Core C++ lives in `src/core` and `src/*` (memory, session, agent, tools). The application entry is `src/main.cpp` which calls `Application::init()` and `Application::run()`.
- Plugins implement three roles: Channel (messaging), AI (LLM provider), and Tool (agentic tools). Plugins are built as `.so` and loaded via `dlopen`.
- Skills are Markdown files (`skills/*/SKILL.md`) that are loaded at startup and injected into the system prompt.

Build & run (concrete commands)
- Build everything: `make` (default target builds core + all plugins).
- Debug build: `make debug` (adds `-g -O0`).
- Run: copy `config.example.json` → `config.json`, edit keys, then `./bin/opencrank config.json` or `make run`.
- Plugin builds are delegated to each `src/plugins/<name>/Makefile` — do not assume a single-step plugin compile inside the top-level build.

Project-specific conventions & patterns
- C++17, PIC for plugin objects (see top-level `Makefile`).
- Tool/agent pattern: tools implement a `ToolProvider` interface and return `ToolResult`. See `src/core/memory_tool.cpp` for concrete examples of:
  - registering actions (`memory_save`, `file_read`, `task_create`, ...)
  - exposing `AgentTool` schemas for the agent (name, description, params, execute lambda)
  - sandbox checks (`Sandbox::instance().is_path_allowed(...)`) and path resolution (`resolve_path`, `ensure_parent_dir`).
- Tool-call JSON format (AI → runtime): `{"tool": "<name>", "arguments": { ... }}` — parse results are injected back into the agentic loop as plain text blocks.
- Skills: `SKILL.md` frontmatter controls eligibility (required bins, env vars); skills that fail checks are silently excluded.

Integration points & external dependencies
- Networking & providers: libcurl, libssl (see `Makefile` LDFLAGS).
- Persistent memory / tasks: SQLite (look under `src/memory/*.cpp`).
- Plugins: loaded from `plugins_dir` in config (fallbacks: `./plugins`, `/usr/lib/opencrank/plugins`, `/usr/local/lib/opencrank/plugins`).

Developer workflows & debugging tips
- Build quickly: `make core` then `make plugins` if iterating on core vs plugins separately.
- To iterate on a single plugin: `make -C src/plugins/<plugin> install` (plugin `Makefile` supports `install`).
- Logs use project logging macros — increase verbosity by building with `-DDEBUG` or using `make debug` and run the binary directly to inspect stdout/stderr.
- Run the binary with a local config: `./bin/opencrank config.json` (`main.cpp` accepts the config path as the sole arg).

Files to read first (examples)
- `src/main.cpp` — entrypoint and `Application::init()` behavior.
- `src/core/application.cpp` — startup sequence: config, plugin discovery, skill loading, system-prompt construction.
- `src/core/memory_tool.cpp` — canonical tool provider: parameter parsing, sandboxing, file IO, and task API examples.
- `Makefile` — exact compiler flags, plugin build flow, and runtime `make run` target.

Do's and don'ts for code changes
- Do respect plugin boundaries: prefer adding functionality as a plugin when feasible.
- Do not bypass sandbox checks for file IO — those are intentional safety constraints (see `Sandbox::instance()` usage).
- When adding new tools, follow the `AgentTool`/`ToolResult` patterns used in `memory_tool.cpp` so the `context_manager` and skill injection continue to work.

If something is missing
- Ask about runtime config values (e.g., `plugins_dir`, channel tokens) or which plugin to target.

Feedback
- Review these notes and tell me any unclear areas or further details you'd like added (examples, specific files, or coding conventions).
