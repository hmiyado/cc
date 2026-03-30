#!/bin/bash
set -e

TOKEN_DIR="/home/claude/.config/gh-tokens"
SECRETS_DIR="/run/secrets"

# シークレットファイルから読み込み（環境変数には残さない）
_read_secret() {
  local file="$SECRETS_DIR/$1"
  [ -f "$file" ] && cat "$file" || echo ""
}

# Anthropic API キーをセット（Claude Code が参照する環境変数）
ANTHROPIC_API_KEY=$(_read_secret anthropic_api_key)
[ -n "$ANTHROPIC_API_KEY" ] && export ANTHROPIC_API_KEY

# GitHub PAT の復号と認証
if [ -n "${GH_REPO:-}" ]; then
  if [ -f "$SECRETS_DIR/gh_repo_pass" ]; then
    KEY="$(echo "$GH_REPO" | sed 's|/|-|').enc"
    TOKEN_FILE="$TOKEN_DIR/$KEY"

    if [ -f "$TOKEN_FILE" ]; then
      TOKEN=$(openssl enc -d -aes-256-gcm -pbkdf2 -iter 600000 \
        -pass file:"$SECRETS_DIR/gh_repo_pass" \
        -in "$TOKEN_FILE" 2>/dev/null || echo "")
      if [ -n "$TOKEN" ]; then
        gh auth login --with-token <<< "$TOKEN"
        echo "gh: $GH_REPO で認証しました。"
      else
        echo "Warning: トークンの復号に失敗しました。cc init を再実行してください。" >&2
      fi
    else
      echo "Warning: トークンファイルが見つかりません。cc init を実行してください。" >&2
    fi
  fi
fi

# オンボーディング・テーマ選択をスキップ、/workspace を信頼
if [ ! -f "$HOME/.claude.json" ]; then
  echo '{"hasCompletedOnboarding":true,"theme":"dark","projects":{"/workspace":{"hasTrustDialogAccepted":true}}}' > "$HOME/.claude.json"
fi

exec claude --dangerously-skip-permissions
