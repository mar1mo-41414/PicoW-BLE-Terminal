# PicoBLE-Terminal

Raspberry Pi Pico W 用の **BLE UART ターミナル**ファームウェア。
スマートフォンや PC から Bluetooth (Nordic UART Service) 経由で接続し、
Unix ライクな CLI で Pico を操作できます。

- 疎結合な CLI コマンドレジストリ (**ファイル 1 個ドロップイン**でコマンド追加)
- 3-way ログ sink (BLE / USB serial / 将来の Wi-Fi など)
- Wi-Fi + HTTP + SHA-256 検証 + **独立ブートローダ + dual-slot OTA + 自動ロールバック**
- 保守性・拡張性を最優先した「小型 OS」志向の構成

詳細な内部仕様は [`docs/SPEC.md`](docs/SPEC.md) を、
将来やりたいことは [`docs/ROADMAP.md`](docs/ROADMAP.md) を参照。

---

## ハードウェア要件

- **Raspberry Pi Pico W** または **Pico 2 W** (CYW43 搭載モデル)
- USB ケーブル (書き込み / デバッグ用)

---

## ビルド

### 依存

- [pico-sdk](https://github.com/raspberrypi/pico-sdk)(サブモジュール展開済)
- arm-none-eabi-gcc
- CMake 3.13+
- Python 3(BTStack `compile_gatt.py` 用)

### 手順

```bash
export PICO_SDK_PATH=/path/to/pico-sdk

cmake -S . -B build -DPICO_BOARD=pico_w
cmake --build build -j
```

生成物:

| ファイル | 用途 |
|---|---|
| `build/picow-ota/bootloader/pico_ota_bootloader.uf2` | ブートローダ (0x10000000 起点、16 KB) |
| `build/picoble_terminal_a.uf2` | slot A アプリ (0x10004000 起点、768 KB 枠) |
| `build/picoble_terminal_b.uf2` | slot B アプリ (0x100C4000 起点、768 KB 枠) |

`.bin` も同時に出力されるので、OTA サーバから配信する場合はそちらを使います。

Pico 2 W (RP2350) の場合: `-DPICO_BOARD=pico2_w`(未検証、要動作確認)。

---

## 初回書き込み

Pico を BOOTSEL(BOOTSEL ボタンを押しながら USB 接続)にし、
以下の 2 つを順にドラッグ&ドロップ:

1. `build/picow-ota/bootloader/pico_ota_bootloader.uf2`
2. `build/picoble_terminal_a.uf2`

再起動するとブートローダがメタデータを初期化して slot A を起動します。

以降の更新は **OTA**(後述)で無線から差し替え可能で、
BOOTSEL 経由の再書き込みは不要です。

---

## 使い方

1. 電源投入後、`Pico-Terminal` として BLE アドバタイズが始まります
2. **nRF Connect** / **Bluefruit Connect** / **Serial Bluetooth Terminal** など
   NUS 対応クライアントから接続
3. 接続直後にプロンプトが送られてきます:

```
PicoBLE Terminal v0.1.2
Type "help"

>
```

USB CDC 経由でも同じシェルにアクセスできます(BLE と USB は独立した行バッファを持ちます)。

---

## 内蔵コマンド

| コマンド | 概要 |
|---|---|
| `help [cmd]` | コマンド一覧 / 個別コマンドの Usage |
| `version` | ファームウェア + SDK + ビルド日時 |
| `echo <args...>` | 引数を表示(引数パースの動作確認用) |
| `clear` | 画面クリア (ANSI エスケープ) |
| `gpio <pin> read\|high\|low\|toggle` | GPIO 制御(GPIO 23-25 は CYW43 予約警告) |
| `adc <pin> read` | ADC 読み取り(GPIO 26-29) |
| `system info` | ハードウェア / BLE / Wi-Fi / uptime |
| `uptime` | 起動からの経過時間 |
| `pwm <pin> <freq> <duty> \| pwm <pin> off` | PWM 出力 |
| `temp` | 内蔵温度センサ (°C) |
| `i2c <bus> scan\|read\|write` | I2C マスタ操作 |
| `wifi status\|connect <ssid> <psk>\|forget` | Wi-Fi 制御 |
| `net info\|arp <ip>\|test <host> <port>` | ネットワーク診断 |
| `ota status\|download <url> [sha]\|apply\|confirm\|abort` | OTA 制御 |
| `set NAME=VAL`, `unset NAME`, `env` | シェル変数 (`$NAME` で展開) |
| `sleep <ms\|s>` | 非ブロッキング待機 |
| `repeat <n> <cmd...>` | コマンドを N 回繰り返し |
| `if COND then BODY [else ELSE]` | 条件分岐(戻り値ベース) |
| `while COND do BODY` | 条件付きループ(10000 回上限) |
| `script save\|fetch\|list\|show\|rm` | .script を flash に保管 |
| `run <name> [args...]` | スクリプト実行 (`$1`..`$9`, `$#`, `$?` 展開) |
| `bind gpio <pin> <edge> <cmd...>` | GPIO エッジで自動実行 |
| `unbind gpio <pin> \| unbind all` | バインド解除 |
| `bindings [save\|load]` | バインド一覧・永続化 |
| `reboot` | ウォッチドッグでソフトリセット |

エラー表示は Linux ライクに:

```
> gpio
Usage:
  gpio <pin> read|high|low|toggle|latch <ms|s>
```

---

## シェルスクリプト

変数、繰り返し、待機、flash 保存 + 実行までできます。

**BLE 経由で保存(ヒアドキュメント方式):**

```
> script save led_blink
script: entering capture — end with a lone '.end' line (or '.abort' to discard)
: set LED=5
: repeat 10 gpio $LED latch 100ms
: sleep 500
: .end
script: saved 'led_blink' (52 bytes)

> run led_blink       # 実行
```

**Wi-Fi 経由で HTTP fetch(エディタで書いたファイルをそのまま配信):**

```bash
# ホスト側
python3 -m http.server 8000
```

```
> script fetch led_blink http://192.168.11.6:8000/led_blink.script
script: saved 'led_blink' (52 bytes)
> run led_blink
```

**内蔵メタコマンド:**

- `set NAME=VALUE` / `unset NAME` / `env` — 変数
- `sleep 500ms` / `sleep 2s` — 待機(BLE/Wi-Fi 停滞なし)
- `repeat 100 <cmd...>` — N 回繰り返し
- `run <name>` — 保存済みスクリプトを 1 行ずつ実行

**変数展開のルール:**

- `$NAME` または `${NAME}` で参照(前後は英数字_で境界判定)
- 未定義の変数は空文字に展開(エラーにならない、bash 慣習)
- 展開は tokenization の**後**、値内の空白では新トークンを作らない

**制限:**

- スクリプト 1 本 = 最大 4032 バイト、最大 16 本まで(0x185000 領域の 64 KB)
- ネストした `run` は未対応(`repeat` は使える)

**関数風の使い方(引数付き `run`):**

```
> script save blink
: gpio $1 latch $2
: .end

> run blink 5 100ms      # $1=5, $2=100ms
> run blink 3 500ms
```

- `$0` = script name, `$1..$9` = 引数, `$#` = 引数個数, `$?` = 直前の rc

**条件分岐 / ループ:**

```
> if gpio 2 read then gpio 5 high else gpio 5 low
> while gpio 2 read do gpio 5 toggle
> if wifi_status then ota download http://... else echo offline
```

- `then` / `else` / `do` はキーワード(引数側で使うと衝突するので注意)
- `while` は 10000 回で安全停止

---

## GPIO イベントバインディング

「ピンがこうなったらコマンド実行」を宣言的に登録できます。

```
> bind gpio 2 rise gpio 5 latch 200ms
bind: gpio 2 high -> gpio 5 latch 200ms

> bind gpio 3 change run alarm
> bind gpio 4 fall unset ALERT

> bindings
  [ 0] gpio  2 high   (debounce=20ms) -> gpio 5 latch 200ms
  [ 1] gpio  3 change (debounce=20ms) -> run alarm
  [ 2] gpio  4 low    (debounce=20ms) -> unset ALERT

> bindings save           # 永続化(次回起動で自動復元)
bindings: saved 3 bindings

> unbind gpio 3
> unbind all
```

**仕様:**

- edge: `high` / `low` / `change`(エイリアス: `rise/rising`, `fall/falling`, `both`)
- 最大 16 バインド、target は 96 バイトまで
- IRQ で pending フラグをセット、main-thread の 20 ms タイマで実行(BLE を止めない)
- デフォルト 20 ms デバウンス
- 実行時の出力は破棄(BLE プロンプトを乱さない)。副作用は残る
- 永続化: 0x1FA000 (専用 4 KB セクタ、CRC 付き)、`bindings save`
- 起動時に flash から自動ロード

---

## コマンドを追加する

**`src/cli/commands/` にファイル 1 つ足すだけ**で新しいコマンドを登録できます。

`src/cli/commands/cmd_hello.c`:

```c
#include "cli/cli.h"
#include "cli/command.h"

static int cmd_hello(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc > 1) {
        cli_printf(ctx, "hello, %s\r\n", argv[1]);
    } else {
        cli_write(ctx, "hello, world\r\n");
    }
    return CLI_OK;
}

CLI_COMMAND_REGISTER(hello,
    "print a greeting",
    "hello [name]",
    cmd_hello);
```

再 configure + ビルド:

```bash
cmake -S . -B build
cmake --build build -j
```

- CMake の `file(GLOB ...)` がディレクトリを見にいくため、
  ファイル追加後は 1 度 configure(`cmake -S . -B build`)が必要
- `CLI_COMMAND_REGISTER` マクロが GCC の `__attribute__((constructor))` で
  起動時に自動登録します(中央のテーブルには何も手を加えなくて良い)

**サブコマンド、引数バリデーション、Usage 慣習、返り値、出力方法**の詳細と
コピペしやすいテンプレートは [`docs/SPEC.md`](docs/SPEC.md#コマンドを追加する) に。

---

## ディレクトリ構成

```
picow-ota/                  OTA フレームワーク (別プロジェクト扱い)
  README.md
  CMakeLists.txt            pico_ota / pico_ota_metadata / helper
  bootloader/               独立ブートローダターゲット
  linker/                   3 種類の memmap_*.ld
  include/pico_ota/         公開ヘッダ (metadata / ota / sha256 / crc32 / log)
  src/                      ota.c / sha256.c / crc32.c / log_default.c
include/
  btstack_config.h          BTStack のアプリ側設定
  lwipopts.h                LwIP のアプリ側設定
docs/
  SPEC.md                   実装仕様書 (拡張ポイント / 契約 / 内部設計)
  ROADMAP.md                将来の機能案
src/
  main.c                    起動 / メインループ / タイマ配線 + pico_ota ロガー
  cli/
    cli.c                   行受信 → プロンプト → ディスパッチ
    parser.c                空白 / ダブルクォート引数パーサ
    command.c               コマンドレジストリ (自動登録)
    commands/               各コマンドの実装 (← ここに追加)
  ble/
    ble_nus.c               Nordic UART Service + アドバタイズ
    nus.gatt                GATT データベース定義
  system/
    log.c                   sink 抽象化
    uptime.c                起動時刻
    sysinfo.c               system info の実装本体
    version.h               PICOBLE_FW_VERSION 定数
  drivers/                  ペリフェラルドライバ薄ラッパ (← 追加はここへ)
    gpio_ctrl.c
    adc_ctrl.c
  network/
    wifi.c                  STA 接続
    http_get.c              LwIP raw TCP HTTP クライアント
  storage/
    config.c                flash セクタ内 KV (pico_ota_crc32 を共有)
```

OTA フレームワークの詳細は [`picow-ota/README.md`](picow-ota/README.md) を参照。

---

## Wi-Fi

起動 2 秒後に自動接続します。優先順位:

1. Flash 保存済み(`wifi connect` で書いた資格情報)
2. ビルド時埋め込み `WIFI_SSID` / `WIFI_PASSWORD`
3. どちらもなければ待機 → CLI から `wifi connect <ssid> <psk>` で接続 + flash 保存

ビルド時埋め込み(開発用):

```bash
cmake -S . -B build -DPICO_BOARD=pico_w \
      -DWIFI_SSID=MyAP -DWIFI_PASSWORD=hunter2
```

コミット前に定数から `.env` などに移すこと。

---

## OTA 更新

1. `.bin` をホストで配信
   ```bash
   cd build
   python3 -m http.server 8000
   sha256sum picoble_terminal_a.bin picoble_terminal_b.bin
   ```
2. Pico の CLI で staging slot を確認 → その slot の .bin をダウンロード
   ```
   > ota status
   ...
     staging slot : B
   > ota download http://192.168.11.6:8000/picoble_terminal_b.bin <sha256>
   ...
     verified against expected digest
   > ota apply
   ota: applying — rebooting into slot B (on probation).
   ```
3. 再起動後は slot B が動作、30 秒生存で自動 confirm
   - `ota confirm` で手動確定も可
   - probation 中に 3 回リセットが続くとブートローダが自動でロールバック

**staging slot を間違えない**こと。slot A 実行中に slot A 用の .bin を焼くと
リンクアドレス不一致で probation 失敗 → ロールバックで救われるが更新は失敗します。

OTA プロトコルの詳細(メタデータレコード、CRC、apply シーケンス、
rollback state machine)は [`docs/SPEC.md`](docs/SPEC.md#ota-プロトコル) を参照。

---

## ライセンス

未定 (追って決定)。
