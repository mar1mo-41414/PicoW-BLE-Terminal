# PicoBLE-Terminal

Raspberry Pi Pico W 用の **BLE UART ターミナル**ファームウェア。
スマートフォンや PC から Bluetooth (Nordic UART Service) 経由で接続し、
Unix ライクな CLI で Pico を操作できます。

単なる GPIO サンプルではなく、
今後さまざまな機能を追加していける「小型 OS」を目指した構造になっています。

- 疎結合な CLI コマンドレジストリ (ファイル追加のみで拡張可能)
- ログサブシステム (BLE / USB serial / 将来の Wi-Fi へ同時出力)
- OTA 更新フックとパーティション設計
- 保守性・拡張性を最優先

---

## ハードウェア要件

- **Raspberry Pi Pico W** または **Pico 2 W** (CYW43 搭載モデル)
- USB ケーブル (書き込み / デバッグ用)

---

## ビルド

### 依存

- [pico-sdk](https://github.com/raspberrypi/pico-sdk) (最新版, サブモジュール展開済)
- arm-none-eabi-gcc
- CMake 3.13+
- Python 3 (`compile_gatt.py` 用)

### 手順

```bash
export PICO_SDK_PATH=/path/to/pico-sdk

cmake -S . -B build -DPICO_BOARD=pico_w
cmake --build build -j
```

`build/picoble_terminal.uf2` を BOOTSEL 状態の Pico にドラッグ&ドロップして書き込みます。

Pico 2 W (RP2350) を使う場合は `-DPICO_BOARD=pico2_w`。

---

## 使い方

1. 書き込み後、Pico を再起動。
2. `Pico-Terminal` という名前で BLE アドバタイズが始まります。
3. **nRF Connect** / **Serial Bluetooth Terminal** など NUS (Nordic UART Service) に対応した
   クライアントから接続。
4. 接続直後にプロンプトが送られてきます:

```
PicoBLE Terminal v0.1.1
Type "help"

>
```

USB でシリアル (CDC) に接続すると同じ入出力がミラーリングされ、
BLE がなくても動作確認できます。

---

## 内蔵コマンド

| コマンド | 概要 |
|----------|------|
| `help` | 登録済みコマンド一覧を表示 |
| `version` | ファームウェアと SDK のバージョン |
| `echo <text...>` | 引数をそのまま表示 |
| `clear` | 画面クリア (ANSI エスケープ) |
| `gpio <pin> read\|high\|low\|toggle` | GPIO 操作 |
| `adc <pin> read` | ADC 読み取り (GPIO 26–29) |
| `system info` | ハードウェア / BLE / uptime |
| `uptime` | 起動からの経過時間 |
| `reboot` | ウォッチドッグでソフトリセット |

エラー表示は Linux ライクに:

```
> gpio
Usage:
  gpio <pin> read|high|low|toggle
```

---

## 新しいコマンドの追加

コマンドは **`src/cli/commands/` にファイルを 1 つ追加するだけ**で登録できます。

`src/cli/commands/cmd_hello.c` を作成:

```c
#include <stdio.h>
#include "cli/command.h"

static int cmd_hello(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    cli_printf(ctx, "hello, world\n");
    return 0;
}

CLI_COMMAND_REGISTER(hello,
    /* summary */ "print a greeting",
    /* usage   */ "hello",
    cmd_hello);
```

`CLI_COMMAND_REGISTER` マクロが GCC の `__attribute__((constructor))` を使って
自動的にコマンドテーブルへ登録します。
CMake は `src/cli/commands/*.c` を glob するので、
再 configure すればビルドに含まれます (`cmake -S . -B build` を再実行)。

サブコマンド (`gpio X high` のような形) は
ハンドラの中で `argv[1]` を見て分岐してください。

---

## ディレクトリ構成

```
src/
  main.c                 起動 / メインループ
  cli/
    cli.c                行受信 → プロンプト → ディスパッチ
    parser.c             空白 / ダブルクォート引数パーサ
    command.c            コマンドレジストリ (自動登録)
    commands/            各コマンドの実装 (追加はここへ)
  ble/
    ble_nus.c            Nordic UART Service + アドバタイズ
    nus.gatt             GATT データベース定義
  system/
    log.c                sink 抽象化 (BLE/USB/Wi-Fi)
    uptime.c             起動時刻
    sysinfo.c            system info の実装本体
  drivers/
    gpio_ctrl.c          GPIO ラッパ
    adc_ctrl.c           ADC ラッパ
  ota/
    ota.c                OTA API とパーティション設計 (雛形)
  network/               将来: Wi-Fi / HTTP / MQTT
  storage/               将来: KVS / スクリプト
include/                 公開ヘッダ (現状未使用)
```

---

## ログサブシステム

`src/system/log.c` は複数の **sink** を持つリング状のリストです。
`log_write(LOG_INFO, "msg\n")` は登録されている全 sink に書き出されます。
BLE 接続時は BLE sink が、USB CDC 経由でも USB sink が受け取ります。
将来 Wi-Fi TCP sink や syslog sink を追加できます。

CLI 出力は `cli_ctx_t.print()` を通るため、
テスト時にはメモリバッファ sink に差し替え可能な設計です。

---

## OTA (未完成 / 設計段階)

現状は API とパーティション設計のみが入っています。

- パーティション: `boot` / `slot_a` / `slot_b` / `metadata`
  (実際のオフセットは `src/ota/ota.c` を参照)
- API:
  - `ota_begin(size)`
  - `ota_write(offset, buf, len)`
  - `ota_finalize()`
  - `ota_status()`
- 将来的に `ota check` / `ota update` コマンドから呼び出す想定。
- ロールバック: ブート時に `slot_a` を検証し、失敗したら `slot_b` に切り戻す
  デュアルスロット構成 (詳細は未実装)。

現段階では `ota` コマンドはステータスを表示し、
未実装機能へのアクセスは `not implemented` を返します。

---

## ライセンス

未定 (追って決定)。
