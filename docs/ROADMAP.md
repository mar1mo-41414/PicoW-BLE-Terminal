# PicoBLE-Terminal ROADMAP

将来やりたいことの雑多なメモ。優先度順ではない。
実装フェーズに入ったら [`SPEC.md`](SPEC.md) の該当セクションと同期する。

---

## ★ シェルスクリプト機能

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

1. **Phase A: 変数 + 展開**
   - `set` / `unset` / `env`
   - `$名前` 展開をパーサに追加
2. **Phase B: 待機 + ループ**
   - `sleep` / `repeat`
   - パースした行を再ディスパッチする内部 API
3. **Phase C: 条件分岐**
   - `if` / `while` — 直前コマンドの戻り値(`CLI_OK` = truthy)
4. **Phase D: スクリプト保存 + 実行**
   - flash に multi-line スクリプトを書き込む
   - `run <name>` で行単位に順次ディスパッチ

Phase A 単独でも「LED を named pin で扱う」など実用性は高い。

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

`src/drivers/` に薄いラッパを追加し、対応 CLI コマンドを 1 つずつ。

| 機能 | ドライバファイル | CLI コマンド案 |
|---|---|---|
| **PWM** | `drivers/pwm_ctrl.c` | `pwm <slice> freq <hz> duty <pct>` |
| **I2C** | `drivers/i2c_ctrl.c` | `i2c <bus> scan` / `i2c <bus> read <addr> <len>` / `i2c <bus> write <addr> <hex>` |
| **SPI** | `drivers/spi_ctrl.c` | `spi <bus> xfer <hex>` |
| **UART** | `drivers/uart_ctrl.c` | `uart <inst> send <text>` / `uart <inst> tail` |
| **WS2812** | `drivers/ws2812_ctrl.c` (PIO) | `neopixel <count> set <index> <RRGGBB>` |
| **温度センサ (内蔵)** | `drivers/temp_sensor.c` | `temp` |

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
