# SPEC.md — cc 動作要件

## 概要

Docker コンテナ内で Claude Code CLI を非 root ユーザーとして実行し、オンボーディングなしで即座に対話セッションを開始する。

## 要件

### R1: 非 root 実行

Claude Code は root / sudo 権限での `--dangerously-skip-permissions` を禁止しているため、コンテナ内では非 root ユーザー (`claude`) で実行する。

- Dockerfile で `useradd` により `claude` ユーザーを作成
- `USER claude` で以降のレイヤーとエントリーポイントを非 root にする
- Claude Code バイナリは root でインストール後 `/usr/local/bin/` にコピー（`cp -L` でシンボリックリンクを解決）し、`chmod 755` で全ユーザーが実行可能にする

### R2: ホスト側設定のマウント

| コンテナパス | ホスト側 | 用途 |
|---|---|---|
| `/home/claude/.claude` | `~/.claude` | Claude Code 認証・設定 |
| `/home/claude/.gstack` | `~/.gstack` | gstack データ |
| `/home/claude/.config/gh-tokens` | `claude-gh-tokens` (named volume) | 暗号化済み GitHub PAT |
| `/workspace` | `$(pwd)` | 作業ディレクトリ |

### R3: オンボーディングスキップ

コンテナは `--rm` で毎回破棄されるため、エントリーポイントで `~/.claude.json` を生成し初回セットアップを回避する。

```json
{
  "hasCompletedOnboarding": true,
  "theme": "dark",
  "projects": {
    "/workspace": {
      "hasTrustDialogAccepted": true
    }
  }
}
```

- `hasCompletedOnboarding`: テーマ選択等の初回ウィザードをスキップ
- `theme`: ターミナルテーマ（`dark` / `light`）
- `projects./workspace.hasTrustDialogAccepted`: ワークスペース信頼ダイアログをスキップ

このファイルはマウントされた `~/.claude/` とは別のパス（`$HOME/.claude.json`）に書き込む。

### R4: GitHub 認証

- GitHub PAT は AES-256-CBC で暗号化し named volume に保存
- パスフレーズは 1Password で管理（vault: `CC`）
- エントリーポイントで復号して `gh auth login --with-token` で認証

### R5: Anthropic API 認証

- API キーは 1Password に保存（アイテム: `claude-docker-global`）
- `ANTHROPIC_API_KEY` 環境変数としてコンテナに渡す

### R6: ホスト側の依存

| ツール | 用途 |
|---|---|
| Docker | コンテナ実行 |
| 1Password CLI (`op`) | パスフレーズ・API キー取得 |
| `openssl` | トークン暗号化（init 時） |
| `jq` | JSON パース |
