# cc — Claude Code Multi-Provider Launcher

A lightweight bash script that lets you switch between AI providers when using [Claude Code](https://claude.ai/code), with optional tmux session management.

## What it does

- **Multi-provider support** — switch between Anthropic, MiniMax, GLM, OpenRouter, DeepSeek, or any custom endpoint
- **Tmux sessions** — name a session and it opens in tmux (reattaches if it already exists)
- **Model overrides** — use any model a provider offers with `-m`
- **Zero dependencies** — just bash and tmux (Claude Code must be installed)

## Quick start

```bash
# 1. Clone and install
git clone https://github.com/YOUR_USERNAME/claude-tmux-switch.git
cd claude-tmux-switch
./install.sh

# 2. Set up API keys
mkdir -p ~/.config/cc
cp providers.env.example ~/.config/cc/providers.env
# Edit ~/.config/cc/providers.env with your actual keys

# 3. Run
cc                    # Anthropic (default)
cc --glm              # GLM-5.1
cc --minimax coding   # MiniMax in tmux session "coding"
```

## Requirements

- [Claude Code CLI](https://docs.anthropic.com/en/docs/claude-code/overview) installed and authenticated
- `tmux` (only needed for named sessions)
- bash 3.2+

## Usage

```
cc [provider] [session-name] [-m model] [-p]
```

| Flag | Description |
|------|-------------|
| `(default)` | Anthropic via your existing `ANTHROPIC_API_KEY` |
| `--minimax` | MiniMax M2.7 |
| `--glm` | GLM-5.1 via z.ai |
| `--openrouter` | OpenRouter (access to 100+ models) |
| `--deepseek` | DeepSeek (experimental — OpenAI format) |
| `--custom` | Any Anthropic-compatible endpoint |
| `-m MODEL` | Override the default model |
| `-p` | Enable `--dangerously-skip-permissions` |
| `session-name` | Open in a named tmux session |

### Examples

```bash
cc                              # Anthropic, current terminal
cc work                         # Anthropic in tmux session "work"
cc --glm                        # GLM-5.1, current terminal
cc --glm coding                 # GLM in tmux session "coding"
cc --openrouter -m anthropic/claude-opus-4-20250514
cc --minimax debug -p           # MiniMax, skip permission prompts, session "debug"
```

## How it works

Claude Code uses the Anthropic SDK internally. This script redirects requests to other providers by setting environment variables:

| Variable | Purpose |
|----------|---------|
| `ANTHROPIC_BASE_URL` | API endpoint URL |
| `ANTHROPIC_AUTH_TOKEN` | API key for the provider |
| `ANTHROPIC_MODEL` | Model to use |

Providers that expose an Anthropic-compatible Messages API work natively. Providers using OpenAI format (like DeepSeek, Qwen) may not work — Claude Code expects the Anthropic protocol.

## Adding a new provider

Edit the `cc` script and add a new case to the provider switch block:

```bash
  yourprovider)
    [[ -z "${YOURPROVIDER_API_KEY:-}" ]] && { echo "Error: YOURPROVIDER_API_KEY not set" >&2; exit 1; }
    P_BASE_URL="https://api.yourprovider.com/anthropic"
    P_AUTH_TOKEN="$YOURPROVIDER_API_KEY"
    P_MODEL="${MODEL:-your-default-model}"
    ;;
```

Then add `YOURPROVIDER_API_KEY=your-key` to your `providers.env` file and a `--yourprovider` flag to the argument parser.

### Finding provider endpoints

Look for providers that offer an **Anthropic-compatible** or **Anthropic Messages API** endpoint. Common patterns:
- `/anthropic` path (MiniMax, z.ai)
- OpenRouter's `/api/anthropic` route
- Self-hosted proxies like [LiteLLM](https://github.com/BerriAI/litellm) that translate between formats

## Custom provider

Use `--custom` with environment variables for any endpoint without modifying the script:

```bash
CC_CUSTOM_BASE_URL="https://your-proxy.example.com/anthropic" \
CC_CUSTOM_API_KEY="your-key" \
CC_CUSTOM_MODEL="your-model" \
cc --custom
```

Or set those in your `providers.env`.

## Configuration

### Environment file

Default location: `~/.config/cc/providers.env` (override with `CC_ENV_FILE`):

```bash
# Only uncomment providers you have keys for
ANTHROPIC_API_KEY=sk-ant-...
# MINIMAX_API_KEY=...
# Z_GLM_API_KEY=...
# OPENROUTER_API_KEY=...
```

### Install location

`install.sh` symlinks `cc` to `~/.local/bin/cc` by default. Make sure `~/.local/bin` is in your `$PATH`.

## Security notes

- API keys are loaded from a local file — never committed to git
- The `.gitignore` blocks `*.env` and `providers.env`
- The `-p` flag (`--dangerously-skip-permissions`) disables Claude Code's tool confirmation prompts. Only use this if you understand the risks.

## License

MIT
