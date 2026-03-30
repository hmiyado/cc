# cc

Claude Code をどのプロジェクトでもカジュアルに使うための Docker ベース CLI ツール。

## 依存

- Docker
- 1Password CLI (`op`)
- `openssl`, `jq`

## セットアップ

```bash
# Anthropic API キーを登録
cc init -g

# イメージをビルド
docker build -t my-claude-code .

# cc をパスに通す
chmod +x cc
ln -s "$(pwd)/cc" ~/.local/bin/cc
```

## 使い方

```bash
cd ~/projects/myapp
cc init   # リポジトリ登録（1Password にパスフレーズ + GitHub PAT 暗号化保存）
cc        # 起動
cc list   # 登録済みリポジトリ一覧
cc revoke # トークン削除
```

## アーキテクチャ

```
cc (Bash) → docker run my-claude-code → entrypoint.sh → claude
```

コンテナは非 root ユーザー (`claude`) で実行される。`--dangerously-skip-permissions` は root で使用できないため、Dockerfile で専用ユーザーを作成している。

## 永続化される場所

| データ | 場所 |
|---|---|
| Claude Code 認証・設定 | `~/.claude` → `/home/claude/.claude` (host mount) |
| gstack データ | `~/.gstack` → `/home/claude/.gstack` (host mount) |
| GitHub トークン（暗号化済み） | `claude-gh-tokens` → `/home/claude/.config/gh-tokens` (named volume) |
| パスフレーズ・API キー | 1Password Vault `CC` |

## オンボーディング

コンテナは `--rm` で毎回破棄されるため、エントリーポイントで `~/.claude.json` を自動生成し、テーマ選択・ワークスペース信頼ダイアログをスキップする。
