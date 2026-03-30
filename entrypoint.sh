#!/bin/bash
set -e

TOKEN_DIR="/home/claude/.config/gh-tokens"

if [ -n "${GH_REPO:-}" ] && [ -n "${GH_REPO_PASS:-}" ]; then
  KEY="$(echo "$GH_REPO" | sed 's|/|-|').enc"
  TOKEN_FILE="$TOKEN_DIR/$KEY"

  if [ -f "$TOKEN_FILE" ]; then
    TOKEN=$(openssl enc -d -aes-256-cbc -pbkdf2 \
      -pass pass:"$GH_REPO_PASS" \
      -in "$TOKEN_FILE" 2>/dev/null || echo "")
    if [ -n "$TOKEN" ]; then
      echo "$TOKEN" | gh auth login --with-token
      echo "gh: $GH_REPO で認証しました。"
    else
      echo "Warning: トークンの復号に失敗しました。cc init を再実行してください。" >&2
    fi
  else
    echo "Warning: トークンファイルが見つかりません。cc init を実行してください。" >&2
  fi
fi

# オンボーディング・テーマ選択をスキップ、/workspace を信頼
if [ ! -f "$HOME/.claude.json" ]; then
  echo '{"hasCompletedOnboarding":true,"theme":"dark","projects":{"/workspace":{"hasTrustDialogAccepted":true}}}' > "$HOME/.claude.json"
fi

exec claude --dangerously-skip-permissions