#!/usr/bin/env bash
set -euo pipefail

DOCKER_IMAGE="my-claude-code"
OP_VAULT="CC"
OP_ITEM_PREFIX="claude-docker"
OP_GLOBAL_ITEM="claude-docker-global"
TOKEN_VOLUME="claude-gh-tokens"

# ---------------------------------------------------------------------------
# ヘルパー
# ---------------------------------------------------------------------------

_repo_from_remote() {
  git remote get-url origin 2>/dev/null \
    | sed 's|git@github.com:||;s|https://github.com/||;s|\.git$||' \
    || echo ""
}

_token_file_key() {
  local repo="$1"
  echo "$(echo "$repo" | sed 's|/|-|').enc"
}

_op_ref() {
  local repo="$1"
  echo "op://${OP_VAULT}/${OP_ITEM_PREFIX}/${repo}/password"
}

_require_op() {
  if ! command -v op &>/dev/null; then
    echo "Error: 1Password CLI (op) が見つかりません。" >&2
    exit 1
  fi
  if ! op account list &>/dev/null; then
    echo "Error: 1Password にサインインしてください: op signin" >&2
    exit 1
  fi
}

_require_repo() {
  local repo
  repo=$(_repo_from_remote)
  if [ -z "$repo" ]; then
    echo "Error: git remote origin が見つかりません。" >&2
    exit 1
  fi
  echo "$repo"
}

# ---------------------------------------------------------------------------
# サブコマンド
# ---------------------------------------------------------------------------

cmd_init_global() {
  _require_op

  local exists=false
  op item get "$OP_GLOBAL_ITEM" --vault "$OP_VAULT" &>/dev/null && exists=true

  if $exists; then
    echo "既に登録済みです。上書きしますか？ [y/N] "
    read -r ans
    [ "${ans:-N}" = "y" ] || return
  fi

  echo "Anthropic API キーを入力してください:"
  echo "  https://console.anthropic.com/settings/keys"
  echo ""
  read -rsp "API キー: " api_key
  echo ""

  if $exists; then
    op item edit "$OP_GLOBAL_ITEM" --vault "$OP_VAULT" "password=$api_key" > /dev/null
  else
    op item create \
      --category login \
      --title "$OP_GLOBAL_ITEM" \
      --vault "$OP_VAULT" \
      "password=$api_key" > /dev/null
  fi

  echo "登録しました: op://${OP_VAULT}/${OP_GLOBAL_ITEM}/password"
}

cmd_init() {
  if [ "${1:-}" = "-g" ]; then
    cmd_init_global
    return
  fi

  _require_op
  local remote_repo
  remote_repo=$(_repo_from_remote)
  local repo
  if [ -z "$remote_repo" ]; then
    repo="$(basename "$(pwd)")"
  else
    repo="$remote_repo"
  fi
  local op_ref
  op_ref=$(_op_ref "$repo")

  echo "リポジトリ: $repo"
  echo ""

  if ! op item get "${OP_ITEM_PREFIX}/${repo}" --vault "$OP_VAULT" &>/dev/null; then
    echo "1Password にパスフレーズを登録します..."
    local pass
    pass=$(openssl rand -base64 32)
    op item create \
      --category login \
      --title "${OP_ITEM_PREFIX}/${repo}" \
      --vault "$OP_VAULT" \
      "password=${pass}"
    echo "登録しました: ${op_ref}"
  else
    echo "1Password に登録済みです: ${op_ref}"
  fi

  if [ -z "$remote_repo" ]; then
    echo "GitHub リポジトリと紐づいていないため、PAT の登録をスキップします。"
    return
  fi

  echo ""
  echo "GitHub Fine-grained PAT を発行してください:"
  echo "  https://github.com/settings/personal-access-tokens/new"
  echo "  対象リポジトリ: $repo"
  echo ""
  read -rsp "トークンを貼り付けてください: " token
  echo ""

  local pass
  pass=$(op read "$op_ref")
  local key
  key=$(_token_file_key "$repo")

  docker run --rm \
    -v "${TOKEN_VOLUME}:/tokens" \
    -e PASS="$pass" \
    -e TOKEN="$token" \
    -e KEY="$key" \
    alpine sh -c \
      'echo -n "$TOKEN" | openssl enc -aes-256-cbc -pbkdf2 -pass env:PASS -out "/tokens/$KEY" && chmod 600 "/tokens/$KEY"'

  echo "保存しました: $repo"
}

cmd_run() {
  _require_op
  local use_api_key=false
  if [ "${1:-}" = "--api-key" ]; then
    use_api_key=true
  fi

  local repo
  repo=$(_repo_from_remote)

  local anthropic_key=""
  if $use_api_key; then
    anthropic_key=$(op read "op://${OP_VAULT}/${OP_GLOBAL_ITEM}/password" 2>/dev/null || echo "")
    if [ -z "$anthropic_key" ]; then
      echo "Error: Anthropic API キーが未登録です。cc init -g を実行してください。" >&2
      exit 1
    fi
  fi

  local pass=""
  if [ -n "$repo" ]; then
    local op_ref
    op_ref=$(_op_ref "$repo")
    pass=$(op read "$op_ref" 2>/dev/null || echo "")
    if [ -z "$pass" ]; then
      echo "Warning: $repo は未登録です。cc init を実行してください。" >&2
      echo "認証なしで起動します。" >&2
    fi
  fi

  local tty_flags="-i"
  [ -t 0 ] && tty_flags="-it"

  docker run $tty_flags --rm \
    -v "$(pwd)":/workspace \
    -v ~/.claude:/home/claude/.claude \
    -v ~/.gstack:/home/claude/.gstack \
    -v "${TOKEN_VOLUME}:/home/claude/.config/gh-tokens" \
    ${anthropic_key:+-e ANTHROPIC_API_KEY="$anthropic_key"} \
    ${pass:+-e GH_REPO_PASS="$pass"} \
    ${repo:+-e GH_REPO="$repo"} \
    "$DOCKER_IMAGE"
}

cmd_list() {
  echo "登録済みリポジトリ:"
  op item list --vault "$OP_VAULT" --format json \
    | jq -r '.[].title' \
    | grep "^${OP_ITEM_PREFIX}/" \
    | sed "s|^${OP_ITEM_PREFIX}/||"
}

cmd_revoke() {
  _require_op
  local repo
  repo=$(_repo_from_remote)
  if [ -z "$repo" ]; then
    repo="$(basename "$(pwd)")"
  fi
  local key
  key=$(_token_file_key "$repo")

  docker run --rm \
    -v "${TOKEN_VOLUME}:/tokens" \
    alpine sh -c "rm -f '/tokens/$key'"

  op item delete "${OP_ITEM_PREFIX}/${repo}" --vault "$OP_VAULT" 2>/dev/null || true

  echo "削除しました: $repo"
}

# ---------------------------------------------------------------------------
# エントリーポイント
# ---------------------------------------------------------------------------

case "${1:-run}" in
  init)   cmd_init "${2:-}" ;;
  run)    cmd_run ;;
  list)   cmd_list ;;
  revoke) cmd_revoke ;;
  *)      echo "Usage: cc [init [-g]|run [--api-key]|list|revoke]" >&2; exit 1 ;;
esac