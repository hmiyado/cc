#!/bin/bash
set -euo pipefail

SECRETS_DIR="/run/secrets"

# シークレットファイルから読み込み（環境変数には残さない）
_read_secret() {
  local file="$SECRETS_DIR/$1"
  [ -f "$file" ] && cat "$file" || echo ""
}

# Anthropic API キーをセット（op run 経由の env var、またはシークレットファイル）
[ -z "${ANTHROPIC_API_KEY:-}" ] && ANTHROPIC_API_KEY=$(_read_secret anthropic_api_key)
[ -n "$ANTHROPIC_API_KEY" ] && export ANTHROPIC_API_KEY

# GitHub PAT で認証（op run 経由の env var、またはシークレットファイル）
# GH_TOKEN env var があれば gh CLI はそのまま使うので auth login 不要
if [ -n "${GH_REPO:-}" ] && [ -z "${GH_TOKEN:-}" ]; then
  TOKEN=$(_read_secret gh_token)
  if [ -n "$TOKEN" ]; then
    export GH_TOKEN="$TOKEN"
  else
    echo "Warning: トークンが見つかりません。cc init を実行してください。" >&2
  fi
fi

# オンボーディング・テーマ選択をスキップ、/workspace を信頼
if [ ! -f "$HOME/.claude.json" ]; then
  echo '{"hasCompletedOnboarding":true,"theme":"dark","projects":{"/workspace":{"hasTrustDialogAccepted":true}}}' > "$HOME/.claude.json"
fi

# ターミナルサイズを PTY に反映（tmux など環境でレイアウト崩れを防ぐ）
if [ -n "${COLUMNS:-}" ] && [ -n "${LINES:-}" ]; then
  stty cols "$COLUMNS" rows "$LINES" 2>/dev/null || true
fi

exec claude --dangerously-skip-permissions "$@"
