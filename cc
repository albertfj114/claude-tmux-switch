#!/usr/bin/env bash
set -euo pipefail

# cc — Claude Code launcher with multi-provider switching + tmux sessions
#
# Launches Claude Code with different AI providers (Anthropic, MiniMax, GLM,
# OpenRouter, etc.) and optionally wraps the session in tmux.
#
# Usage:
#   cc                              # Anthropic (default), run directly
#   cc work                         # Anthropic, in tmux session "work"
#   cc --glm                        # GLM-5.1, run directly
#   cc --glm mod2                   # GLM, in tmux session "mod2"
#   cc --minimax debug              # MiniMax, in tmux session "debug"
#   cc --openrouter refactor        # OpenRouter, in tmux session "refactor"
#   cc --glm mod2 -m glm-4         # GLM with model override
#   cc -h                           # Show help

# ─── Configuration ───────────────────────────────────────────────────────────

# Path to your API keys file (see .env.example for format)
ENV_FILE="${CC_ENV_FILE:-$HOME/.config/cc/providers.env}"

# ─── Load API keys ───────────────────────────────────────────────────────────

if [[ -f "$ENV_FILE" ]]; then
  while IFS='=' read -r key val; do
    # Skip comments, blank lines, and invalid bash variable names
    [[ "$key" =~ ^[a-zA-Z_][a-zA-Z0-9_]*$ ]] || continue
    [[ -z "$val" ]] && continue
    export "$key=$val"
  done < "$ENV_FILE"
fi

# ─── Parse arguments ─────────────────────────────────────────────────────────

PROVIDER="anthropic"
MODEL=""
SESSION=""
SKIP_PERMISSIONS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --minimax)      PROVIDER="minimax";      shift ;;
    --glm)          PROVIDER="glm";          shift ;;
    --openrouter)   PROVIDER="openrouter";   shift ;;
    --deepseek)     PROVIDER="deepseek";     shift ;;
    --custom)       PROVIDER="custom";       shift ;;
    -m|--model)     MODEL="$2";              shift 2 ;;
    -p|--skip-permissions) SKIP_PERMISSIONS="1"; shift ;;
    -h|--help)
      cat <<'HELP'
cc — Claude Code launcher with multi-provider switching + tmux sessions

Usage: cc [provider] [session-name] [-m model] [-p]

Providers:
  (default)     Anthropic (uses ANTHROPIC_API_KEY)
  --minimax     MiniMax M2.7
  --glm         GLM-5.1 via z.ai
  --openrouter  OpenRouter (any model)
  --deepseek    DeepSeek (OpenAI format — experimental)
  --custom      Custom endpoint (set CC_CUSTOM_* vars)

Options:
  session-name    Opens a named tmux session (attaches if it exists)
  -m, --model     Override the default model for the provider
  -p, --skip-permissions
                  Enable --dangerously-skip-permissions flag
  -h, --help      Show this help message

Setup:
  1. Copy .env.example to ~/.config/cc/providers.env
  2. Fill in your API keys
  3. Run: cc

Examples:
  cc                            # Anthropic, current terminal
  cc --glm coding               # GLM in tmux session "coding"
  cc --openrouter -m claude-sonnet-4-20250514
  cc --minimax debug -p         # MiniMax, skip permissions, session "debug"
HELP
      exit 0
      ;;
    *)
      [[ -z "$SESSION" ]] && SESSION="$1"
      shift
      ;;
  esac
done

# ─── Provider config ─────────────────────────────────────────────────────────

P_BASE_URL=""
P_AUTH_TOKEN=""
P_MODEL=""
P_TIMEOUT="300000"
P_DISABLE_TRAFFIC=""
P_OPENAI_FORMAT=""

case "$PROVIDER" in
  minimax)
    [[ -z "${MINIMAX_API_KEY:-}" ]] && { echo "Error: MINIMAX_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.minimax.io/anthropic"
    P_AUTH_TOKEN="$MINIMAX_API_KEY"
    P_MODEL="${MODEL:-MiniMax-M2.7}"
    P_DISABLE_TRAFFIC="1"
    ;;

  glm)
    [[ -z "${Z_GLM_API_KEY:-}" ]] && { echo "Error: Z_GLM_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.z.ai/api/anthropic"
    P_AUTH_TOKEN="$Z_GLM_API_KEY"
    P_MODEL="${MODEL:-glm-5.1}"
    ;;

  openrouter)
    [[ -z "${OPENROUTER_API_KEY:-}" ]] && { echo "Error: OPENROUTER_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://openrouter.ai/api/anthropic"
    P_AUTH_TOKEN="$OPENROUTER_API_KEY"
    P_MODEL="${MODEL:-anthropic/claude-sonnet-4-20250514}"
    ;;

  deepseek)
    # DeepSeek uses OpenAI format — may not work with Claude Code
    [[ -z "${DEEPSEEK_API_KEY:-}" ]] && { echo "Error: DEEPSEEK_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    echo "Warning: DeepSeek uses OpenAI format — may not work with Claude Code" >&2
    P_BASE_URL="https://api.deepseek.com"
    P_AUTH_TOKEN="$DEEPSEEK_API_KEY"
    P_MODEL="${MODEL:-deepseek-chat}"
    P_OPENAI_FORMAT="1"
    ;;

  custom)
    # Use CC_CUSTOM_BASE_URL, CC_CUSTOM_API_KEY, CC_CUSTOM_MODEL
    P_BASE_URL="${CC_CUSTOM_BASE_URL:-}"
    P_AUTH_TOKEN="${CC_CUSTOM_API_KEY:-}"
    P_MODEL="${CC_CUSTOM_MODEL:-${MODEL:-}}"
    [[ -z "$P_BASE_URL" ]] && { echo "Error: CC_CUSTOM_BASE_URL not set" >&2; exit 1; }
    [[ -z "$P_AUTH_TOKEN" ]] && { echo "Error: CC_CUSTOM_API_KEY not set" >&2; exit 1; }
    [[ -z "$P_MODEL" ]] && { echo "Error: No model specified. Use -m or set CC_CUSTOM_MODEL" >&2; exit 1; }
    ;;

  anthropic)
    # Uses whatever ANTHROPIC_API_KEY is already set in the environment
    ;;
esac

# ─── Build the claude command ────────────────────────────────────────────────

CMD="claude"
[[ -n "$SKIP_PERMISSIONS" ]] && CMD="$CMD --dangerously-skip-permissions"

# For anthropic, pass model as a CLI flag; for others it goes in env vars
if [[ "$PROVIDER" == "anthropic" && -n "$MODEL" ]]; then
  CMD="$CMD --model $MODEL"
fi

# ─── Launch ──────────────────────────────────────────────────────────────────

if [[ -n "$SESSION" ]]; then
  # Build an env-prefixed command for tmux
  if [[ "$PROVIDER" != "anthropic" ]]; then
    ENV_PREFIX="env"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_BASE_URL=$P_BASE_URL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_AUTH_TOKEN=$P_AUTH_TOKEN"
    ENV_PREFIX="$ENV_PREFIX API_TIMEOUT_MS=$P_TIMEOUT"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_MODEL=$P_MODEL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_SMALL_FAST_MODEL=$P_MODEL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_SONNET_MODEL=$P_MODEL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_OPUS_MODEL=$P_MODEL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_HAIKU_MODEL=$P_MODEL"
    [[ -n "$P_DISABLE_TRAFFIC" ]] && ENV_PREFIX="$ENV_PREFIX CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1"
    FULL_CMD="$ENV_PREFIX $CMD"
  else
    FULL_CMD="$CMD"
  fi

  if tmux has-session -t "$SESSION" 2>/dev/null; then
    echo "[cc] Attaching to existing session: $SESSION"
    exec tmux attach-session -t "$SESSION"
  else
    echo "[cc] Starting [$PROVIDER] session: $SESSION in $(pwd)"
    exec tmux new-session -s "$SESSION" -c "$PWD" "$FULL_CMD"
  fi

else
  # No session name — run directly in current terminal
  echo "[cc] Provider: $PROVIDER${P_MODEL:+ ($P_MODEL)}"
  if [[ "$PROVIDER" != "anthropic" ]]; then
    unset ANTHROPIC_API_KEY  # Prevent conflicts with the default key
    export ANTHROPIC_BASE_URL="$P_BASE_URL"
    export ANTHROPIC_AUTH_TOKEN="$P_AUTH_TOKEN"
    export API_TIMEOUT_MS="$P_TIMEOUT"
    export ANTHROPIC_MODEL="$P_MODEL"
    export ANTHROPIC_SMALL_FAST_MODEL="$P_MODEL"
    export ANTHROPIC_DEFAULT_SONNET_MODEL="$P_MODEL"
    export ANTHROPIC_DEFAULT_OPUS_MODEL="$P_MODEL"
    export ANTHROPIC_DEFAULT_HAIKU_MODEL="$P_MODEL"
    [[ -n "$P_DISABLE_TRAFFIC" ]] && export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1
  fi
  exec $CMD
fi
