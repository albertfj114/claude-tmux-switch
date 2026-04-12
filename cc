#!/usr/bin/env bash
set -euo pipefail

# cc — Claude Code launcher with multi-provider switching + tmux sessions
#
# Launches Claude Code with different AI providers and optional tmux sessions.
# Run without arguments to get an interactive provider + model picker.
#
# Usage:
#   cc                              # Pick provider + model from menus
#   cc --glm                        # GLM, pick model from menu
#   cc --glm mod2                   # GLM, pick model, tmux session "mod2"
#   cc --glm -m glm-5.1             # GLM, skip menu, use glm-5.1 directly
#   cc --minimax work               # MiniMax, pick model, tmux session "work"
#   cc --openrouter                 # OpenRouter, pick model from menu
#   cc -p --glm coding              # Skip permissions, GLM, tmux "coding"
#   cc -h                           # Show help

# ─── Load API keys ───────────────────────────────────────────────────────────
# Set CC_ENV_FILE to your keys file, or it checks standard locations.
ENV_FILE="${CC_ENV_FILE:-}"
if [[ -z "$ENV_FILE" ]]; then
  for candidate in \
    "$HOME/.config/cc/providers.env" \
    "$HOME/Documents/Projects/.env"; do
    if [[ -f "$candidate" ]]; then
      ENV_FILE="$candidate"
      break
    fi
  done
fi

if [[ -n "$ENV_FILE" && -f "$ENV_FILE" ]]; then
  while IFS='=' read -r key val; do
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
    --qwen)         PROVIDER="qwen";         shift ;;
    --custom)       PROVIDER="custom";       shift ;;
    -m|--model)     MODEL="$2";              shift 2 ;;
    -p|--skip-permissions) SKIP_PERMISSIONS="1"; shift ;;
    -h|--help)
      cat <<'HELP'
cc — Claude Code launcher with multi-provider switching + tmux sessions

Usage: cc [provider] [session-name] [-m model] [-p]

Providers:
  (default)     Pick from all providers interactively
  --anthropic   Anthropic (Claude models)
  --minimax     MiniMax (model picker)
  --glm         GLM via z.ai (model picker)
  --openrouter  OpenRouter — access 100+ models (model picker)
  --qwen        Qwen direct (experimental — OpenAI format)
  --custom      Custom endpoint (set CC_CUSTOM_* env vars)

Options:
  session-name    Opens a named tmux session (attaches if exists)
  -m, --model     Skip model picker, use MODEL directly
  -p              Enable --dangerously-skip-permissions
  -h, --help      Show this help message

Environment:
  CC_ENV_FILE          Path to API keys file (default: ~/.config/cc/providers.env)

Setup:
  1. Copy providers.env.example to ~/.config/cc/providers.env
  2. Fill in your API keys
  3. Run: cc
HELP
      exit 0
      ;;
    *)
      [[ -z "$SESSION" ]] && SESSION="$1"
      shift
      ;;
  esac
done

# ─── Model definitions per provider ──────────────────────────────────────────
# Format: "model_id|Display Name"
# First entry is the default when user presses Enter

ANTHROPIC_MODELS=(
  "claude-sonnet-4-20250514|Claude Sonnet 4 (default)"
  "claude-opus-4-20250514|Claude Opus 4 (most capable)"
  "claude-haiku-4-5-20251001|Claude Haiku 4.5 (fast)"
)

GLM_MODELS=(
  "glm-5.1|GLM 5.1 (latest)"
  "glm-5.1-flash|GLM 5.1 Flash (fast)"
  "glm-5.1-turbo|GLM 5.1 Turbo"
  "glm-4-plus|GLM 4 Plus"
  "glm-4-flash|GLM 4 Flash (fast)"
)

MINIMAX_MODELS=(
  "MiniMax-M2.7-turbo|M2.7 Turbo (fast)"
  "MiniMax-M2.7|M2.7 (latest)"
  "MiniMax-M2.7-highspeed|M2.7 High Speed"
  "MiniMax-M2.5|M2.5"
  "MiniMax-M2.1|M2.1"
  "MiniMax-M1|M1 (reasoning)"
)

OPENROUTER_MODELS=(
  "qwen/qwen3.6-plus|Qwen 3.6 Plus"
  "deepseek/deepseek-v3.2|DeepSeek V3.2"
  "deepseek/deepseek-v3.2-speciale|DeepSeek V3.2 Speciale"
  "moonshotai/kimi-k2.5|Kimi K2.5"
  "moonshotai/kimi-k2-thinking|Kimi K2 Thinking"
  "deepseek/deepseek-r1-0528|DeepSeek R1 (reasoning)"
  "qwen/qwen3-max|Qwen 3 Max"
  "qwen/qwen3-coder-plus|Qwen 3 Coder Plus"
)

# Provider menu for the interactive picker
PROVIDERS=(
  "anthropic|Anthropic"
  "glm|GLM (z.ai)"
  "minimax|MiniMax"
  "openrouter|OpenRouter"
  "qwen|Qwen (experimental)"
  "custom|Custom endpoint"
)

# ─── Interactive pickers ─────────────────────────────────────────────────────

pick_provider() {
  echo "" >&2
  echo "  Pick a provider:" >&2
  echo "" >&2

  local i=1
  for entry in "${PROVIDERS[@]}"; do
    local name="${entry#*|}"
    if [[ $i -eq 1 ]]; then
      printf "    %s) %-30s [default]\n" "$i" "$name" >&2
    else
      printf "    %s) %s\n" "$i" "$name" >&2
    fi
    ((i++))
  done

  echo "" >&2
  printf "  Enter choice [1]: " >&2
  read -r choice < /dev/tty

  [[ -z "$choice" ]] && choice=1

  if ! [[ "$choice" =~ ^[0-9]+$ ]] || (( choice < 1 || choice > ${#PROVIDERS[@]} )); then
    echo "  Invalid choice, using Anthropic." >&2
    choice=1
  fi

  local selected="${PROVIDERS[$((choice-1))]}"
  echo "${selected%%|*}"
}

pick_model() {
  local array_name="$1"
  local provider_name="$2"

  # If user passed -m, skip the picker
  if [[ -n "$MODEL" ]]; then
    echo "$MODEL"
    return
  fi

  echo "" >&2
  echo "  [$provider_name] Pick a model:" >&2
  echo "" >&2

  local i=1 count
  eval "count=\${#$array_name[@]}"

  while (( i <= count )); do
    eval "local entry=\"\${$array_name[\$((i-1))]}\""
    local name="${entry#*|}"
    if [[ $i -eq 1 ]]; then
      printf "    %s) %-35s [default]\n" "$i" "$name" >&2
    else
      printf "    %s) %s\n" "$i" "$name" >&2
    fi
    ((i++))
  done

  echo "" >&2
  printf "  Enter choice [1]: " >&2
  read -r choice < /dev/tty

  # Default to first option
  [[ -z "$choice" ]] && choice=1

  # Validate
  if ! [[ "$choice" =~ ^[0-9]+$ ]] || (( choice < 1 || choice > count )); then
    echo "  Invalid choice, using default." >&2
    choice=1
  fi

  eval "local selected=\"\${$array_name[\$((choice-1))]}\""
  local selected_name="${selected#*|}"
  local selected_id="${selected%%|*}"

  echo "  Using: $selected_name" >&2
  echo "" >&2
  echo "$selected_id"
}

# ─── No provider flag? Show provider picker ──────────────────────────────────

if [[ "$PROVIDER" == "anthropic" && -z "$MODEL" ]]; then
  PROVIDER="$(pick_provider)"
fi

# ─── Provider config ─────────────────────────────────────────────────────────

P_BASE_URL=""
P_AUTH_TOKEN=""
P_MODEL=""
P_TIMEOUT="300000"
P_DISABLE_TRAFFIC=""

case "$PROVIDER" in
  minimax)
    [[ -z "${MINIMAX_API_KEY:-}" ]] && { echo "Error: MINIMAX_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.minimax.io/anthropic"
    P_AUTH_TOKEN="$MINIMAX_API_KEY"
    P_MODEL="$(pick_model MINIMAX_MODELS "MiniMax")"
    P_DISABLE_TRAFFIC="1"
    ;;

  glm)
    [[ -z "${Z_GLM_API_KEY:-}" ]] && { echo "Error: Z_GLM_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.z.ai/api/anthropic"
    P_AUTH_TOKEN="$Z_GLM_API_KEY"
    P_MODEL="$(pick_model GLM_MODELS "GLM")"
    ;;

  openrouter)
    [[ -z "${OPENROUTER_API_KEY:-}" ]] && { echo "Error: OPENROUTER_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://openrouter.ai/api/anthropic"
    P_AUTH_TOKEN="$OPENROUTER_API_KEY"
    P_MODEL="$(pick_model OPENROUTER_MODELS "OpenRouter")"
    ;;

  qwen)
    # QWEN-API-KEY has a dash so it can't be a bash var — read it directly from the file
    QWEN_KEY="$(grep '^QWEN-API-KEY=' "$ENV_FILE" | cut -d= -f2-)"
    [[ -z "$QWEN_KEY" ]] && { echo "Error: QWEN-API-KEY not set in $ENV_FILE" >&2; exit 1; }
    echo "Warning: Qwen uses OpenAI format — may not work with Claude Code" >&2
    P_BASE_URL="https://dashscope-intl.aliyuncs.com/compatible-mode/v1"
    P_AUTH_TOKEN="$QWEN_KEY"
    P_MODEL="${MODEL:-qwen-plus}"
    ;;

  custom)
    P_BASE_URL="${CC_CUSTOM_BASE_URL:-}"
    P_AUTH_TOKEN="${CC_CUSTOM_API_KEY:-}"
    P_MODEL="${CC_CUSTOM_MODEL:-${MODEL:-}}"
    [[ -z "$P_BASE_URL" ]] && { echo "Error: CC_CUSTOM_BASE_URL not set" >&2; exit 1; }
    [[ -z "$P_AUTH_TOKEN" ]] && { echo "Error: CC_CUSTOM_API_KEY not set" >&2; exit 1; }
    [[ -z "$P_MODEL" ]] && { echo "Error: No model specified. Use -m or set CC_CUSTOM_MODEL" >&2; exit 1; }
    ;;

  anthropic)
    P_MODEL="$(pick_model ANTHROPIC_MODELS "Anthropic")"
    ;;
esac

# ─── Build the claude command ────────────────────────────────────────────────

CMD="claude"
[[ -n "$SKIP_PERMISSIONS" ]] && CMD="$CMD --dangerously-skip-permissions"
if [[ "$PROVIDER" == "anthropic" && -n "$P_MODEL" ]]; then
  CMD="$CMD --model $P_MODEL"
fi

# ─── Launch ──────────────────────────────────────────────────────────────────

if [[ -n "$SESSION" ]]; then
  if [[ "$PROVIDER" != "anthropic" ]]; then
    ENV_PREFIX="env ANTHROPIC_API_KEY="
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
  echo "[cc] Provider: $PROVIDER${P_MODEL:+ ($P_MODEL)}"
  if [[ "$PROVIDER" != "anthropic" ]]; then
    unset ANTHROPIC_API_KEY
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
