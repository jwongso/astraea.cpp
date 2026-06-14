#!/usr/bin/env bash
# Launch astraea.cpp nz_tenancy on port 8003.
# Run from any directory; paths are resolved relative to this script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BINARY="$REPO_DIR/build/apps/nz_tenancy/nz_tenancy"
VENV="$HOME/proj/priv/astraea/.venv/bin/python"

if [[ ! -x "$BINARY" ]]; then
    echo "Binary not found: $BINARY"
    echo "Run: cmake --build $REPO_DIR/build -j\$(nproc)"
    exit 1
fi

# ---- embed proxy -------------------------------------------------------
# Start on port 8081 if nothing is already listening there.
if ! ss -tlnp 2>/dev/null | grep -q ':8081 '; then
    echo "Starting embed proxy on port 8081..."
    "$VENV" "$SCRIPT_DIR/embed_proxy.py" --port 8081 &
    EMBED_PID=$!
    echo "Embed proxy PID: $EMBED_PID"
    # Wait until it responds
    for i in $(seq 1 30); do
        sleep 1
        if curl -sf http://127.0.0.1:8081/health >/dev/null 2>&1; then
            echo "Embed proxy ready."
            break
        fi
        if [[ $i -eq 30 ]]; then
            echo "Embed proxy did not start in time."
            kill "$EMBED_PID" 2>/dev/null || true
            exit 1
        fi
    done
else
    echo "Port 8081 already in use, assuming embed proxy is running."
fi

# ---- astraea.cpp -------------------------------------------------------
export LLM_BASE_URL="http://localhost:8080/v1"
export EMBED_BASE_URL="http://localhost:8081/v1"
export QDRANT_URL="http://localhost:6333"
export REDIS_URL="redis://127.0.0.1:6379/0"

export LLM_MODEL="Qwen_Qwen3-8B-Q5_K_M.gguf"
export EMBED_MODEL="nomic-ai/nomic-embed-text-v1.5"
export EMBED_DIMS="768"

export PUBLIC_TOKEN="Oqt3jfJtpY89VYVpVZITG-obkOd-cmgS"
export DEBUG_KEY="nz2023"
export ALLOWED_ORIGIN="*"

export ENABLE_RERANKER="false"
export ENABLE_THINKING="false"
export LLM_GLOBAL_CONCURRENCY="1"
export COORDINATOR_BACKEND="in_process"

export PORT="8010"
export FEEDBACK_DIR="$REPO_DIR/data"

echo ""
echo "Starting astraea.cpp nz_tenancy on port $PORT"
echo "  LLM:    $LLM_BASE_URL  model=$LLM_MODEL"
echo "  Embed:  $EMBED_BASE_URL  model=$EMBED_MODEL  dims=$EMBED_DIMS"
echo "  Qdrant: $QDRANT_URL"
echo "  Redis:  $REDIS_URL"
echo ""

exec "$BINARY"
