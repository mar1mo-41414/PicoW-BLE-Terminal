# PicoBLE-Terminal 仕様書

Raspberry Pi Pico W 用 BLE ターミナルファームウェアの実装仕様。
本ドキュメントは **拡張する人向けの一次資料** です。

- 動くだけなら [`../README.md`](../README.md) を参照
- 将来の機能アイデアは [`ROADMAP.md`](ROADMAP.md) を参照

---

## 目次

1. [設計思想](#1-設計思想)
2. [ソースレイアウトと責務](#2-ソースレイアウトと責務)
3. [ビルドシステム](#3-ビルドシステム)
4. [フラッシュレイアウト](#4-フラッシュレイアウト)
5. [起動フローとブートローダ](#5-起動フローとブートローダ)
6. [CLI プロトコル](#6-cli-プロトコル)
7. [コマンドを追加する](#7-コマンドを追加する) ★
8. [ドライバを追加する](#8-ドライバを追加する)
9. [ログサブシステム](#9-ログサブシステム)
10. [BLE / NUS 仕様](#10-ble--nus-仕様)
11. [Wi-Fi + HTTP + LwIP 構成](#11-wi-fi--http--lwip-構成)
12. [OTA プロトコル](#12-ota-プロトコル)
13. [設定保存](#13-設定保存)
14. [エラーコード一覧](#14-エラーコード一覧)
15. [ハマりどころ (トラブルシューティング)](#15-ハマりどころ-トラブルシューティング)

---

## 1. 設計思想

**「小型 OS」志向**: 個別機能を疎結合に足し引きできる構造を優先し、
機能ごとの結合コストを最小化する。以下 3 つの拡張点が中心:

- **コマンド**: `src/cli/commands/` にファイルを 1 つ落とすだけで登録
- **ログ sink**: `log_sink_t` を実装して `log_add_sink()` を 1 行呼ぶ
- **トランスポート**: 新しい入力チャネルは新しい `cli_ctx_t` + 出力アダプタで完結

「新しい機能を追加するのに `main.c` を触らない」ことを目安に設計する。

---

## 2. ソースレイアウトと責務

| ディレクトリ | 責務 | 触っていい人 |
|---|---|---|
| `src/cli/` | 行受信・パース・ディスパッチ | コアいじりのみ |
| `src/cli/commands/` | 各コマンド実装 | **拡張者はここ** |
| `src/drivers/` | ペリフェラルの薄ラッパ | 新デバイスは新ファイル |
| `src/ble/` | Nordic UART Service (BTStack 統合) | 変更希薄 |
| `src/network/` | Wi-Fi / HTTP | 新プロトコルは新ファイル |
| `src/storage/` | flash 内 KV | スキーマ追加は慎重 |
| `src/system/` | log / uptime / sysinfo / sha256 | 共有ユーティリティ |
| `src/ota/` | ステージング / メタデータ / apply / confirm | プロトコル安定 |
| `bootloader/` | 独立ブートローダ (別 CMake target) | 別プロセス感覚 |
| `linker/` | 3 種類の `memmap_*.ld` | フラッシュ拡張時のみ |
| `include/` | 公開ヘッダ・SDK 設定ヘッダ | `btstack_config.h`, `lwipopts.h`, `ota_metadata.h` |

---

## 3. ビルドシステム

### ターゲット

トップの `CMakeLists.txt` は 3 つの実行ファイルを生成する:

- `picoble_terminal_boot` — ブートローダ (0x10000000 起点)
- `picoble_terminal_a` — アプリ slot A (0x10004000 起点)
- `picoble_terminal_b` — アプリ slot B (0x100C4000 起点)

### アプリ 2 スロット化の仕組み

`add_app_slot(letter, id)` 関数がアプリ用ターゲットを一括生成:

```cmake
add_app_slot(a 0)
add_app_slot(b 1)
```

やっていること:

1. **同一の `APP_SOURCES`** を使って `add_executable`
2. `PICOBLE_SLOT=0` または `=1` を `target_compile_definitions` で注入
3. `pico_set_linker_script` でスロット別リンカを差す
4. `pico_add_extra_outputs` で `.uf2` / `.bin` / `.hex` を生成

**結果**: 同じソースコードから、リンクアドレスの異なる 2 つのイメージが出る。
リテラルアドレスが埋め込まれているので、`_a.bin` と `_b.bin` は **互換性なし**。

### コマンドファイルの glob

`src/cli/commands/*.c` は CMake の `file(GLOB CONFIGURE_DEPENDS ...)` で拾う。
新ファイル追加後は 1 度 configure(`cmake -S . -B build`)が必要。

### Wi-Fi 資格情報のビルド埋め込み(任意)

```bash
cmake -S . -B build -DWIFI_SSID=MyAP -DWIFI_PASSWORD=hunter2
```

なければ flash 保存を優先し、両方無ければ手動接続待ち。

---

## 4. フラッシュレイアウト

2 MB flash 前提:

| オフセット | サイズ | 内容 |
|---|---|---|
| `0x000000..0x003FFF` | 16 KB | Bootloader(自前 boot2 含む) |
| `0x004000..0x0C3FFF` | 768 KB | Slot A(アプリ、自前 boot2 は先頭 256 B に含めるが未使用) |
| `0x0C4000..0x183FFF` | 768 KB | Slot B(同上) |
| `0x184000..0x184FFF` | 4 KB | OTA メタデータ(1 セクタ) |
| `0x185000..0x1FEFFF` | ~488 KB | 予約(将来 KVS / スクリプト保存など) |
| `0x1FF000..0x1FFFFF` | 4 KB | Wi-Fi 資格情報 |

定数はすべて `include/ota_metadata.h` に集約:

```c
#define OTA_BOOTLOADER_OFFSET   0x00000000u
#define OTA_BOOTLOADER_SIZE     (16u  * 1024u)
#define OTA_SLOT_A_OFFSET       (OTA_BOOTLOADER_OFFSET + OTA_BOOTLOADER_SIZE)
#define OTA_SLOT_SIZE           (768u * 1024u)
#define OTA_SLOT_B_OFFSET       (OTA_SLOT_A_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_OFFSET     (OTA_SLOT_B_OFFSET + OTA_SLOT_SIZE)
#define OTA_METADATA_SIZE       (4u   * 1024u)
```

Wi-Fi 資格情報のオフセットは別途 `src/storage/config.c` で
`PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`。

---

## 5. 起動フローとブートローダ

### ハードウェアブート

```
RP2040 ROM bootloader
      ↓
0x10000000 の boot2 (256 B) がロード & 実行
      ↓
XIP 有効化 & 0x10000100 (vector table) にジャンプ
      ↓
ブートローダの Reset_Handler
```

### ブートローダの判定ロジック(`bootloader/main.c`)

```
metadata 読み出し (magic + CRC 検証)
  │
  ├─ 無効 → slot A に有効な vector があれば初期メタデータ書いて slot A へ
  │                             │
  │                             └─ なければ USB BOOTSEL に落ちる
  │
  ├─ pending_verify=1 かつ boot_attempts >= MAX
  │   → active_slot を反転、フラグクリア、メタデータ書き戻し、watchdog リブート
  │
  ├─ pending_verify=1 → boot_attempts++ でメタデータ書き戻し
  │
  ├─ active_slot 側の vector が無効
  │   → 反対側が有効なら切り替え、両方ダメなら BOOTSEL
  │
  └─ 有効な側にジャンプ:
        VTOR = slot_base + 0x100
        SP   = *(slot_base + 0x100)
        PC   = *(slot_base + 0x104)
        msr msp; bx pc
```

### アプリ側

- `_entry_point` (crt0) は自身の `__vectors` を VTOR に再セットする(念のため)
- `main()` → `ota_init()` がメタデータを読み `PICOBLE_SLOT` と照合、
  不整合なら警告ログ
- 30 秒後、`ota_confirm()` が自動発火 → `pending_verify` と `boot_attempts` をクリア

### 手動ロールバック

- `ota confirm` を打たずにリセットを繰り返せば、3 回目でブートローダが自動ロールバック
- ユーザ側から明示的にロールバックしたいときは、
  `ota download` で前スロット用の .bin を焼き直して `ota apply` で戻す

---

## 6. CLI プロトコル

### 行受信

- `cli_feed(ctx, data, len)`: バイト列を受け取り、行末で `dispatch()`
- 行末: `\n` / `\r` / `\r\n` すべて対応
- 制御文字(< 0x20)は基本ドロップ、`\b` (0x08) と DEL (0x7F) はバックスペース
- 行バッファ長は `CLI_LINE_MAX = 256`、超過は無音ドロップ

### BLE 固有: パケット境界ディスパッチ

Bluefruit Connect などのクライアントは行末を付けずに送ってくる。
BLE 側の受信では `cli_feed_line()` を使い、
「バッファに何か残った状態でパケットが終わったら LF を補って即ディスパッチ」する
(USB CDC はバイトストリームなので通常の `cli_feed()`)。

### 引数パーサ(`src/cli/parser.c`)

- 空白(スペース + タブ)区切り
- ダブルクォート `"..."` で括った領域はそのまま(空白保持)
- クォート内の `\"` と `\\` はエスケープ
- **クォート外のバックスラッシュはそのまま**(現在はエスケープしない)
- 最大 `CLI_MAX_ARGS = 16` 引数、超過は `CLI_PARSE_ERR_TOO_MANY_ARGS`
- クォート未閉じは `CLI_PARSE_ERR_UNCLOSED_QUOTE`

パーサは入力バッファをその場でトークン化する(NUL を打って argv[] を組む)。
呼び出し側は argv 有効な間バッファを解放してはいけない。

### コマンドディスパッチ

- レジストリは name のアルファベット順のリンクリスト
- `cli_command_find(name)` で線形検索
- ヒットしなければ `<name>: command not found`
- ハンドラが `CLI_ERR_USAGE` を返したら、レジストリ登録時の `usage` 文字列で
  `Usage:\n  <usage>\n` を自動表示

### 出力慣習

- 改行は `\r\n`(BLE クライアント互換のため)
- コマンドが「成功して何も表示しない」ときは、
  ディスパッチ後の `> ` プロンプトが唯一の応答
- エラーは Linux 慣習: `<command>: <what happened>`

---

## 7. コマンドを追加する

**★ 最も重要な拡張点 ★**

### 最小テンプレート

`src/cli/commands/cmd_<name>.c`:

```c
#include "cli/cli.h"
#include "cli/command.h"

static int cmd_hello(int argc, char **argv, cli_ctx_t *ctx) {
    (void)argc; (void)argv;
    cli_write(ctx, "hello, world\r\n");
    return CLI_OK;
}

CLI_COMMAND_REGISTER(hello,
    "greet the caller",       // help 一覧に出る 1 行サマリ
    "hello",                   // Usage 表示に使う
    cmd_hello);
```

### 登録マクロの仕組み

```c
#define CLI_COMMAND_REGISTER(name_, summary_, usage_, handler_)         \
    static cli_command_t _cli_cmd_##name_ = { ... };                     \
    __attribute__((constructor))                                         \
    static void _cli_cmd_ctor_##name_(void) {                            \
        cli_command_register(&_cli_cmd_##name_);                         \
    }
```

- GCC の `__attribute__((constructor))` により `main()` より前に発火
- pico-sdk の crt0 が `__libc_init_array` を呼ぶ経路で走る
- `cli_command_register` は insertion sort でレジストリに繋ぐ
  → **help 表示順は登録順ではなく name 昇順**で決まる

### ハンドラの契約

```c
typedef int (*cli_handler_fn)(int argc, char **argv, cli_ctx_t *ctx);
```

| 項目 | 説明 |
|---|---|
| `argc` | 引数の総数(コマンド名を含む。最低 1) |
| `argv[0]` | コマンド名 |
| `argv[1..]` | 引数(NUL 終端、パーサが解釈済み) |
| `ctx` | 出力先 CLI 文脈。BLE 用 / USB 用が呼び出しごとに異なる |
| 戻り値 | `CLI_OK` / `CLI_ERR_USAGE` / `CLI_ERR_ARG` / `CLI_ERR_HARDWARE` / `CLI_ERR_UNSUPPORTED` |

### 出力ヘルパ

```c
int cli_write  (cli_ctx_t *ctx, const char *s);          // NUL 終端
int cli_writen (cli_ctx_t *ctx, const char *s, size_t n); // バイト長
int cli_printf (cli_ctx_t *ctx, const char *fmt, ...);   // format 印字
```

内部で `vsnprintf` を stack バッファ(256 B)に一発、
sink 側は完成した文字列を受け取る。1 出力あたり 256 B 上限。

### 引数バリデーションの定石

**数値パース(pin 番号など):**

```c
static bool parse_pin(const char *s, uint *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0') return false;   // 数字以外の混入を拒否
    if (v > 29) return false;
    *out = (uint)v;
    return true;
}
```

**サブコマンド分岐:**

```c
static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;
    if (strcmp(argv[1], "read") == 0) return sub_read(ctx);
    if (strcmp(argv[1], "high") == 0) return sub_high(ctx);
    cli_printf(ctx, "%s: unknown subcommand: %s\r\n", argv[0], argv[1]);
    return CLI_ERR_USAGE;
}
```

`argv[0]` を使うと `hello: unknown ...` のようにコマンド名が入る自然な形になる。

### Usage 表示のフォーマット規約

- 縦棒 `|` は「どれか一つ」を意味する(パーサは解釈しない、表示上の慣習)
- 波括弧 `< >` は必須引数、角括弧 `[ ]` は省略可
- 例: `wifi status | connect <ssid> <psk> | forget`

`return CLI_ERR_USAGE` すると自動で表示される:

```
Usage:
  wifi status | connect <ssid> <psk> | forget
```

### エラー時の文字列

- 「なぜ失敗したか」を短く: `wifi: not connected`, `gpio: invalid pin: 42`
- 「Usage:」プレフィックスは自前で書かない(自動発行される)
- 改行は `\r\n`

### フルスケール例: `led` コマンド(サブコマンド + 数値 + 状態遷移)

```c
// src/cli/commands/cmd_led.c
#include <string.h>
#include <stdlib.h>

#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/gpio_ctrl.h"

static bool parse_pin(const char *s, uint *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0' || v > GPIO_CTRL_MAX_PIN) return false;
    *out = (uint)v;
    return true;
}

static int cmd_led(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    uint pin;
    if (!parse_pin(argv[1], &pin)) {
        cli_printf(ctx, "led: bad pin: %s\r\n", argv[1]);
        return CLI_ERR_ARG;
    }

    const char *op = argv[2];
    if (strcmp(op, "on") == 0)   return gpio_ctrl_write(pin, true)  ? CLI_OK : CLI_ERR_HARDWARE;
    if (strcmp(op, "off") == 0)  return gpio_ctrl_write(pin, false) ? CLI_OK : CLI_ERR_HARDWARE;
    if (strcmp(op, "blink") == 0) {
        for (int i = 0; i < 6; i++) {
            gpio_ctrl_toggle(pin);
            sleep_ms(120);
        }
        return CLI_OK;
    }
    cli_printf(ctx, "led: unknown action: %s\r\n", op);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(led,
    "LED control shorthand",
    "led <pin> on | off | blink",
    cmd_led);
```

`cmake -S . -B build` → `cmake --build build -j` → 焼き直しで
`led 5 blink` が使えるようになる。

### コマンドを削除する

該当ファイルを消せば OK。**中央のテーブルを直す必要は無い**。

### コマンドの絶対規則

- **副作用は最小限**: グローバル状態を書き換えるならログを残す
- **ブロッキング呼び出しは short-timeout に**: BLE 接続が長時間止まると切れる
- **必ず `\r\n`**: `\n` だけだと一部クライアントで表示崩れ
- **`cli_ctx_t` 越しに出力**: `printf` は BLE には届かない

---

## 8. ドライバを追加する

### 構造の型

`src/drivers/<name>_ctrl.h/.c` に「pico-sdk 呼び出しの薄いラッパ」を書く。
CLI コマンドは `drivers/*.h` のみ include し、`hardware/*.h` は直接触らない。

### lazy init パターン

初回アクセス時に SDK の `xxx_init()` を呼ぶ:

```c
static uint32_t g_pin_ready_mask = 0;   // 初期化済みピンのビットマップ

static void ensure_init(uint pin) {
    uint32_t bit = 1u << pin;
    if (!(g_pin_ready_mask & bit)) {
        gpio_init(pin);
        g_pin_ready_mask |= bit;
    }
}
```

**利点**: main.c で `xxx_init_all()` を呼ぶ順序管理が不要。
CLI コマンド側は素直に read/write するだけ。

### 予約リソースの申告

Pico W では GPIO 23-25 が CYW43 の SPI に使われている。
`gpio_ctrl_is_reserved()` のような関数で当該情報を提供し、
CLI コマンドは警告するが処理は続行する
(パワーユーザが意図的に触ることは許容する)。

### バリデーションの境界

- **ドライバ層**: 範囲外 pin を拒否(false 返し)
- **CLI 層**: `argv` の文字列を解釈して数値化 + 範囲チェック
- **ユーザ入力の到達点はドライバ層のみ**: OS からのユーザ層とハードウェアの境界

---

## 9. ログサブシステム

### モデル

```c
typedef struct log_sink {
    void (*write)(log_level_t level, const char *msg, size_t len, void *user);
    void *user;
    struct log_sink *_next;
} log_sink_t;

void log_add_sink(log_sink_t *sink);
void log_write(log_level_t level, const char *fmt, ...);
```

- sink はリンクリスト、`log_write` は全 sink にファンアウト
- レベルフィルタは共通(`log_set_level(LOG_LEVEL_INFO)` など)
- **デフォルトの sink は無い**: main.c が明示的に登録する

### 新しい sink の追加例

```c
// たとえば SD カード or LittleFS 経由でファイルログを取りたいとき
static void filelog_sink_write(log_level_t lvl, const char *msg, size_t len, void *user) {
    file_t *f = (file_t *)user;
    write_line_with_level(f, lvl, msg, len);
}

static log_sink_t g_filelog = { .write = filelog_sink_write, .user = &g_log_file };

// boot 時
log_add_sink(&g_filelog);
```

### BLE に流さない理由

BLE ターミナルは対話シェルなので、
非同期ログ行が入力中に割り込むと画面が崩れる。
現状の main.c では USB CDC のみに sink 登録。
将来「BLE デバッグモード」を作るなら、
別 characteristic に流すか、レベル ERROR 以上のみに絞ると良い。

---

## 10. BLE / NUS 仕様

### サービス / 特性 UUID

Nordic UART Service(NUS):

- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic(WRITE / WRITE_WITHOUT_RESPONSE): `6E400002-...`(Pico が受信)
- TX Characteristic(NOTIFY): `6E400003-...`(Pico が送信)

### 接続ライフサイクル

1. `HCI_EVENT_META_GAP` + `GAP_SUBEVENT_LE_CONNECTION_COMPLETE` で接続確立
2. Central が TX の CCCD に notify enable を書き込む
3. その瞬間に `cli_greet()` を発火(バナー + 最初のプロンプト送信)
4. RX への write を `on_ble_rx` → `cli_feed_line()` に流す
5. 切断で `HCI_EVENT_DISCONNECTION_COMPLETE` を受け、`cli_reset()` + 再アドバタイズ

### MTU とチャンク分割

- デフォルト ATT MTU 23 → notify payload は 20 B
- Bluefruit のような Central は 500+ B に MTU 拡張する場合がある
- `att_server_get_mtu(handle) - 3` を毎回参照して chunk サイズを決定
- 1 KB の TX リングバッファに溢れた分は無音ドロップ(ベストエフォート)

### GATT 生成

`src/ble/nus.gatt` → `compile_gatt.py` → `${TARGET}_gatt_header/nus.h` (CMake タイミングで自動)。
handle 名は `ATT_CHARACTERISTIC_<UUID部分>_01_VALUE_HANDLE` 形式。

---

## 11. Wi-Fi + HTTP + LwIP 構成

### CYW43 arch モード: `pico_cyw43_arch_lwip_poll`

**なぜ poll モードなのか(重要)**:

`pico_cyw43_arch_lwip_threadsafe_background` を使うと、
BTStack と cyw43_arch が **同じ async_context を共有** する。
BLE パケットハンドラ(= CLI コマンド)はその async_context 内で走るため、
コマンド内で LwIP の完了を待つとデッドロックする
(待たれている側が自分自身なので永久に処理されない)。

Poll モードでは LwIP は `cyw43_arch_poll()` を呼ぶまで進まない。
5 ms 間隔の BTStack タイマ + 各コマンドの待機ループ内で明示的に叩く。

### 自動接続の順序

1. `config_load_wifi()` で flash から資格情報を読む
2. なければ `WIFI_SSID`/`WIFI_PASSWORD` (compile define) を試す
3. どちらもなければ手動接続待ち

タイミング: 起動 2 秒後の BTStack timer で発火(BLE アドバタイズ確立後)。

### HTTP クライアント (`src/network/http_get.c`)

- LwIP raw TCP API 直接使用
- 対応: HTTP/1.1 GET、Content-Length ベースの終了判定、
  接続クローズによる終了判定
- 非対応: HTTPS、chunked transfer-encoding、認証、リダイレクト
- URL: `http://host[:port]/path` (IP リテラルか DNS 解決可能なホスト名)
- Body は chunk コールバックでストリーム受け渡し(全部メモリに載せない)

### エラーコード

- `HTTP_ERR_PARSE` (-1): URL パース失敗
- `HTTP_ERR_DNS` (-2): DNS 失敗
- `HTTP_ERR_CONNECT` (-3): TCP connect 失敗(即時)
- `HTTP_ERR_HTTP_STATUS` (-4): 非 2xx
- `HTTP_ERR_ABORTED` (-5): LwIP `tcp_err_cb` 発火(RST / タイムアウトなど)
- `HTTP_ERR_TIMEOUT` (-6): 総合デッドライン到達
- `HTTP_ERR_NOMEM` (-7): pbuf / TCP write 圧迫
- `HTTP_ERR_INTERNAL` (-8): 内部状態不整合

失敗時は `http_get_last_lwip_err()` で LwIP の err_t を取れる。

---

## 12. OTA プロトコル

### メタデータレコード

`include/ota_metadata.h`:

```c
#define OTA_META_MAGIC     0x4F544131u   // 'OTA1'
#define OTA_META_VERSION   1u
#define OTA_MAX_BOOT_ATTEMPTS 3u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  active_slot;              // 0=A, 1=B
    uint8_t  pending_verify;
    uint8_t  boot_attempts;
    uint8_t  reserved[3];
    uint32_t image_size[2];
    uint8_t  image_sha256[2][32];
    uint32_t crc32;                     // 全フィールドに対する IEEE CRC-32
} __attribute__((packed)) ota_metadata_t;
```

`0x184000` の 1 セクタに記録。ページ書き込みは 256 B なので、
レコード全体は 1 ページに収まる(120 B 程度)。

### CRC 計算

- 多項式: `0xEDB88320`(IEEE 802.3 標準の反転版)
- `config.c`、`ota.c`、`bootloader/main.c` の 3 箇所に同じ実装がある
  (どちらか一方だけを更新するとブートローダとアプリで解釈が食い違うので注意)

### apply シーケンス

```
アプリ:
  ota_apply()
    ├─ 現在のメタデータを RAM に読み、必要フィールドを更新
    │     active_slot = staging_slot
    │     pending_verify = 1
    │     boot_attempts = 0
    │     image_size[new_active] = bytes_written
    │     image_sha256[new_active] = computed_sha256
    ├─ CRC を計算して書き戻し
    ├─ sleep_ms(50) で BLE flush
    └─ watchdog_reboot(0, 0, 0)

再起動:
  ROM → bootloader
  bootloader:
    ├─ metadata OK
    ├─ pending_verify=1, boot_attempts=0 < MAX
    ├─ boot_attempts=1 でメタデータ書き戻し
    └─ 新スロットの vector table にジャンプ

新アプリ:
  main() → ota_init() (pending_verify=1 を検出)
       → 30 秒後の timer が ota_confirm() を発火
       → pending_verify=0, boot_attempts=0 に書き戻す
```

### rollback シーケンス

```
新アプリが起動失敗 or 30 秒以内にリセット
  → 次回起動時: bootloader が pending_verify=1, boot_attempts=1 を見て 2 に上げる
  → さらに失敗が続いて boot_attempts=3 に到達
    ↓
  → 次の起動時:
       active_slot ^= 1
       pending_verify = 0
       boot_attempts = 0
       書き戻し + watchdog_reboot
    ↓
  旧スロットで正常起動
```

### confirm の手動 vs 自動

- **自動**: main.c の BTStack timer が起動 30 秒後に `ota_confirm()` 発火
- **手動**: `ota confirm` コマンドで即時発火(long soak テスト用)
- 一度 confirm されたら OTA_MAX_BOOT_ATTEMPTS のカウントも 0 に戻る

### ステージング書き込み

- staging slot は「自分でない方」(compile time の `PICOBLE_SLOT` から決定)
- `ota_begin()` で slot 全域(768 KB)を 4 KB セクタ単位で erase
  (CYW43 との協調のため 1 セクタずつ)
- `ota_write()` は 256 B ページ単位でバッファリング & プログラム
- `ota_finalize()` で末端の partial page を 0xFF パディングして書き、
  SHA-256 を確定

---

## 13. 設定保存

### スキーマ

`src/storage/config.c` — 現状 Wi-Fi 資格情報のみ:

```c
typedef struct {
    uint32_t magic;         // 'PCCG'
    uint16_t version;
    uint16_t reserved;
    char     ssid[CONFIG_SSID_MAXLEN + 1];   // 33 B
    char     psk[CONFIG_PSK_MAXLEN + 1];     // 64 B
    uint32_t crc32;
} __attribute__((packed)) config_record_t;
```

`0x1FF000` の 1 セクタに書く。ページ書き込みは 256 B、レコードは 100 B 程度。

### 拡張(将来)

- schema バージョン管理: `version` フィールドで migrate
- 汎用 KV: 新レコード形式で先頭 `type` フィールド + tagged variant
- **既存の Wi-Fi レコードとの互換は magic で区別すること**

---

## 14. エラーコード一覧

### CLI ハンドラ返り値

| 定数 | 値 | 意味 |
|---|---|---|
| `CLI_OK` | 0 | 成功 |
| `CLI_ERR_USAGE` | 1 | 引数不足など → Usage を自動表示 |
| `CLI_ERR_ARG` | 2 | 引数の内容が不正 |
| `CLI_ERR_HARDWARE` | 3 | 下位レイヤ失敗 |
| `CLI_ERR_UNSUPPORTED` | 4 | 未実装 |

### OTA 結果コード

| 定数 | 値 | 意味 |
|---|---|---|
| `OTA_OK` | 0 | 成功 |
| `OTA_ERR_BUSY` | -1 | 別の update 進行中 |
| `OTA_ERR_NOT_STARTED` | -2 | `ota_begin` していない |
| `OTA_ERR_SIZE` | -3 | サイズが slot を超える |
| `OTA_ERR_RANGE` | -4 | offset / len が不正 |
| `OTA_ERR_VERIFY` | -5 | SHA-256 不一致 |
| `OTA_ERR_UNSUPPORTED` | -6 | 未実装 |
| `OTA_ERR_IO` | -7 | flash op 失敗 |
| `OTA_ERR_NO_IMAGE` | -8 | apply で image_ready でない |

### HTTP 結果コード

`network/http_get.h` 参照(§11 に列挙済み)。

---

## 15. ハマりどころ (トラブルシューティング)

### コマンドが Usage を返す

- 引数パーサに空白系のバグは無いか
  → `echo "a b c" d` などで確認
- サブコマンドの `strcmp` が case-sensitive(仕様通り)

### BLE 接続はできるが応答が来ない

- クライアントの EOL 設定は関係無い
  (`cli_feed_line()` がパケット境界でディスパッチ)
- notify subscribe されているか(bit 0x0001 を CCCD に書き込み)
- `att_server_notify` が MTU 超えを送っていないか

### Wi-Fi 接続後 TCP が全く通らない

- `net info` で netif フラグ・IP・gateway を確認
- `net arp <gateway>` で L2 到達確認
- **poll モードの `cyw43_arch_poll()` を待機ループで叩いているか**
  (threadsafe_background に切り替えるとデッドロック再発)
- ルータの client isolation / privacy separator を疑う

### OTA download 中に BLE が切れる

- 8 秒の erase 中、~45 ms 単位で IRQ が止まるので接続が切れる場合がある
- 実害は無い(erase 完了後 Wi-Fi は復帰、CLI 応答も戻る)
- 断続が気になる場合は `ota_begin` を非同期タスクに移すのが将来対策

### 新しいスロットが起動しない → 自動ロールバック

- リンクアドレスが合っていない(A 用 .bin を B スロットに書くなど)
- SDK 依存のシンボルが飛んでいる(BSS 初期化順など)
- 復旧: 3 回リセットが積み上がって旧スロットに戻るのを待つ

### `net info` の ARP に見知らぬエントリ

- Buffalo などのルータが独自 172.31.x.x で管理応答することがある
- 実害無し、無視して OK

---

## 参考: 主要な公開ヘッダ

- `src/cli/cli.h` — CLI 出力ヘルパ、`cli_ctx_t`
- `src/cli/command.h` — `CLI_COMMAND_REGISTER` マクロと契約
- `src/cli/parser.h` — 引数パーサ
- `src/system/log.h` — ログ sink 型
- `src/system/sha256.h` — 自前 SHA-256
- `src/drivers/gpio_ctrl.h` — GPIO ラッパ
- `src/drivers/adc_ctrl.h` — ADC ラッパ
- `src/network/wifi.h` — Wi-Fi STA
- `src/network/http_get.h` — HTTP GET
- `src/storage/config.h` — flash 内 KV
- `src/ota/ota.h` — OTA API
- `include/ota_metadata.h` — メタデータ struct + パーティション定数
- `include/btstack_config.h` — BTStack 設定
- `include/lwipopts.h` — LwIP 設定
