# PicoBLE-Terminal ROADMAP

将来やりたいことの雑多なメモ。優先度順ではない。
実装フェーズに入ったら [`SPEC.md`](SPEC.md) の該当セクションと同期する。

---

## ★ シェルスクリプト機能

**Phase A / B / D 実装済み** (2026-07-08)。残るは Phase C(条件分岐)。

**目標**: CLI 上で簡単なスクリプトを書いて自動化できるようにする。
BLE から手入力するだけでなく、`.script` ファイルとして flash に保存 → 呼び出し可能に。

### 想定される最小構文

```sh
# 変数定義 (環境変数感覚)
set LED=5
set DELAY=100

# 変数展開 ($名前)
gpio high $LED

# 遅延
sleep 500              # ms

# 有限回ループ
repeat 100 gpio toggle $LED

# 条件付き実行 (シンプル形)
if wifi_connected; then
    ota download http://...
fi

# 変数のスコープはグローバル (シェル状態と同居)
```

### コマンド案

| コマンド | 概要 |
|---|---|
| `set <name>=<value>` | 変数代入 |
| `unset <name>` | 変数削除 |
| `env` | 変数一覧 |
| `sleep <ms>` | 指定ミリ秒待機(BLE 応答は続く) |
| `repeat <count> <cmd...>` | N 回繰り返し |
| `if <cmd>; then <cmd>; [else <cmd>;] fi` | 条件分岐(cmd の戻り値で判定) |
| `while <cmd>; do <cmd>; done` | 条件付きループ |
| `run <script_name>` | flash 保存済みスクリプトを実行 |
| `script save <name> <<EOF ... EOF` | スクリプトを flash に保存 |
| `script list` | 保存済みスクリプト一覧 |
| `script show <name>` | スクリプト内容表示 |
| `script rm <name>` | スクリプト削除 |

### 実装ヒント

- **パーサ拡張**: `$名前` の展開を `parser.c` に加える(引数トークン化直後にスキャン)
  - 変数がなければ空文字置換、あるいはエラー
  - `${LED}` 形式で名前境界を明示できるようにする
- **変数ストア**: 小さなハッシュテーブル or 線形リスト(50 エントリで十分)
  - `system/vars.c` あたりに切り出す
  - CLI の出力バッファと同じくらいのサイズ制限
- **repeat / if / while**: **メタコマンド** として実装
  - `argv[1..]` を「もう一度パーサに戻して実行する子コマンド」として扱う
  - `cli_dispatch_line(ctx, "gpio toggle 5")` のような内部 API を用意
  - **ネスト深度の上限**は決めておく(スタック枯渇対策、例: 8 段)
- **sleep**: BLE 接続を殺さないため、blocking `sleep_ms` ではなく
  `cyw43_arch_wait_for_work_until` 相当の待機を挟む
  - キャンセル可能にすると尚良い(受信バイトで打ち切り、Ctrl+C 相当)
- **スクリプト保存**: `storage/config.c` の汎用化(下記)と合わせて設計
  - 各スクリプト 1 セクタ(4 KB)、最大 10 個くらい
  - 予約領域 0x185000〜 を使う

### 段階的実装

1. **Phase A: 変数 + 展開** ✅ 実装済み
   - `set` / `unset` / `env`
   - `$名前` / `${名前}` 展開(post-tokenization、word-splitting なし)
2. **Phase B: 待機 + ループ** ✅ 実装済み
   - `sleep <ms|s>` — cyw43_arch_poll を叩きながらの keepalive スリープ
   - `repeat <n> <cmd...>` — cli_dispatch_argv() で再ディスパッチ
3. **Phase C: 条件分岐** ✅ 実装済み
   - `if COND then BODY [else ELSE]` — キーワードベース(セミコロンなし)
   - `while COND do BODY` — 10000 回上限
   - `$?` にディスパッチ後の rc を自動格納(cli_dispatch_argv 経由)
4. **Phase D: スクリプト保存 + 実行** ✅ 実装済み
   - 16 スロット × 4 KB の flash 領域 (0x185000〜)
   - CRC-32 付きレコード、`storage/scripts.c`
   - `script save <name>` — BLE ヒアドキュメント (`.end` で終了)
   - `script fetch <name> <url>` — HTTP GET
   - `script list` / `show` / `rm`
   - `run <name>` — 1 行ずつ dispatch(コメント行 `#` サポート)
   - ネストした `run` は未対応(将来 `depth` 制御にすれば追加可能)

---

## ★ イベント駆動 / GPIO バインディング

✅ 実装済み (2026-07-08)。詳細は README の該当セクション。
以下は当初の設計メモを残しておく(実装との差分は将来必要に応じて追記)。

**目標**: 「ピン X が high になったら script_name を実行」のような
イベント連動を宣言的に書けるようにする。ラベル貼り替えだけで
プロトタイピングが完結する。

### 使い方の例

```sh
# GPIO 2 が high になったら script_name を実行
bind gpio 2 high script_name

# エッジ検出も欲しい (rise / fall / change)
bind gpio 3 rise blink_led
bind gpio 3 fall stop_motor

# インラインコマンドも許可(script 化するほどでない用)
bind gpio 4 change "gpio 5 toggle"

# 一覧・解除
bindings
unbind gpio 2
```

スクリプト内からも書けるとよい:

```sh
# init.script:
set LED=5
bind gpio 2 rise pulse_led
```

### 実装ヒント

- **トリガの種類:**
  - `high` / `low` = 「状態が変わってその値になった」= 実質エッジ
  - `rise` / `fall` = エッジ明示
  - `change` = rise | fall
- **登録テーブル**: `system/bindings.c` に固定スロット(16 個くらい)。
  各エントリ: `{pin, edge, target_kind (script/cmd), name_or_cmd[64]}`。
- **IRQ ハンドラは絶対に軽く**: pico-sdk の `gpio_set_irq_enabled_with_callback` を
  使い、ハンドラ内は「フラグを立ててキューに登録」だけ。
  実行は BTStack timer が拾って main-thread context でディスパッチ。
- **チャタリング対策**: `debounce <ms>` オプションで直近発火から
  N ms 経過してなければ捨てる。デフォルト 20 ms。
- **再入防止**: 実行中の bind ハンドラが再度発火しても実行しない
  (in-flight フラグ)。
- **保存**: `bindings save` で config セクタに保存 → 起動時自動復元。
- **セーフティ**: `unbind all` を用意して事故に備える。

### 検討事項

- IRQ で拾えるのは基本エッジのみ。「level high 継続」を厳密に検出したいなら
  ポーリング(20 ms 間隔の BTStack timer)にフォールバックする実装も要。
- CYW43 予約 GPIO(23〜25)にはバインドを拒否。
- ADC ピン(26〜29)は bind の意味が薄い(閾値付き `adc_bind` は将来別枠で)。

---

## ★ 関数定義 (Python `def` 相当)

方式 A(引数付き `run`)✅ 実装済み (2026-07-08)。
方式 B(インライン `def NAME { ... }`)⬜ 未着手 — 以下設計案。

**目標**: シェルスクリプト内で関数を定義し、引数を渡して呼び出せる。
既存の `run <name>` を拡張する方向で最小コストで実現できる見込み。

### 使い方の例

**方式 A: 引数付き `run`(既存 script 拡張)**

```sh
# script save blink
: gpio $1 latch $2
: .end

> run blink 5 200ms       # $1=5, $2=200ms
> run blink 3 50ms
```

- `$1`〜`$9` は run 開始時にシェル変数として設定(既存 `set` と互換)
- `$#` = 引数の個数
- `$?` = 直前コマンドの戻り値
- 現行 `run` に「サブシェル的な変数スコープ」を導入するかは要検討
  (グローバル汚染が嫌なら save/restore)

**方式 B: インライン `def` ブロック**

```sh
def blink {
  gpio $1 latch $2
}

blink 5 200ms
```

- スクリプト内で `def NAME { ... }` を評価 → `NAME` がコマンドテーブルに登録
- `def` の解析は `{` と `}` を括る特殊なパースが必要
- 登録先: **通常の CLI コマンドテーブルには入れない**(constructor 経由でなく動的)、
  別テーブル `functions.c` を用意して `cli_command_find` から先に検索

### 実装ヒント

- **まず方式 A を実装**(既存 `run` に位置引数対応を足す + `$?`)。
  Phase D の延長線で 100 LoC くらいで済む。
- **方式 B は次段階**。パーサに `{ ... }` ブロック検出を追加、
  関数テーブルは動的登録型に。呼び出し検出は
  `cli_dispatch_argv` の入口で「関数テーブルに argv[0] があれば
  そのブロックを cli_dispatch_line ループで実行」。
- **再帰・ネスト**: 現行 `run` はネスト不可 → 関数呼び出しでも同じ制約が付く。
  内部で `depth` カウンタ + 上限(4 くらい)を導入すれば解禁できる。
- **戻り値**: 関数内の最後のコマンドの rc を `$?` に伝える。
  明示的な `return N` は必要になったら追加。

### 依存関係

- Phase C(if/while)と組み合わさると特に強力になる:
  ```sh
  def blink_if_button {
    if gpio 2 read; then
      gpio $1 latch $2
    fi
  }
  bind gpio 2 rise blink_if_button 5 200ms
  ```
- 上記の bindings と組み合わさると宣言的で強い。

---

## 設定保存の汎用化

現在 `storage/config.c` は Wi-Fi 資格情報専用の単一レコード。
これを **タグ付き KV** に拡張:

```c
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_count;
    // records[]: [type(2) | length(2) | payload...]
    uint32_t crc32;
} config_header_t;
```

- 各レコードは type + length + payload
- type = 1 は既存の Wi-Fi credentials 形式
- type = 2 でスクリプト、type = 3 でユーザ変数の永続化、など

**移行時の注意**: 既存の magic (`PCCG`) は残し、新形式は別 magic (`PCCG2` など)で区別する。
起動時に旧形式を検出したら新形式にリライトする一発コンバータを入れる。

---

## 追加ペリフェラルドライバ

| 機能 | ドライバファイル | CLI コマンド | 状態 |
|---|---|---|---|
| **PWM** | `drivers/pwm_ctrl.c` | `pwm <pin> <hz> <duty> / pwm <pin> off` | ✅ |
| **I2C** | `drivers/i2c_ctrl.c` | `i2c <bus> scan|read|write` | ✅ |
| **温度センサ (内蔵)** | `drivers/temp_sensor.c` | `temp`, `system info` | ✅ |
| **SPI** | `drivers/spi_ctrl.c` | `spi <bus> xfer <hex>` | ⬜ |
| **UART** | `drivers/uart_ctrl.c` | `uart <inst> send <text>` / `uart <inst> tail` | ⬜ |
| **WS2812** | `drivers/ws2812_ctrl.c` (PIO) | `neopixel <count> set <index> <RRGGBB>` | ⬜ |

各ドライバは lazy init + 予約リソース申告 + 範囲バリデーションのパターンを踏襲。

---

## Wi-Fi 機能拡充

- `wifi scan` — 周辺 AP 一覧(SSID, RSSI, セキュリティ)
- `wifi ap start <ssid> <psk>` — SoftAP モード
- 複数保存済み SSID(スーパーマーケットとご家庭の切り替え等)
- WPA3 (SAE) 明示指定

---

## ネットワークサービス

- **HTTPS**: mbedtls 有効化(~200 KB フットプリント増加を許容できるなら)
- **HTTP サーバ**: 診断用 Web UI(GPIO 状態、Wi-Fi スキャン結果、OTA トリガ)
- **MQTT クライアント**: センサデータをブローカに publish
- **mDNS**: `pico.local` などで発見しやすく
- **NTP**: 起動時に epoch 同期(sysinfo に real-world time を追加できる)

---

## ログ拡張

- `log level <debug|info|warn|error>` コマンド
- **BLE デバッグ characteristic**: ERROR 以上を専用 characteristic で送る
- **リングバッファ + `dmesg` コマンド**: 直近 N 行をいつでも取り出せる
- **syslog**(UDP 送信 sink)

---

## OTA 拡張

- **署名検証**: Ed25519 で先方公開鍵を焼き込み、署名付きイメージのみ受け入れ
- **段階的配信**: バージョン番号を metadata に入れ、
  同一版の再ダウンロードをスキップ
- **`ota version_manifest` エンドポイント**: サーバの `manifest.json` を先に読み、
  `available_version > current_version` のときのみ apply

---

## 品質・保守

- **ユニットテスト**: パーサ / SHA-256 / CRC / OTA metadata のホスト側テスト
  (pico-sdk からデコップリング)
- **CI**: GitHub Actions で毎 push でクロスコンパイル + 生成物サイズ確認
- **`clang-format` 設定**: スタイル固定化
- **`compile_commands.json` 生成**: IDE 補完向け(既に `.gitignore` 済み)
