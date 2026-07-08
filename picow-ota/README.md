# pico-ota

Raspberry Pi Pico W (RP2040) 用の **デュアルスロット OTA フレームワーク**。
独立ブートローダ + 2 スロット + メタデータ + 自動ロールバックを提供します。

- ボード: **Pico W / Pico 2 W** (CYW43 搭載モデル)
- ホストアプリ側の依存: `hardware_flash`, `pico_flash`(pico-sdk 標準)
- 提供機能:
  - `pico_ota_bootloader` — 0x10000000 に置く小さな独立ブートローダ (~10 KB)
  - `pico_ota` INTERFACE ライブラリ — アプリ側 OTA API
  - `pico_ota_metadata` INTERFACE ライブラリ — ヘッダ + CRC-32(ブートローダとアプリの共有)
  - `pico_ota_set_slot()` CMake ヘルパ — スロット別リンカ + `PICO_OTA_SLOT` define

## パーティション構成

`include/pico_ota/metadata.h` の定数がすべての真実:

```
0x000000..0x003FFF   bootloader (16 KB)
0x004000..0x0C3FFF   slot A     (768 KB)
0x0C4000..0x183FFF   slot B     (768 KB)
0x184000..0x184FFF   metadata   (4 KB, 1 セクタ)
```

必要に応じてこのヘッダの定数を書き換えれば任意のオフセットに再配置できますが、
リンカスクリプト側(`linker/memmap_app_slot_*.ld`)の `ORIGIN` も同期する必要があります。

## 使い方(アプリ側 CMakeLists.txt)

```cmake
add_subdirectory(picow-ota)

set(APP_SOURCES main.c my_app.c ...)

function(add_app_slot letter id)
    set(target myapp_${letter})
    add_executable(${target} ${APP_SOURCES})
    target_link_libraries(${target} PRIVATE
        pico_stdlib
        pico_cyw43_arch_lwip_poll
        pico_ota            # ← ここで OTA API を有効化
        # ...他の依存...
    )
    pico_ota_set_slot(${target} ${id})   # ← スロット別リンカ + define
    pico_add_extra_outputs(${target})
endfunction()

add_app_slot(a 0)
add_app_slot(b 1)
```

これで以下が生成されます:

- `myapp_a.uf2` — スロット A 用 (0x10004000 起点)
- `myapp_b.uf2` — スロット B 用 (0x100C4000 起点)
- `pico_ota_bootloader.uf2` — 共通ブートローダ (0x10000000 起点)

## 初回書き込み

BOOTSEL モードで 2 ファイルを順にドラッグ:

1. `pico_ota_bootloader.uf2`
2. `myapp_a.uf2`

ブートローダはメタデータが空でも slot A に有効な vector があれば
自動的にメタデータを初期化して slot A を起動します。

## アプリ側 API

```c
#include "pico_ota/ota.h"

void ota_init(void);                       // main() の頭で呼ぶ
ota_status_t ota_status(void);             // 現状取得(active/staging/pending 等)

ota_result_t ota_begin(size_t image_size); // staging slot 消去 (~8s)
ota_result_t ota_write(size_t offset,
                       const uint8_t *data, size_t len);
ota_result_t ota_finalize(void);           // 末端ページ書き + SHA-256 確定
ota_result_t ota_verify(const uint8_t expected[32]);
ota_result_t ota_apply(void);              // メタデータ書換 + reboot
void         ota_confirm(void);            // pending_verify クリア
ota_result_t ota_abort(void);              // 途中放棄
```

典型的な流れ:

```c
ota_init();
// ... 何らかの経路で新しいイメージを取得 ...
ota_begin(image_size);
while (has_more_data()) {
    ota_write(offset, chunk, chunk_len);
}
ota_finalize();
if (ota_verify(expected_sha256) == OTA_OK) {
    ota_apply();  // reboot into new slot (probation)
}
// 新イメージが 30 秒以内に ota_confirm() を呼ばないと、
// 3 回リセットで自動ロールバック
```

## ロールバック挙動

`ota_apply()` 実行時にメタデータは以下になる:
```
active_slot   = <新スロット>
pending_verify = 1
boot_attempts  = 0
```

ブートローダは起動毎に:
- `pending_verify=1 && boot_attempts >= 3` → `active_slot` 反転 + フラグクリア + reboot
- そうでなければ `boot_attempts++` してから active_slot にジャンプ

新アプリが `ota_confirm()` を呼べば `pending_verify=0` + `boot_attempts=0` にクリア。

## ロギング

`pico_ota/log.h` の 3 関数を weak シンボルで提供しています。
アプリ側で strong に定義すれば、フレームワークが吐くログを自前のシステムに流せます:

```c
#include "pico_ota/log.h"

void pico_ota_log_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    my_log(LOG_INFO, fmt, ap);
    va_end(ap);
}
// pico_ota_log_warn / pico_ota_log_error も同様
```

何も override しなければ完全に silent。

## メタデータレコード

`include/pico_ota/metadata.h`:

```c
typedef struct {
    uint32_t magic;              // 0x4F544131 'OTA1'
    uint16_t version;            // 1
    uint8_t  active_slot;        // 0=A, 1=B
    uint8_t  pending_verify;
    uint8_t  boot_attempts;
    uint8_t  reserved[3];
    uint32_t image_size[2];
    uint8_t  image_sha256[2][32];
    uint32_t crc32;              // IEEE CRC-32
} __attribute__((packed)) ota_metadata_t;
```

サイズは 1 flash ページ (256 B) に収まります。

## ディレクトリ構成

```
picow-ota/
  CMakeLists.txt              # pico_ota / pico_ota_metadata の export
  README.md                   # このファイル
  bootloader/
    CMakeLists.txt            # pico_ota_bootloader ターゲット
    main.c                    # ~10 KB のブートローダ本体
  linker/
    memmap_bootloader.ld      # 0x10000000 / 16 KB
    memmap_app_slot_a.ld      # 0x10004000 / 768 KB
    memmap_app_slot_b.ld      # 0x100C4000 / 768 KB
  include/pico_ota/
    metadata.h                # 共有 struct + パーティション定数
    ota.h                     # アプリ API
    sha256.h
    crc32.h
    log.h                     # weak ロギングフック
  src/
    ota.c
    sha256.c
    crc32.c
    log_default.c             # weak no-op ロガー
```

## 制約 / 注意

- **RP2040 / 2 MB flash 前提** の定数(RP2350 でも動くはずですが未検証)
- **署名検証は無い** — SHA-256 は完全性のみ。改ざん耐性が必要な用途では
  Ed25519 などを追加すべき
- **HTTPS はフレームワークが提供しない** — 転送層はアプリ次第
- **アプリは 2 種類ビルド必要** — RP2040 は XIP 実行なので、
  スロット A 用と B 用は別リンクアドレスで別イメージ

## 参考

- 元 PoC 実装: [PicoBLE-Terminal](../) — このフレームワークが抽出された元のプロジェクト
- pico-sdk 標準リンカ: `pico-sdk/src/rp2_common/pico_crt0/rp2040/memmap_default.ld`
