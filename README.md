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
| `system info` | ハードウェア / BLE / Wi-Fi / uptime |
| `uptime` | 起動からの経過時間 |
| `wifi status\|connect <ssid> <psk>\|forget` | Wi-Fi 状態 / 接続 / 資格情報消去 |
| `ota status\|download <url> [sha256]\|abort` | OTA イメージのステージング |
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

## Wi-Fi

起動 2 秒後に自動接続を試みます。優先順位:

1. Flash に保存された資格情報 (`wifi connect` で書いた分)
2. ビルド時に埋め込まれた `WIFI_SSID` / `WIFI_PASSWORD` (CMake の `-D` で渡す)
3. どちらもなければ待機。CLI から `wifi connect <ssid> <psk>` で接続すると
   同時に flash にも保存されます。

Flash 保存領域はデバイス末尾の 4KB セクタ (2MB フラッシュなら `0x1FF000`) で、
OTA ステージング領域とは物理的に別です。

ビルド時埋め込み:

```bash
cmake -S . -B build -DPICO_BOARD=pico_w \
      -DWIFI_SSID=MyAP -DWIFI_PASSWORD=hunter2
```

コミット前に `.env` などへ移してください。

---

## OTA

**フェーズ 1 + 2 実装済**: HTTP でダウンロード → 検証 → ブートローダ経由でスロット切替 → 30 秒無事なら自動 confirm、そうでなければ自動ロールバック。

### パーティション構成 (2MB flash)

```
0x000000 - 0x003FFF   ( 16 KB)  bootloader
0x004000 - 0x0C3FFF   (768 KB)  slot A
0x0C4000 - 0x183FFF   (768 KB)  slot B
0x184000 - 0x184FFF   (  4 KB)  metadata (active slot, pending flag, hash, ...)
0x185000 - 0x1FEFFF   (~488 KB) 予約
0x1FF000 - 0x1FFFFF   (  4 KB)  Wi-Fi 資格情報
```

ブートローダ・スロット A・スロット B の 3 つは互いに独立したビルドで、それぞれ専用リンカで異なるアドレスに配置します。ソースは全共通、`PICOBLE_SLOT=0/1` の compile define で自身のスロットを識別。

### 初回書き込み

BOOTSEL モードにして 2 ファイル連続でドラッグ:

1. `build/bootloader/picoble_terminal_boot.uf2` (16 KB、0x10000000 起点)
2. `build/picoble_terminal_a.uf2` (476 KB、0x10004000 起点)

再起動するとブートローダがメタデータを初期化しつつ slot A を起動します。

### 更新 (OTA)

ホストで HTTP サーバを立てる:

```bash
cd build
python3 -m http.server 8000
sha256sum picoble_terminal_a.bin picoble_terminal_b.bin
```

Pico の CLI から、**現在の staging slot 用の .bin** をダウンロード (`ota status` で確認):

```
> ota status
ota status
  active slot     : A
  staging slot    : B
  ...

> ota download http://192.168.11.6:8000/picoble_terminal_b.bin <sha256>
ota: erasing slot B staging area (~8s, BLE may stall)...
ota: fetching http://...
  ..32 KB
  ..
ota: NNN bytes staged in slot B
     sha256 ...
     verified against expected digest
ota: staged. Run `ota apply` to reboot into it.

> ota apply
ota: applying — rebooting into slot B (on probation).
```

再起動すると slot B が動きます。30 秒経過すると自動的に `ota confirm` が呼ばれ probation が解除されます。手動で確認したいなら:

```
> ota confirm
ota: confirmed — no rollback on next boot
```

### ロールバック

新しいイメージが起動失敗、または `ota confirm` を呼ぶ前に何度もリセットする状況が続くと、ブートローダは自動的に前のスロットに切り戻します(3 回試行後)。文鎮化リスクは大幅に低減されています。

### 制約

- **HTTPS 未対応**。TLS を組むと mbedtls が入って ~200KB 増える。SHA-256 の事前共有前提。
- **署名検証なし**。イメージ完全性はハッシュ、送信元認証は Wi-Fi の WPA2 に依存。
- **必ず staging slot 用の .bin を渡すこと**。slot A の実行中に slot A の .bin を渡すと、リンクアドレスが staging slot(B)と合わないためブート後にクラッシュ → 自動ロールバック、で救われますが、無駄な update サイクルを消費します。

### 参考: 手軽なテストサーバ

```bash
# ホスト側で
cd build
python3 -m http.server 8000

# Pico 側で (BLE 経由の CLI から)
wifi connect MyAP hunter2
ota status  # staging slot を確認
ota download http://192.168.11.6:8000/picoble_terminal_<staging>.bin <sha256>
ota apply
```

---

## ライセンス

未定 (追って決定)。
