# Claude Usage — Pebble Time 2 watchface

[Usage4Claude](https://github.com/f-is-h/Usage4Claude)（macOSメニューバー常駐アプリ）と同じ発想で、
Claude Code / Claude.ai の **5時間枠** と **7日枠** の使用率を Pebble Time 2（emery）の
ウォッチフェイスにリング表示する。

Pebbleアプリストアに同等の既存ウォッチフェイスは無し（2026年9月時点で確認）。

## 仕組み

Pebble本体はインターネットに直接繋がらないため、実際の通信はPebbleスマホアプリ内の
**PebbleKit JS**（`src/pkjs/index.js`）が担当する:

1. Clay設定画面（`src/pkjs/config.js`）でユーザーが貼り付けた **Claude OAuth リフレッシュトークン**
   を使い、`https://console.anthropic.com/v1/oauth/token` でアクセストークンを取得・キャッシュする。
2. `https://api.anthropic.com/api/oauth/usage` を叩いて `five_hour` / `seven_day` の
   `utilization`（%）と `resets_at`（リセット時刻）を取得する。
3. `Pebble.sendAppMessage` で時計本体（`src/c/main.c`）にパーセンテージとリセット時刻を送る。
4. 時計側は2つの円弧プログレスリングとリセットまでのカウントダウンを表示する
   （70%未満=緑、70-90%=オレンジ、90%以上=赤）。

更新は起動時・設定変更時・時計のSELECTボタン押下時・定期ポーリング（既定15分、設定で変更可）
で行われる。

## リフレッシュトークンの入手方法

以下のいずれか:

- `claude setup-token` コマンドを実行して発行する。
- 既にこのマシンで `claude` CLI にログイン済みなら、`~/.claude/.credentials.json` の
  `claudeAiOauth.refreshToken` の値をそのまま貼り付けても良い。

**トークンは第三者と共有しないこと。このリポジトリにコミットしないこと。**
`src/pkjs/config.js` の入力欄を通じてスマホ内の `localStorage` にのみ保存される。

## ビルド・インストール

```sh
pebble build
pebble install --emulator emery      # QEMUエミュレータで見た目を確認
pebble install --phone <ip>          # 実機Pebble Time 2 へインストール（要ペアリング済みスマホアプリ）
```

エミュレータでは `pebble sdk install-emulator emery` が未実行の場合は先に実行しておくこと。
設定画面（Clay）はエミュレータの場合ブラウザが自動で開く。実機の場合はPebbleスマホアプリの
アプリ設定から開く。

`pebble logs` でPebbleKit JS側のトークン更新・使用量取得・AppMessage送信ログを確認できる。

## プロジェクト構成

```
src/c/main.c            ウォッチフェイス本体（時刻 + 5時間枠/7日枠リング）
src/pkjs/index.js        PebbleKit JS（OAuth更新・使用量取得・送信）
src/pkjs/config.js        Clay設定画面定義（リフレッシュトークン・更新間隔）
package.json              プロジェクトマニフェスト（UUID・messageKeys等）
```

## 対応機種

`emery`（Pebble Time 2, 200x228, 64色）のみを対象にビルドする。他機種は未検証。

## ドキュメント

- [Pebble SDK Documentation](https://developer.repebble.com/)
- [Usage4Claude](https://github.com/f-is-h/Usage4Claude)（このウォッチフェイスの着想元）
