<p align="center">
  <img src="OpenCrank.jpg" alt="OpenCrank" width="200"/>
</p>

<h1 align="center">OpenCrank C++</h1>

<p align="center">
  <strong>A modular AI assistant framework written in C++ with an agentic tool-calling loop, dynamic plugin system, and a Markdown-based skills architecture.</strong>
</p>

<p align="center">
  <a href="#quick-start">Quick Start</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#agentic-loop">Agentic Loop</a> •
  <a href="#skills-system">Skills System</a> •
  <a href="#plugins">Plugins</a> •
  <a href="#configuration">Configuration</a>
</p>

---

## Overview

OpenCrank is a personal AI assistant framework that runs as a single native binary with optional shared-library plugins. It connects to messaging channels (Telegram, WhatsApp), AI providers (Claude, Llama.cpp), and exposes a WebSocket gateway with a built-in web UI — all orchestrated through a central event loop in pure C++.

The AI doesn't just answer questions — it **acts**. OpenCrank implements a full agentic loop that lets the AI read/write files, execute shell commands, browse the web, manage persistent memory, and invoke user-defined skills, all through iterative tool calls until the task is complete.

### Key Features

| Feature | Description |
|---|---|
| **Agentic Tool Loop** | Multi-iteration loop where the AI calls tools, reads results, and decides next steps autonomously |
| **Dynamic Plugin System** | Load `.so` plugins at runtime — channels, AI providers, and tools |
| **Skills System** | Drop a `SKILL.md` file into a directory and the AI learns new capabilities |
| **Memory & Tasks** | SQLite-backed persistent memory with BM25 full-text search and task management |
| **Multiple Channels** | Telegram, WhatsApp, and WebSocket gateway with web UI |
| **Multiple AI Providers** | Claude API and Llama.cpp (local models via OpenAI-compatible API) |
| **Built-in Tools** | File I/O, shell execution, web browsing, content chunking, memory/task management |
| **Session Management** | Per-user conversation history with configurable scoping (DM, group, per-peer) |
| **Rate Limiting** | Token-bucket and sliding-window rate limiters per user |
| **AI Process Monitor** | Heartbeat tracking, hang detection, automatic typing indicators |
| **Minimal Binary** | Small core binary; all optional functionality lives in plugins |

---

## Quick Start

### Requirements

- C++ compatible compiler (g++ or clang++)
- `libcurl-dev`, `libsqlite3-dev`, `libssl-dev`

**Fedora/RHEL:**
```bash
sudo dnf install gcc-c++ libcurl-devel sqlite-devel openssl-devel
```

**Ubuntu/Debian:**
```bash
sudo apt-get install build-essential libcurl4-openssl-dev libsqlite3-dev libssl-dev
```

### Build & Run

```bash
git clone https://github.com/user/opencrank-cpp.git
cd opencrank-cpp

make                # Build binary + all plugins
```

```bash
cp config.example.json config.json
# Edit config.json — add your API keys and bot tokens

./bin/opencrank config.json
```

### Build Targets

| Command | Description |
|---|---|
| `make` | Build main binary and all plugins |
| `make core` | Build only the core objects |
| `make plugins` | Build only plugins (requires core) |
| `make debug` | Debug build (`-g -O0`) |
| `make release` | Optimized build (`-O3`, stripped) |
| `make clean` | Remove all build artifacts |
| `make install` | Install to `/usr/local` |

### Output Structure

```
bin/
├── opencrank              # Main binary (orchestrator)
└── plugins/
    ├── telegram.so        # Telegram channel
    ├── whatsapp.so        # WhatsApp channel
    ├── claude.so          # Claude AI provider
    ├── llamacpp.so        # Llama.cpp local AI provider
    ├── gateway.so         # WebSocket gateway + web UI
    └── polls.so           # Poll system
```

---

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                     Application Singleton                      │
│  Config · PluginLoader · SessionManager · ThreadPool · Agent   │
│  SkillManager · AIProcessMonitor · RateLimiter                 │
└───────────────┬──────────────┬─────────────────┬───────────────┘
                │              │                 │
    ┌───────────┴──┐    ┌──────┴──────┐   ┌──────┴──────┐
    │   Channels   │    │  AI Agents  │   │    Tools    │
    │  (plugins)   │    │  (plugins)  │   │  (built-in  │
    │              │    │             │   │  + plugins)  │
    ├──────────────┤    ├─────────────┤   ├─────────────┤
    │ telegram.so  │    │ claude.so   │   │ Browser     │
    │ whatsapp.so  │    │ llamacpp.so │   │ Memory      │
    │ gateway.so   │    │             │   │ File I/O    │
    │              │    │             │   │ Bash        │
    └──────────────┘    └─────────────┘   └─────────────┘
                                │
                        ┌───────┴────────┐
                        │  Agentic Loop  │
                        │  (tool calls)  │
                        └───────┬────────┘
                                │
                    ┌───────────┴───────────┐
                    │    Skills System      │
                    │  (SKILL.md prompts)   │
                    └──────────────────────┘
```

### How It Works

1. **Startup** — `Application::init()` loads `config.json`, discovers plugins from the plugin directory, initializes channels, AI providers, and tools, loads skills from workspace directories, and builds the system prompt.

2. **Message Routing** — When a channel plugin receives a message, it fires a callback. The `MessageHandler` performs deduplication and rate limiting, then enqueues the message into the `ThreadPool`.

3. **Command Dispatch** — If the message starts with `/`, it's matched against registered commands (built-in or skill commands). Otherwise, it's forwarded to the AI provider.

4. **Agentic Loop** — The AI response is parsed for `<tool_call>` XML tags. If found, the referenced tool is executed, results are injected back into the conversation, and the AI is called again. This repeats until the AI produces a final response with no tool calls, or the iteration limit is reached.

5. **Response Delivery** — The final text is split into chunks (if needed) and sent back through the originating channel.

---

## Agentic Loop

The core of OpenCrank's intelligence is its **agentic loop** — an iterative cycle that allows the AI to act on the world, not just respond.

### How the Loop Works

```
User Message
    │
    ▼
┌─────────────────────┐
│  Build system prompt │◄──── Skills prompt + Tools prompt
│  + conversation      │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│   Call AI Provider   │──── Claude API / Llama.cpp
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐     ┌──────────────────────┐
│  Parse AI response   │────►│  Has <tool_call> tags? │
└──────────────────────┘     └──────────┬───────────┘
                                  │           │
                                 Yes          No
                                  │           │
                                  ▼           ▼
                        ┌─────────────┐   Return final
                        │Execute tool  │   response to user
                        │Inject result │
                        └──────┬──────┘
                               │
                               ▼
                        Loop back to
                        "Call AI Provider"
                        (max 10 iterations)
```

### Tool Call Format

The AI uses an XML-based format to invoke tools:

```xml
<tool_call name="bash">
  {"command": "ls -la /workspace"}
</tool_call>
```

Results are injected back as:

```xml
<tool_result name="bash" success="true">
  total 42
  drwxr-xr-x  5 user user  4096 Jan 15 10:30 .
  -rw-r--r--  1 user user  1234 Jan 15 10:28 config.json
  ...
</tool_result>
```

### Built-in Tools

| Tool | Description |
|---|---|
| `read` | Read file contents (with line ranges) |
| `write` | Write/create files |
| `bash` | Execute shell commands (with timeout) |
| `list_dir` | List directory contents |
| `browser_fetch` | Fetch web page content |
| `browser_links` | Extract links from a URL |
| `memory_save` | Save content to persistent memory |
| `memory_search` | BM25 full-text search across memory |
| `memory_get` | Read a specific memory file |
| `task_create` | Create a tracked task |
| `task_list` | List pending tasks |
| `task_complete` | Mark a task as done |
| `content_chunk` | Retrieve chunks of large content |
| `content_search` | Search within large chunked content |

### Content Chunking

When a tool returns content larger than 15,000 characters, OpenCrank automatically chunks it and provides a summary to the AI. The AI can then request specific chunks or search within the content using `content_chunk` and `content_search` tools, avoiding context window overflow.

### Safety

- **Path sandboxing** — File operations are restricted to the workspace directory. Directory traversal is blocked.
- **Command timeout** — Shell commands have a configurable timeout (default 20s).
- **Iteration limit** — The agentic loop stops after 10 iterations (configurable).
- **Error limit** — 3 consecutive tool errors halt the loop.
- **Token limit recovery** — If the context window overflows, the agent automatically truncates conversation history and retries.

---

## Skills System

Skills are the mechanism for teaching the AI new capabilities without writing C++ code. A skill is simply a `SKILL.md` Markdown file placed in a directory.

### How Skills Work

1. At startup, the `SkillManager` scans configured directories for subdirectories containing a `SKILL.md` file.
2. Each `SKILL.md` is parsed for YAML-like frontmatter (name, description, metadata) and a Markdown body containing instructions.
3. Eligible skills are injected into the AI's system prompt as an `<skills>` XML block, giving the AI awareness of available capabilities.
4. When a user sends a message, the AI can read and follow the instructions in any active skill to accomplish the task.

### Directory Structure

```
skills/
├── weather/
│   └── SKILL.md          # Weather lookup instructions
├── translate/
│   └── SKILL.md          # Translation instructions
└── summarize/
    └── SKILL.md          # Document summarization instructions
```

### SKILL.md Format

Each skill file uses YAML-style frontmatter followed by Markdown instructions:

```markdown
---
name: weather
description: Get current weather and forecasts (no API key required).
homepage: https://wttr.in/:help
metadata: { "opencrank": { "emoji": "🌤️", "requires": { "bins": ["curl"] } } }
---

# Weather

## Open-Meteo (JSON)

Free, no key, good for programmatic use:

\`\`\`bash
curl -s "https://api.open-meteo.com/v1/forecast?latitude=51.5&longitude=-0.12&current_weather=true"
\`\`\`

Find coordinates for a city, then query. Returns JSON with temp, windspeed, weathercode.
```

### Skill Loading Precedence

Skills are loaded from multiple directories with a priority system — higher-priority sources override lower ones:

| Priority | Source | Description |
|---|---|---|
| 1 (highest) | **Workspace** | `skills/` in the current workspace directory |
| 2 | **Managed** | `~/.config/opencrank/skills/` (user-installed) |
| 3 | **Bundled** | Built-in skills shipped with OpenCrank |
| 4 (lowest) | **Extra** | Additional directories from config |

### Skill Metadata

The frontmatter supports rich metadata for controlling skill behavior:

| Field | Description |
|---|---|
| `name` | Skill identifier |
| `description` | Short description shown in `/skills` list |
| `homepage` | URL for documentation |
| `metadata.opencrank.emoji` | Display emoji |
| `metadata.opencrank.always` | Always include in system prompt |
| `metadata.opencrank.requires.bins` | Required binaries (eligibility check) |
| `metadata.opencrank.requires.any_bins` | At least one must exist |
| `metadata.opencrank.requires.env` | Required environment variables |
| `metadata.opencrank.os` | OS restrictions (`darwin`, `linux`, `win32`) |

### Skill Eligibility

Before a skill is included in the system prompt, OpenCrank checks:

- **Binary requirements** — Are the required CLI tools installed? (`curl`, `ffmpeg`, etc.)
- **Environment variables** — Are the needed API keys set?
- **OS restrictions** — Is the skill compatible with the current platform?
- **Config filters** — Does the skill pass the user's skill filter list?

Skills that fail eligibility checks are silently excluded.

### Skill Commands

Skills can also register as chat commands. When a skill is loaded, it becomes available as `/skillname` in chat, allowing users to invoke skill-specific functionality directly.

---

## Plugins

Plugins are shared libraries (`.so`) loaded at runtime via `dlopen`. Each plugin implements one of three interfaces:

### Plugin Types

| Type | Interface | Purpose |
|---|---|---|
| **Channel** | `ChannelPlugin` | Messaging integrations (Telegram, WhatsApp, Gateway) |
| **AI** | `AIPlugin` | LLM providers (Claude, Llama.cpp) |
| **Tool** | `ToolProvider` | Agent tools (Browser, Memory) |

### Available Plugins

| Plugin | Type | Description |
|---|---|---|
| `telegram.so` | Channel | Telegram Bot API with long-polling |
| `whatsapp.so` | Channel | WhatsApp Business API bridge |
| `gateway.so` | Channel | WebSocket server with JSON-RPC protocol and built-in web UI |
| `claude.so` | AI | Anthropic Claude API (Sonnet, Opus, Haiku) |
| `llamacpp.so` | AI | Llama.cpp server via OpenAI-compatible API (fully local) |
| `polls.so` | Tool | Interactive poll creation and management |

### Plugin Search Paths

1. `plugins_dir` from `config.json`
2. `./plugins`
3. `/usr/lib/opencrank/plugins`
4. `/usr/local/lib/opencrank/plugins`

### Creating a Plugin

```cpp
#include <opencrank/core/loader.hpp>
#include <opencrank/core/channel.hpp>

class MyChannel : public opencrank::ChannelPlugin {
public:
    const char* name() const override { return "My Channel"; }
    const char* version() const override { return "1.0.0"; }
    const char* channel_id() const override { return "mychannel"; }

    bool init(const opencrank::Config& cfg) override { /* ... */ return true; }
    void shutdown() override { /* ... */ }
    bool start() override { /* ... */ return true; }
    bool stop() override { return true; }
    opencrank::ChannelStatus status() const override { return opencrank::ChannelStatus::RUNNING; }
    opencrank::ChannelCapabilities capabilities() const override { return {}; }
    opencrank::SendResult send_message(const std::string& to, const std::string& text) override { /* ... */ }
    void poll() override { /* ... */ }
};

OPENCRANK_DECLARE_PLUGIN(MyChannel, "mychannel", "1.0.0", "My custom channel", "channel")
```

Build as a shared library:

```bash
g++ -std=c++11 -fPIC -shared -I./include mychannel.cpp -o mychannel.so
```

---

## Memory System

OpenCrank includes a built-in persistent memory system backed by SQLite with BM25 full-text search.

### Capabilities

- **File-based memory** — Save and retrieve Markdown documents in a `memory/` directory
- **Automatic chunking** — Large documents are split into overlapping chunks for search
- **BM25 search** — Full-text search using SQLite FTS5
- **Session transcripts** — Conversation history is indexed for search
- **Task management** — Create, list, and complete tracked tasks with due dates

### How It Integrates

The memory system is exposed to the AI through agent tools (`memory_save`, `memory_search`, `memory_get`, `task_create`, `task_list`, `task_complete`). The AI can autonomously decide to save important information or search past conversations.

---

## Configuration

All configuration lives in a single `config.json` file. See [`config.example.json`](config.example.json) for all options with descriptions.

### Quick Configurations

**Telegram Bot with Claude:**
```json
{
  "plugins": ["telegram", "claude"],
  "telegram": { "bot_token": "..." },
  "claude": { "api_key": "..." }
}
```

**WebSocket Gateway with Web UI:**
```json
{
  "plugins": ["gateway", "claude"],
  "gateway": { "port": 18789, "bind": "0.0.0.0" },
  "claude": { "api_key": "..." }
}
```

**Fully Local (Llama.cpp):**
```json
{
  "plugins": ["gateway", "llamacpp"],
  "llamacpp": { "url": "http://localhost:8080" }
}
```

### Configuration Reference

| Option | Default | Description |
|---|---|---|
| `plugins` | `[]` | List of plugins to load |
| `plugins_dir` | `./bin/plugins` | Plugin search directory |
| `workspace_dir` | `.` | Working directory for file operations |
| `log_level` | `info` | Logging: `debug`, `info`, `warn`, `error` |
| `system_prompt` | *(built-in)* | Custom system prompt for the AI |
| `skills.bundled_dir` | *(auto)* | Directory for bundled skills |
| `skills.managed_dir` | *(auto)* | Directory for user-installed skills |
| `telegram.bot_token` | — | Telegram Bot API token |
| `telegram.poll_timeout` | `30` | Long-poll timeout in seconds |
| `claude.api_key` | — | Anthropic API key |
| `claude.model` | `claude-sonnet-4-20250514` | Model to use |
| `claude.max_tokens` | `4096` | Max tokens per response |
| `claude.temperature` | `1.0` | Sampling temperature |
| `llamacpp.url` | `http://localhost:8080` | Llama.cpp server URL |
| `llamacpp.model` | `local-model` | Model name for API |
| `gateway.port` | `18789` | WebSocket server port |
| `gateway.bind` | `0.0.0.0` | Bind address |
| `gateway.auth.token` | *(none)* | Authentication token |
| `browser.timeout` | `30` | HTTP fetch timeout |
| `memory.db_path` | `.opencrank/memory.db` | SQLite database path |
| `memory.chunk_tokens` | `400` | Chunk size for indexing |
| `session.max_history` | `20` | Messages to keep in context |
| `session.timeout` | `3600` | Session timeout in seconds |
| `rate_limit.max_tokens` | `10` | Rate limit bucket size |
| `rate_limit.refill_rate` | `2` | Tokens refilled per second |

---

## Bot Commands

| Command | Description |
|---|---|
| `/start` | Welcome message |
| `/help` | Show available commands |
| `/skills` | List loaded skills with eligibility status |
| `/ping` | Check if bot is alive |
| `/info` | Show bot version and system info |
| `/new` | Start a new conversation (clear history) |
| `/status` | Show session status and memory stats |
| `/tools` | List available agent tools |
| `/fetch <url>` | Fetch and display web page content |
| `/links <url>` | Extract links from a web page |

Or just send a message to chat with the AI directly.

---

## Project Structure

```
opencrank-cpp/
├── OpenCrank.jpg                  # Logo
├── config.example.json            # Example configuration with all options
├── Makefile                       # Main build system
├── Makefile.plugin                # Shared rules for plugin builds
│
├── include/opencrank/
│   ├── ai/
│   │   └── ai.hpp                 # AIPlugin interface, ConversationMessage, CompletionResult
│   ├── core/
│   │   ├── application.hpp        # Application singleton (lifecycle, system prompt)
│   │   ├── agent.hpp              # Agentic loop, AgentTool, ContentChunker
│   │   ├── builtin_tools.hpp      # File I/O, bash, content tools
│   │   ├── browser_tool.hpp       # Web fetching and link extraction
│   │   ├── memory_tool.hpp        # Memory/task agent tools
│   │   ├── message_handler.hpp    # Message routing and dispatch
│   │   ├── ai_monitor.hpp         # AI heartbeat and hang detection
│   │   ├── plugin.hpp             # Base Plugin interface
│   │   ├── channel.hpp            # ChannelPlugin interface
│   │   ├── tool.hpp               # ToolProvider interface
│   │   ├── loader.hpp             # Plugin dynamic loading (dlopen)
│   │   ├── registry.hpp           # Plugin and command registry
│   │   ├── session.hpp            # Session management and routing
│   │   ├── config.hpp             # JSON config reader
│   │   ├── http_client.hpp        # libcurl HTTP wrapper
│   │   ├── rate_limiter.hpp       # Token-bucket rate limiter
│   │   ├── thread_pool.hpp        # Worker thread pool
│   │   ├── logger.hpp             # Leveled logging
│   │   ├── types.hpp              # Message, SendResult, ChannelCapabilities
│   │   └── utils.hpp              # String, path, phone utilities
│   ├── memory/
│   │   ├── manager.hpp            # Memory indexing, search, and tasks
│   │   ├── store.hpp              # SQLite storage backend
│   │   └── types.hpp              # MemoryChunk, MemoryConfig, MemorySearchResult
│   └── skills/
│       ├── manager.hpp            # Skill loading, filtering, prompt generation
│       ├── loader.hpp             # SKILL.md parser (frontmatter + content)
│       └── types.hpp              # Skill, SkillEntry, SkillMetadata, SkillRequirements
│
├── src/
│   ├── main.cpp                   # Entry point
│   ├── ai/                        # AI provider implementations
│   ├── core/                      # Core framework implementation
│   ├── memory/                    # Memory system implementation
│   ├── skills/                    # Skills system implementation
│   └── plugins/                   # Plugin source code
│       ├── claude/                # Claude AI plugin
│       ├── llamacpp/              # Llama.cpp AI plugin
│       ├── telegram/              # Telegram channel plugin
│       ├── whatsapp/              # WhatsApp channel plugin
│       ├── gateway/               # WebSocket gateway + web UI
│       └── polls/                 # Polls plugin
│
└── skills/                        # Workspace skills directory
    └── weather/
        └── SKILL.md               # Example: weather lookup skill
```

---

## License

MIT License

## Acknowledgments

Inspired by [OpenClaw](https://github.com/openclaw/openclaw) — a TypeScript-based personal AI assistant.
