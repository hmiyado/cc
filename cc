#!/usr/bin/env bash
set -euo pipefail

DOCKER_IMAGE="my-claude-code"
OP_VAULT="CC"
OP_ITEM_PREFIX="containered-claude"
OP_GLOBAL_ITEM="containered-claude-global"

KEYCHAIN_SERVICE="cc"
CC_REGISTRY="${HOME}/.cc_registry"
CC_CONFIG="${HOME}/.cc_config"
SECRETS_BACKEND=""  # _load_backend or _select_backend で設定

# ---------------------------------------------------------------------------
# ヘルパー
# ---------------------------------------------------------------------------

_repo_from_remote() {
  git remote get-url origin 2>/dev/null \
    | sed 's|git@github.com:||;s|https://github.com/||;s|\.git$||' \
    || echo ""
}

# init 時に呼ぶ: 未設定なら対話選択してコンフィグに保存
_select_backend() {
  if [ -n "${CC_SECRETS_BACKEND:-}" ]; then
    SECRETS_BACKEND="$CC_SECRETS_BACKEND"; return
  fi
  if [ -f "$CC_CONFIG" ]; then
    SECRETS_BACKEND=$(grep '^SECRETS_BACKEND=' "$CC_CONFIG" | cut -d= -f2)
    [ -n "$SECRETS_BACKEND" ] && return
  fi
  echo "シークレットのバックエンドを選択してください:"
  echo "  1) 1Password"
  echo "  2) keychain (macOS)"
  read -rp "選択 [1]: " _choice
  case "${_choice:-1}" in
    1) SECRETS_BACKEND="1password" ;;
    2) SECRETS_BACKEND="keychain" ;;
    *) echo "Error: 1 または 2 を入力してください。" >&2; exit 1 ;;
  esac
  echo "SECRETS_BACKEND=${SECRETS_BACKEND}" > "$CC_CONFIG"
  chmod 600 "$CC_CONFIG"
  echo "保存しました: ${CC_CONFIG} (backend=${SECRETS_BACKEND})"
  echo ""
}

# run/list/revoke 時に呼ぶ: コンフィグ or デフォルト
_load_backend() {
  [ -n "${CC_SECRETS_BACKEND:-}" ] && { SECRETS_BACKEND="$CC_SECRETS_BACKEND"; return; }
  if [ -f "$CC_CONFIG" ]; then
    SECRETS_BACKEND=$(grep '^SECRETS_BACKEND=' "$CC_CONFIG" | cut -d= -f2)
    [ -n "$SECRETS_BACKEND" ] && return
  fi
  SECRETS_BACKEND="1password"
}

_require_backend() {
  case "$SECRETS_BACKEND" in
    1password)
      if ! command -v op &>/dev/null; then
        echo "Error: 1Password CLI (op) が見つかりません。" >&2; exit 1
      fi
      if ! op account list &>/dev/null; then
        echo "Error: 1Password にサインインしてください: op signin" >&2; exit 1
      fi
      ;;
    keychain)
      if ! command -v security &>/dev/null; then
        echo "Error: security コマンドが見つかりません（macOS 専用）。" >&2; exit 1
      fi
      ;;
    *)
      echo "Error: CC_SECRETS_BACKEND の値が不正です: $SECRETS_BACKEND (1password|keychain)" >&2; exit 1
      ;;
  esac
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
# シークレット抽象化レイヤー
# key: "repo:<owner>/<name>" または "global"
# ---------------------------------------------------------------------------

_op_item_name() {
  local key="$1"
  case "$key" in
    global)  echo "$OP_GLOBAL_ITEM" ;;
    repo:*)  echo "${OP_ITEM_PREFIX}--${key#repo:}" | tr '/' '-' ;;
  esac
}

_secret_write() {
  local key="$1" value="$2"
  case "$SECRETS_BACKEND" in
    1password)
      local item; item=$(_op_item_name "$key")
      if op item get "$item" --vault "$OP_VAULT" &>/dev/null; then
        op item edit "$item" --vault "$OP_VAULT" "password=$value" > /dev/null
      else
        op item create --category login --title "$item" --vault "$OP_VAULT" "password=$value" > /dev/null
      fi
      ;;
    keychain)
      security add-generic-password -U -s "$KEYCHAIN_SERVICE" -a "$key" -w "$value"
      ;;
    *) echo "Error: 不正なバックエンド: $SECRETS_BACKEND" >&2; exit 1 ;;
  esac
}

_secret_read() {
  local key="$1"
  case "$SECRETS_BACKEND" in
    1password)
      op item get "$(_op_item_name "$key")" --vault "$OP_VAULT" --fields password --reveal 2>/dev/null || echo ""
      ;;
    keychain)
      security find-generic-password -s "$KEYCHAIN_SERVICE" -a "$key" -w 2>/dev/null || echo ""
      ;;
    *) echo "Error: 不正なバックエンド: $SECRETS_BACKEND" >&2; exit 1 ;;
  esac
}

_secret_exists() {
  local key="$1"
  case "$SECRETS_BACKEND" in
    1password) op item get "$(_op_item_name "$key")" --vault "$OP_VAULT" &>/dev/null ;;
    keychain)  security find-generic-password -s "$KEYCHAIN_SERVICE" -a "$key" &>/dev/null ;;
    *) echo "Error: 不正なバックエンド: $SECRETS_BACKEND" >&2; exit 1 ;;
  esac
}

_secret_delete() {
  local key="$1"
  case "$SECRETS_BACKEND" in
    1password) op item delete "$(_op_item_name "$key")" --vault "$OP_VAULT" 2>/dev/null || true ;;
    keychain)  security delete-generic-password -s "$KEYCHAIN_SERVICE" -a "$key" 2>/dev/null || true ;;
    *) echo "Error: 不正なバックエンド: $SECRETS_BACKEND" >&2; exit 1 ;;
  esac
}

_secret_list_repos() {
  case "$SECRETS_BACKEND" in
    1password)
      op item list --vault "$OP_VAULT" --format json \
        | jq -r '.[].title' \
        | grep "^${OP_ITEM_PREFIX}--" \
        | grep -v "^${OP_GLOBAL_ITEM}$" \
        | sed "s|^${OP_ITEM_PREFIX}--||"
      ;;
    keychain)
      [ -f "$CC_REGISTRY" ] && cat "$CC_REGISTRY" || true
      ;;
  esac
}

_registry_add() {
  [ "$SECRETS_BACKEND" = "keychain" ] || return 0
  local repo="$1"
  touch "$CC_REGISTRY" && chmod 600 "$CC_REGISTRY"
  grep -qxF "$repo" "$CC_REGISTRY" || echo "$repo" >> "$CC_REGISTRY"
}

_registry_remove() {
  [ "$SECRETS_BACKEND" = "keychain" ] || return 0
  local repo="$1"
  [ -f "$CC_REGISTRY" ] || return 0
  grep -vxF "$repo" "$CC_REGISTRY" > "${CC_REGISTRY}.tmp" && mv "${CC_REGISTRY}.tmp" "$CC_REGISTRY"
}

# ---------------------------------------------------------------------------
# サブコマンド
# ---------------------------------------------------------------------------

cmd_init_global() {
  _select_backend
  _require_backend

  local exists=false
  _secret_exists "global" && exists=true

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

  _secret_write "global" "$api_key"

  case "$SECRETS_BACKEND" in
    1password) echo "登録しました: op://${OP_VAULT}/${OP_GLOBAL_ITEM}/password" ;;
    keychain)  echo "登録しました: keychain ${KEYCHAIN_SERVICE}/global" ;;
  esac
}

cmd_init() {
  if [ "${1:-}" = "-g" ]; then
    cmd_init_global
    return
  fi

  _select_backend
  _require_backend
  local remote_repo
  remote_repo=$(_repo_from_remote)
  local repo
  if [ -z "$remote_repo" ]; then
    repo="$(basename "$(pwd)")"
  else
    repo="$remote_repo"
  fi
  local secret_key="repo:${repo}"

  echo "リポジトリ: $repo"
  echo ""

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

  _secret_write "$secret_key" "$token"
  _registry_add "$repo"

  echo "保存しました: $repo"
}

cmd_run() {
  _load_backend
  _require_backend
  local use_api_key=false
  local claude_args=()
  while [ $# -gt 0 ]; do
    case "$1" in
      --api-key) use_api_key=true; shift ;;
      --) shift; claude_args+=("$@"); break ;;
      *) claude_args+=("$1"); shift ;;
    esac
  done

  local repo
  repo=$(_repo_from_remote)

  local tty_flags="-i"
  local term_args=()
  if [ -t 0 ]; then
    tty_flags="-it"
    local _stty_size
    _stty_size=$(stty size 2>/dev/null)
    local _rows="${_stty_size% *}"
    local _cols="${_stty_size#* }"
    term_args=(
      -e TERM="xterm-256color"
      -e COLUMNS="${_cols:-80}"
      -e LINES="${_rows:-24}"
    )
  fi

  local base_docker_args=(
    run $tty_flags --rm
    -v "$(pwd)":/workspace
    -v ~/.claude:/home/claude/.claude
    ${term_args[@]+"${term_args[@]}"}
    ${repo:+-e GH_REPO="$repo"}
  )

  local rc=0
  case "$SECRETS_BACKEND" in
    1password)
      local op_env=()
      local extra_docker_args=()
      if $use_api_key; then
        op_env+=("ANTHROPIC_API_KEY=op://${OP_VAULT}/${OP_GLOBAL_ITEM}/password")
        extra_docker_args+=(-e ANTHROPIC_API_KEY)
      fi
      if [ -n "$repo" ]; then
        local op_item
        op_item=$(_op_item_name "repo:${repo}")
        op_env+=("GH_TOKEN=op://${OP_VAULT}/${op_item}/password")
        extra_docker_args+=(-e GH_TOKEN)
      fi
      if [ ${#op_env[@]} -gt 0 ]; then
        env "${op_env[@]}" op run -- docker "${base_docker_args[@]}" ${extra_docker_args[@]+"${extra_docker_args[@]}"} "$DOCKER_IMAGE" ${claude_args[@]+"${claude_args[@]}"} || rc=$?
      else
        docker "${base_docker_args[@]}" "$DOCKER_IMAGE" ${claude_args[@]+"${claude_args[@]}"} || rc=$?
      fi
      ;;
    keychain)
      local secrets_dir
      secrets_dir=$(mktemp -d)
      chmod 700 "$secrets_dir"
      trap 'rm -rf "$secrets_dir"' EXIT
      if $use_api_key; then
        local anthropic_key
        anthropic_key=$(_secret_read "global")
        if [ -z "$anthropic_key" ]; then
          echo "Error: Anthropic API キーが未登録です。cc init -g を実行してください。" >&2
          exit 1
        fi
        printf '%s' "$anthropic_key" > "$secrets_dir/anthropic_api_key"
        chmod 600 "$secrets_dir/anthropic_api_key"
      fi
      if [ -n "$repo" ]; then
        local token
        token=$(_secret_read "repo:${repo}")
        if [ -z "$token" ]; then
          echo "Warning: $repo は未登録です。cc init を実行してください。" >&2
          echo "認証なしで起動します。" >&2
        else
          printf '%s' "$token" > "$secrets_dir/gh_token"
          chmod 600 "$secrets_dir/gh_token"
        fi
      fi
      docker "${base_docker_args[@]}" -v "$secrets_dir:/run/secrets:ro" "$DOCKER_IMAGE" ${claude_args[@]+"${claude_args[@]}"} || rc=$?
      ;;
  esac
  return $rc
}

cmd_list() {
  _load_backend
  echo "登録済みリポジトリ:"
  _secret_list_repos
}

cmd_revoke() {
  _load_backend
  _require_backend
  local repo
  repo=$(_repo_from_remote)
  if [ -z "$repo" ]; then
    repo="$(basename "$(pwd)")"
  fi
  _secret_delete "repo:${repo}"
  _registry_remove "$repo"

  echo "削除しました: $repo"
}

# ---------------------------------------------------------------------------
# エントリーポイント
# ---------------------------------------------------------------------------

cmd_build() {
  local script_dir
  script_dir="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
  docker build -t "$DOCKER_IMAGE" "$script_dir"
}

case "${1:-run}" in
  init)   cmd_init "${2:-}" ;;
  run)    [ $# -gt 0 ] && shift; cmd_run "$@" ;;
  build)  cmd_build ;;
  list)   cmd_list ;;
  revoke) cmd_revoke ;;
  *)      echo "Usage: cc [init [-g]|run [--api-key] [-- <claude args>]|build|list|revoke]" >&2; exit 1 ;;
esac
