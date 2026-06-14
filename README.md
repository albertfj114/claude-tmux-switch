# cc — Claude Code Multi-Provider Launcher

A bash script that lets you switch between AI providers when using [Claude Code](https://claude.ai/code), with optional tmux session management. Compatible with macOS bash 3.2+.

## Features

- **Interactive menus** — run `cc` with no args to pick provider + model
- **Multi-provider support** — Anthropic, MiniMax, GLM, OpenRouter, Qwen, DeepSeek, Kimi, Ollama, or any custom endpoint
- **Tmux sessions** — name a session and it opens in tmux (reattaches if it exists)
- **Model picker per provider** — choose from curated model lists, or override with `-m`
- **In-session model switching** — `/model` inside Claude Code maps to real provider tiers (Haiku/Sonnet/Opus → fast/balanced/powerful)
- **Zero dependencies** — just bash 3.2+ and tmux (optional)
- **macOS compatible** — no bash 4+ features required

## Quick start

```bash
# 1. Clone and install
git clone https://github.com/albertfj114/claude-tmux-switch.git
cd claude-tmux-switch
./install.sh

# 2. Set up API keys
mkdir -p ~/.config/cc
cp providers.env.example ~/.config/cc/providers.env
# Edit ~/.config/cc/providers.env with your actual keys

# 3. Run
cc                    # Interactive provider + model picker
cc --glm              # GLM model picker
cc --minimax coding   # MiniMax in tmux session "coding"
```

## Requirements

- [Claude Code CLI](https://docs.anthropic.com/en/docs/claude-code/overview) installed
- `tmux` (only needed for named sessions)
- bash 3.2+ (macOS default works)

## Usage

```
cc [provider] [session-name] [-m model] [-p]
```

| Flag | Description |
|------|-------------|
| `(no args)` | Interactive provider + model picker |
| `--anthropic` | Anthropic (Claude models) |
| `--glm` | GLM via z.ai (model picker) |
| `--minimax` | MiniMax (model picker) |
| `--openrouter` | OpenRouter — access 100+ models (model picker) |
| `--qwen` | Qwen — Coding Plan (Token Plan) / DashScope fallback (model picker) |
| `--deepseek` | DeepSeek direct (model picker) |
| `--kimi` | Kimi (Moonshot) direct |
| `--ollama` | Ollama local models (auto-detected; needs Anthropic proxy) |
| `--custom` | Custom endpoint (set `CC_CUSTOM_*` env vars) |
| `-m MODEL` | Skip picker, use MODEL directly |
| `-p` | Enable `--dangerously-skip-permissions` |
| `session-name` | Open in a named tmux session |

### Examples

```bash
cc                              # Interactive picker: choose provider, then model
cc --glm                        # GLM model picker
cc --glm coding                 # GLM + tmux session "coding"
cc --glm -m glm-5.1 coding     # GLM 5.1 directly, tmux "coding"
cc --openrouter -m deepseek/deepseek-v3.2
cc --minimax debug -p           # MiniMax, skip permissions, session "debug"
cc --qwen                       # Qwen model picker
cc --deepseek                   # DeepSeek model picker
```

### Interactive menus

When you run `cc` with no arguments, you get two menus:

```
  Pick a provider:

    1) Anthropic                        [default]
    2) GLM (z.ai)
    3) MiniMax
    4) OpenRouter
    5) DeepSeek
    6) Ollama (local)
    7) Qwen (Alibaba Cloud)
    8) Kimi (Moonshot)
    9) Custom endpoint

  Enter choice [1]: 4

  [OpenRouter] Pick a model:

    1) Qwen 3.6 Plus                    [default]
    2) DeepSeek R1
    3) Kimi K2 Thinking
    4) DeepSeek V3.2
    5) Qwen3 Coder Plus
    6) Kimi K2.5
    7) DeepSeek V3.2 Speciale
    8) Qwen3 Max

  Enter choice [1]: 3
  Using: Kimi K2 Thinking
```

Press Enter to accept the default, or type a number.

### In-session model switching

At launch, `cc` prints a tier map showing what `/model` will select inside the session:

```
[cc] Provider: qwen (qwen3.6-plus)
     /model in-session maps to:
       Haiku  →  qwen3.6-flash
       Sonnet →  qwen3.6-plus  ◀
       Opus   →  qwen3.7-max
```

The `◀` marks your currently active model. Inside the session, use `/model` and pick Haiku, Sonnet, or Opus to switch between the provider's actual models — Claude Code routes each tier through the corresponding `ANTHROPIC_DEFAULT_*_MODEL` env var that `cc` set at startup.

Tier mappings by provider:

| Provider | Haiku (fast) | Sonnet (balanced) | Opus (powerful) |
|----------|-------------|-------------------|-----------------|
| Qwen | qwen3.6-flash | qwen3.6-plus | qwen3.7-max |
| GLM | glm-4.7-flash | glm-5-turbo | glm-5.2[1m] |
| MiniMax | M2.7 Turbo | M2.7 | M1 |
| DeepSeek | deepseek-v3 | deepseek-v3 | deepseek-r1 |

Providers not in this table (OpenRouter, Kimi, Ollama, custom) use the single selected model for all tiers.

## How it works

Claude Code uses the Anthropic SDK internally. This script redirects requests to other providers by setting environment variables before launching `claude`:

| Variable | Purpose |
|----------|---------|
| `ANTHROPIC_BASE_URL` | API endpoint URL |
| `ANTHROPIC_AUTH_TOKEN` | API key for the provider |
| `ANTHROPIC_MODEL` | Primary model (set at launch) |
| `ANTHROPIC_DEFAULT_HAIKU_MODEL` | Model used when `/model` → Haiku |
| `ANTHROPIC_DEFAULT_SONNET_MODEL` | Model used when `/model` → Sonnet |
| `ANTHROPIC_DEFAULT_OPUS_MODEL` | Model used when `/model` → Opus |

All supported providers expose an Anthropic-compatible Messages API, so Claude Code works with them natively without any shim or proxy (except Ollama, which needs a proxy like [litellm](https://github.com/BerriAI/litellm)).

## Configuration

### Environment file

Default locations (checked in order):
1. `$CC_ENV_FILE` (if set)
2. `~/.config/cc/providers.env`
3. `~/Documents/Projects/.env`

```bash
# providers.env — only uncomment providers you have keys for
# ANTHROPIC_API_KEY=sk-ant-...
# MINIMAX_API_KEY=...
# Z_GLM_API_KEY=...
# OPENROUTER_API_KEY=...
# QWEN_API_KEY=...        # sk-sp-* Token/Coding Plan (default) — or QWEN-API-KEY= for pay-as-you-go fallback
# DEEPSEEK_API_KEY=...
# KIMI-API-KEY=...
```

### Install location

`install.sh` symlinks `cc` to `~/.local/bin/cc` by default. Make sure `~/.local/bin` is in your `$PATH`.

## Adding a new provider

Add a new entry to the `PROVIDERS` array and a matching case in the provider switch block:

```bash
# 1. Add to PROVIDERS array
PROVIDERS=( ... "yourprovider|Your Provider Name" )

# 2. Add a model array
YOURPROVIDER_MODELS=(
  "model-id|Display Name"
)

# 3. Add to the provider case block
yourprovider)
  [[ -z "${YOURPROVIDER_API_KEY:-}" ]] && { echo "Error: ..." >&2; exit 1; }
  P_BASE_URL="https://api.yourprovider.com/anthropic"
  P_AUTH_TOKEN="$YOURPROVIDER_API_KEY"
  P_MODEL="$(pick_model YOURPROVIDER_MODELS "Your Provider")"
  ;;
```

Then add `YOURPROVIDER_API_KEY=your-key` to your `providers.env`.

### Adding models to an existing provider

Just add a line to the model array — first entry is the default:

```bash
GLM_MODELS=(
  "glm-5.2|GLM 5.2 (latest)"        # default
  "glm-4.7-flash|GLM 4.7 Flash"     # new
)
```

## Custom provider (no code changes)

Use `--custom` with environment variables for any endpoint:

```bash
CC_CUSTOM_BASE_URL="https://your-proxy.example.com/anthropic" \
CC_CUSTOM_API_KEY="your-key" \
CC_CUSTOM_MODEL="your-model" \
cc --custom
```

Or set those in your `providers.env`.

## Security notes

- API keys are loaded from a local file — never committed to git
- The `.gitignore` blocks all `.env` files
- The `-p` flag (`--dangerously-skip-permissions`) disables Claude Code's tool confirmation prompts. Only use this if you understand the risks.

## License

MIT
