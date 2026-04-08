# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 概要

Claude Code をどのプロジェクトでもカジュアルに使うための Docker ベース CLI ツール。GitHub PAT と Anthropic API キーを 1Password または macOS Keychain で管理し、`op run` 経由でコンテナに渡す。

## セットアップ

```bash
docker build -t my-claude-code .
chmod +x cc
ln -s "$(pwd)/cc" ~/.local/bin/cc
```

## コマンド

| コマンド | 説明 |
|---|---|
| `cc init` | 現在のリポジトリを登録（1Password にパスフレーズ登録 + GitHub PAT 暗号化保存） |
| `cc` / `cc run` | Docker コンテナを起動して Claude Code を実行 |
| `cc list` | 登録済みリポジトリ一覧（1Password から取得） |
| `cc revoke` | トークン削除 |

## アーキテクチャ

```
cc (Bash) → docker run my-claude-code → entrypoint.sh → claude
```

- **cc**: ホスト側のエントリーポイント。1Password CLI (`op run`) または Keychain からシークレットを取得し、Docker コンテナに渡す
- **Dockerfile**: `node:24-slim` ベース。GitHub CLI・Claude Code CLI をインストール
- **entrypoint.sh**: コンテナ起動時にシークレットを環境変数にセットし `claude` を実行

## マウント構成

| パス | ホスト側 | 用途 |
|---|---|---|
| `/workspace` | `$(pwd)` | 作業ディレクトリ |
| `/home/claude/.claude` | `~/.claude` | Claude Code 認証・設定 |

## 定数（cc スクリプト内）

| 変数 | 値 |
|---|---|
| `DOCKER_IMAGE` | `my-claude-code` |
| `OP_VAULT` | `CC` |
| `OP_ITEM_PREFIX` | `containered-claude` |

1Password アイテム名: `containered-claude--{owner}-{repo}`、参照パス: `op://CC/containered-claude--{owner}-{repo}/password`

## 依存

ホスト側: Docker, 1Password CLI (`op`) または macOS Keychain, `jq`

## gstack (REQUIRED — global install)

**Before doing ANY work, verify gstack is installed:**

```bash
test -d ~/.claude/skills/gstack/bin && echo "GSTACK_OK" || echo "GSTACK_MISSING"
```

If GSTACK_MISSING: STOP. Do not proceed. Tell the user:

> gstack is required for all AI-assisted work in this repo.
> Install it:
> ```bash
> git clone --depth 1 https://github.com/garrytan/gstack.git ~/.claude/skills/gstack
> cd ~/.claude/skills/gstack && ./setup --team
> ```
> Then restart your AI coding tool.

Do not skip skills, ignore gstack errors, or work around missing gstack.

Using gstack skills: After install, skills like /qa, /ship, /review, /investigate,
and /browse are available. Use /browse for all web browsing.
Use ~/.claude/skills/gstack/... for gstack file paths (the global path).
