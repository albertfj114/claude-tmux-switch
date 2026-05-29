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
    --kimi)         PROVIDER="kimi";         shift ;;
    --ollama)       PROVIDER="ollama";       shift ;;
    --deepseek)     PROVIDER="deepseek";     shift ;;
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
  --qwen        Qwen (Alibaba Cloud / DashScope, Anthropic-compatible)
  --kimi        Kimi (Moonshot) direct
  --ollama      Ollama local models (auto-detected; needs Anthropic proxy)
  --deepseek    DeepSeek direct (model picker)
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
  "claude-opus-4-7|Claude Opus 4.7        · reasoning · planning"
  "claude-sonnet-4-6|Claude Sonnet 4.6    · coding · agentic · fast"
  "claude-haiku-4-5-20251001|Claude Haiku 4.5  · fast · lightweight"
)

GLM_MODELS=(
  "glm-5.1|GLM 5.1          · reasoning · planning"
  "glm-5.1-turbo|GLM 5.1 Turbo  · coding · balanced"
  "glm-5.1-flash|GLM 5.1 Flash  · fast · lightweight"
  "glm-4-plus|GLM 4 Plus      · coding · general"
  "glm-4-flash|GLM 4 Flash     · fast · cheap"
)

MINIMAX_MODELS=(
  "MiniMax-M2.7|M2.7          · coding · balanced"
  "MiniMax-M2.7-turbo|M2.7 Turbo  · fast · coding"
  "MiniMax-M2.7-highspeed|M2.7 High Speed · throughput"
  "MiniMax-M1|M1              · reasoning · planning"
  "MiniMax-M2.5|M2.5          · general"
  "MiniMax-M2.1|M2.1          · legacy"
)

OPENROUTER_MODELS=(
  "qwen/qwen3.6-plus|Qwen 3.6 Plus         · coding · balanced"
  "deepseek/deepseek-r1-0528|DeepSeek R1        · reasoning · planning"
  "moonshotai/kimi-k2-thinking|Kimi K2 Thinking · reasoning · long-context"
  "deepseek/deepseek-v3.2|DeepSeek V3.2     · coding · fast"
  "qwen/qwen3-coder-plus|Qwen3 Coder Plus   · coding · specialized"
  "moonshotai/kimi-k2.5|Kimi K2.5          · coding · long-context"
  "deepseek/deepseek-v3.2-speciale|DeepSeek V3.2 Speciale · coding"
  "qwen/qwen3-max|Qwen3 Max               · general · capable"
)

QWEN_MODELS=(
  "qwen3.6-plus|Qwen 3.6 Plus      · coding · balanced"
  "qwen3.7-max|Qwen 3.7 Max        · capable · general"
  "qwen3-coder-plus|Qwen3 Coder Plus · coding · specialized"
  "qwen3.6-flash|Qwen 3.6 Flash    · fast · cheap"
)

KIMI_MODELS=(
  "kimi-for-coding|Kimi K2.6  · coding · long-context · 262K context"
)

DEEPSEEK_MODELS=(
  "deepseek-v4-pro|DeepSeek V4 Pro    · reasoning · coding · flagship"
  "deepseek-v4-flash|DeepSeek V4 Flash · fast · cheap"
)

# Fallback shown when Ollama is unreachable; actual list comes from /api/tags
OLLAMA_FALLBACK_MODELS=(
  "llama3.2:latest|Llama 3.2        · general · fast"
  "deepseek-r1:latest|DeepSeek R1   · reasoning · planning"
  "qwen3:latest|Qwen3               · reasoning · coding"
  "qwen2.5-coder:latest|Qwen2.5 Coder · coding · specialized"
  "glm4:latest|GLM4                 · coding · general"
  "phi4:latest|Phi-4                · reasoning · compact"
  "mistral:latest|Mistral           · general · fast"
)

# Provider menu for the interactive picker
PROVIDERS=(
  "anthropic|Anthropic"
  "glm|GLM (z.ai)"
  "minimax|MiniMax"
  "openrouter|OpenRouter"
  "deepseek|DeepSeek"
  "ollama|Ollama (local)"
  "qwen|Qwen (Alibaba Cloud)"
  "kimi|Kimi (Moonshot)"
  "custom|Custom endpoint"
)

# ─── Ollama model discovery ───────────────────────────────────────────────────

fetch_ollama_models() {
  local base="${OLLAMA_BASE_URL:-http://localhost:11434}"
  local raw
  raw="$(curl -sf --max-time 3 "$base/api/tags" 2>/dev/null)" || return 1
  python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    for m in data.get('models', []):
        name = m['name']
        size = m.get('size', 0)
        size_gb = f'{size/1e9:.1f}GB' if size else ''
        label = f'{name} ({size_gb})' if size_gb else name
        print(name + '|' + label)
except:
    pass
" <<< "$raw" 2>/dev/null
}

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

# ─── Tier map display ────────────────────────────────────────────────────────
# Shows how /model inside the session maps to actual provider models.
# Only printed when the provider has a real tier hierarchy (haiku ≠ opus).

print_tier_map() {
  [[ -z "$P_HAIKU_MODEL$P_SONNET_MODEL$P_OPUS_MODEL" ]] && return
  local h="${P_HAIKU_MODEL:-$P_MODEL}"
  local s="${P_SONNET_MODEL:-$P_MODEL}"
  local o="${P_OPUS_MODEL:-$P_MODEL}"
  [[ "$h" == "$s" && "$s" == "$o" ]] && return
  local active="$P_MODEL"
  local mh="" ms="" mo=""
  [[ "$active" == "$h" ]] && mh="  ◀"
  [[ "$active" == "$s" ]] && ms="  ◀"
  [[ "$active" == "$o" ]] && mo="  ◀"
  echo "     /model in-session maps to:" >&2
  printf "       Haiku  →  %s%s\n" "$h" "$mh" >&2
  printf "       Sonnet →  %s%s\n" "$s" "$ms" >&2
  printf "       Opus   →  %s%s\n" "$o" "$mo" >&2
  echo "" >&2
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
P_DISABLE_TOOL_SEARCH=""
P_HAIKU_MODEL=""
P_SONNET_MODEL=""
P_OPUS_MODEL=""
P_EFFORT_LEVEL=""
P_SUBAGENT_MODEL=""
P_NEEDS_PROXY=""
P_THINKING_BUDGET=""

case "$PROVIDER" in
  minimax)
    [[ -z "${MINIMAX_API_KEY:-}" ]] && { echo "Error: MINIMAX_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.minimax.io/anthropic"
    P_AUTH_TOKEN="$MINIMAX_API_KEY"
    P_MODEL="$(pick_model MINIMAX_MODELS "MiniMax")"
    P_HAIKU_MODEL="MiniMax-M2.7-turbo"
    P_SONNET_MODEL="MiniMax-M2.7"
    P_OPUS_MODEL="MiniMax-M1"
    P_DISABLE_TRAFFIC="1"
    ;;

  glm)
    [[ -z "${Z_GLM_API_KEY:-}" ]] && { echo "Error: Z_GLM_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.z.ai/api/anthropic"
    P_AUTH_TOKEN="$Z_GLM_API_KEY"
    P_MODEL="$(pick_model GLM_MODELS "GLM")"
    P_HAIKU_MODEL="glm-5.1-flash"
    P_SONNET_MODEL="glm-5.1-turbo"
    P_OPUS_MODEL="glm-5.1"
    ;;

  openrouter)
    [[ -z "${OPENROUTER_API_KEY:-}" ]] && { echo "Error: OPENROUTER_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://openrouter.ai/api/anthropic"
    P_AUTH_TOKEN="$OPENROUTER_API_KEY"
    P_MODEL="$(pick_model OPENROUTER_MODELS "OpenRouter")"
    ;;

  qwen)
    # Prefer QWEN-API-KEY (standard DashScope/pay-as-you-go key) for the
    # dashscope-intl endpoint. QWEN_API_KEY (sk-sp-* prefix) is a Token/Coding
    # Plan key that needs a different endpoint; used only as a fallback here.
    QWEN_KEY=""
    [[ -n "$ENV_FILE" ]] && QWEN_KEY="$(grep '^QWEN-API-KEY=' "$ENV_FILE" | cut -d= -f2-)"
    [[ -z "$QWEN_KEY" ]] && QWEN_KEY="${QWEN_API_KEY:-}"
    [[ -z "$QWEN_KEY" ]] && { echo "Error: No Qwen key found. Set QWEN-API-KEY or QWEN_API_KEY in $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://dashscope-intl.aliyuncs.com/apps/anthropic"
    P_AUTH_TOKEN="$QWEN_KEY"
    P_MODEL="$(pick_model QWEN_MODELS "Qwen")"
    P_HAIKU_MODEL="qwen3.6-flash"
    P_SONNET_MODEL="qwen3.6-plus"
    P_OPUS_MODEL="qwen3.7-max"
    P_NEEDS_PROXY="1"
    ;;

  kimi)
    KIMI_KEY="$(grep '^KIMI-API-KEY=' "$ENV_FILE" | cut -d= -f2-)"
    [[ -z "$KIMI_KEY" ]] && { echo "Error: KIMI-API-KEY not set in $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.kimi.com/coding/"
    P_AUTH_TOKEN="$KIMI_KEY"
    P_MODEL="$(pick_model KIMI_MODELS "Kimi")"
    P_DISABLE_TOOL_SEARCH="1"
    ;;

  deepseek)
    [[ -z "${DEEPSEEK_API_KEY:-}" ]] && { echo "Error: DEEPSEEK_API_KEY not set. Add it to $ENV_FILE" >&2; exit 1; }
    P_BASE_URL="https://api.deepseek.com/anthropic"
    P_AUTH_TOKEN="$DEEPSEEK_API_KEY"
    P_MODEL="$(pick_model DEEPSEEK_MODELS "DeepSeek")"
    P_HAIKU_MODEL="deepseek-v4-flash"
    P_SONNET_MODEL="deepseek-v4-pro"
    P_OPUS_MODEL="deepseek-v4-pro"
    P_EFFORT_LEVEL="max"
    P_SUBAGENT_MODEL="deepseek-v4-flash"
    P_NEEDS_PROXY="1"
    P_THINKING_BUDGET="16000"
    ;;

  ollama)
    OLLAMA_BASE_URL="${OLLAMA_BASE_URL:-http://localhost:11434}"
    OLLAMA_PROXY_URL="${OLLAMA_PROXY_URL:-}"

    # Try to auto-detect locally pulled models
    OLLAMA_MODELS=()
    while IFS= read -r line; do
      [[ -n "$line" ]] && OLLAMA_MODELS+=("$line")
    done < <(fetch_ollama_models 2>/dev/null)

    if [[ ${#OLLAMA_MODELS[@]} -eq 0 ]]; then
      echo "  Note: Could not reach Ollama at $OLLAMA_BASE_URL — showing preset list." >&2
      OLLAMA_MODELS=("${OLLAMA_FALLBACK_MODELS[@]}")
    fi

    P_MODEL="$(pick_model OLLAMA_MODELS "Ollama")"

    if [[ -z "$OLLAMA_PROXY_URL" ]]; then
      cat >&2 <<'NOTE'

  Ollama needs an Anthropic-compatible proxy to work with Claude Code.
  Quickstart with litellm:

    pip install litellm
    litellm --model ollama/<model> --port 8082

  Then add to your providers.env:
    OLLAMA_PROXY_URL=http://localhost:8082

NOTE
      exit 1
    fi

    P_BASE_URL="$OLLAMA_PROXY_URL"
    P_AUTH_TOKEN="ollama"
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

# ─── Start system-message proxy if needed ────────────────────────────────────
# Providers that don't support the mid-conversation-system beta reject requests
# containing role:system messages injected by Claude Code hooks. This proxy
# strips those messages before forwarding to the real provider endpoint.

PROXY_SCRIPT=""
PROXY_REAL_URL=""
PROXY_PORT=""

if [[ -n "$P_NEEDS_PROXY" && "$PROVIDER" != "anthropic" ]]; then
  PROXY_PORT=$(python3 -c "import socket; s=socket.socket(); s.bind(('',0)); p=s.getsockname()[1]; s.close(); print(p)")
  PROXY_SCRIPT=$(mktemp /tmp/cc-proxy-XXXXXX)
  PROXY_REAL_URL="$P_BASE_URL"
  P_BASE_URL="http://127.0.0.1:$PROXY_PORT"

  cat > "$PROXY_SCRIPT" << 'PROXY_EOF'
import http.server, json, urllib.request, urllib.error, sys, ssl, os

TARGET, TOKEN, PORT = sys.argv[1], sys.argv[2], int(sys.argv[3])
THINKING_BUDGET = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else 0

def make_ssl_ctx():
    ctx = ssl.create_default_context()
    try:
        import certifi
        ctx = ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        cafile = '/etc/ssl/cert.pem'
        if os.path.exists(cafile):
            ctx = ssl.create_default_context(cafile=cafile)
    return ctx

class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(n)
        try:
            d = json.loads(body)
            if 'messages' in d:
                d['messages'] = [m for m in d['messages'] if m.get('role') != 'system']
            if THINKING_BUDGET and 'thinking' in d:
                d['thinking'] = {'type': 'enabled', 'budget_tokens': THINKING_BUDGET}
            body = json.dumps(d).encode()
        except Exception:
            pass
        url = TARGET.rstrip('/') + self.path
        req = urllib.request.Request(url, data=body, method='POST')
        for k, v in self.headers.items():
            kl = k.lower()
            if kl in ('host', 'content-length', 'x-api-key', 'authorization', 'anthropic-auth-token'):
                continue
            if kl == 'anthropic-beta':
                v = ','.join(b.strip() for b in v.split(',')
                             if b.strip() != 'mid-conversation-system-2026-04-07')
                if not v:
                    continue
            req.add_header(k, v)
        req.add_header('Authorization', 'Bearer ' + TOKEN)
        req.add_header('x-api-key', TOKEN)
        req.add_header('Content-Length', str(len(body)))
        try:
            with urllib.request.urlopen(req, context=make_ssl_ctx(), timeout=600) as r:
                self.send_response(r.status)
                for k, v in r.headers.items():
                    if k.lower() in ('transfer-encoding', 'content-encoding'):
                        continue
                    self.send_header(k, v)
                self.end_headers()
                while True:
                    chunk = r.read(4096)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    self.wfile.flush()
        except urllib.error.HTTPError as e:
            b = e.read()
            self.send_response(e.code)
            self.end_headers()
            self.wfile.write(b)
        except Exception as e:
            self.send_response(502)
            self.end_headers()
            self.wfile.write(str(e).encode())
    def log_message(self, *a): pass

http.server.HTTPServer(('127.0.0.1', PORT), H).serve_forever()
PROXY_EOF
fi

# ─── Build the claude command ────────────────────────────────────────────────

CMD="claude"
[[ -n "$SKIP_PERMISSIONS" ]] && CMD="$CMD --dangerously-skip-permissions"
if [[ "$PROVIDER" == "anthropic" && -n "$P_MODEL" ]]; then
  CMD="$CMD --model $P_MODEL"
fi

# ─── Launch ──────────────────────────────────────────────────────────────────

if [[ -n "$SESSION" ]]; then
  if [[ "$PROVIDER" != "anthropic" ]]; then
    local_haiku="${P_HAIKU_MODEL:-$P_MODEL}"
    local_sonnet="${P_SONNET_MODEL:-$P_MODEL}"
    local_opus="${P_OPUS_MODEL:-$P_MODEL}"
    ENV_PREFIX="env ANTHROPIC_API_KEY="
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_BASE_URL=$P_BASE_URL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_AUTH_TOKEN=$P_AUTH_TOKEN"
    ENV_PREFIX="$ENV_PREFIX API_TIMEOUT_MS=$P_TIMEOUT"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_MODEL=$P_MODEL"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_SMALL_FAST_MODEL=$local_haiku"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_HAIKU_MODEL=$local_haiku"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_SONNET_MODEL=$local_sonnet"
    ENV_PREFIX="$ENV_PREFIX ANTHROPIC_DEFAULT_OPUS_MODEL=$local_opus"
    [[ -n "$P_DISABLE_TRAFFIC" ]] && ENV_PREFIX="$ENV_PREFIX CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1"
    [[ -n "$P_DISABLE_TOOL_SEARCH" ]] && ENV_PREFIX="$ENV_PREFIX ENABLE_TOOL_SEARCH=FALSE"
    [[ -n "$P_EFFORT_LEVEL" ]] && ENV_PREFIX="$ENV_PREFIX CLAUDE_CODE_EFFORT_LEVEL=$P_EFFORT_LEVEL"
    [[ -n "$P_SUBAGENT_MODEL" ]] && ENV_PREFIX="$ENV_PREFIX CLAUDE_CODE_SUBAGENT_MODEL=$P_SUBAGENT_MODEL"
    FULL_CMD="$ENV_PREFIX $CMD"
  else
    FULL_CMD="$CMD"
  fi

  if tmux has-session -t "$SESSION" 2>/dev/null; then
    echo "[cc] Attaching to existing session: $SESSION"
    exec tmux attach-session -t "$SESSION"
  else
    echo "[cc] Starting [$PROVIDER] session: $SESSION in $(pwd)"
    print_tier_map
    if [[ -n "$PROXY_SCRIPT" ]]; then
      LAUNCH=$(mktemp /tmp/cc-launch-XXXXXX)
      {
        echo '#!/bin/bash'
        echo "python3 '$PROXY_SCRIPT' '$PROXY_REAL_URL' '$P_AUTH_TOKEN' $PROXY_PORT $P_THINKING_BUDGET &"
        echo '_P=$!'
        echo "trap 'kill \$_P 2>/dev/null; rm -f $PROXY_SCRIPT $LAUNCH' EXIT INT TERM"
        echo 'sleep 0.3'
        echo "$FULL_CMD"
      } > "$LAUNCH"
      exec tmux new-session -s "$SESSION" -c "$PWD" "bash '$LAUNCH'"
    else
      exec tmux new-session -s "$SESSION" -c "$PWD" "$FULL_CMD"
    fi
  fi

else
  echo "[cc] Provider: $PROVIDER${P_MODEL:+ ($P_MODEL)}"
  print_tier_map
  if [[ "$PROVIDER" != "anthropic" ]]; then
    unset ANTHROPIC_API_KEY
    export ANTHROPIC_BASE_URL="$P_BASE_URL"
    export ANTHROPIC_AUTH_TOKEN="$P_AUTH_TOKEN"
    export API_TIMEOUT_MS="$P_TIMEOUT"
    export ANTHROPIC_MODEL="$P_MODEL"
    export ANTHROPIC_SMALL_FAST_MODEL="${P_HAIKU_MODEL:-$P_MODEL}"
    export ANTHROPIC_DEFAULT_HAIKU_MODEL="${P_HAIKU_MODEL:-$P_MODEL}"
    export ANTHROPIC_DEFAULT_SONNET_MODEL="${P_SONNET_MODEL:-$P_MODEL}"
    export ANTHROPIC_DEFAULT_OPUS_MODEL="${P_OPUS_MODEL:-$P_MODEL}"
    [[ -n "$P_DISABLE_TRAFFIC" ]] && export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1
    [[ -n "$P_DISABLE_TOOL_SEARCH" ]] && export ENABLE_TOOL_SEARCH=FALSE
    [[ -n "$P_EFFORT_LEVEL" ]] && export CLAUDE_CODE_EFFORT_LEVEL="$P_EFFORT_LEVEL"
    [[ -n "$P_SUBAGENT_MODEL" ]] && export CLAUDE_CODE_SUBAGENT_MODEL="$P_SUBAGENT_MODEL"
  fi
  if [[ -n "$PROXY_SCRIPT" ]]; then
    python3 "$PROXY_SCRIPT" "$PROXY_REAL_URL" "$P_AUTH_TOKEN" "$PROXY_PORT" "$P_THINKING_BUDGET" &
    _PROXY_PID=$!
    sleep 0.3
    $CMD
    kill $_PROXY_PID 2>/dev/null
    rm -f "$PROXY_SCRIPT"
  else
    exec $CMD
  fi
fi
