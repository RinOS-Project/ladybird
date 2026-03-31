# Ladybird → RinOS 完全移植ガイド

本ドキュメントは `libs/ladybird/` のLadybirdソースをRinOS固有の依存に完全に書き換える
移植プロジェクトの方針・設計判断・完了条件を網羅的に記録するものです。

---

## 1. 移植の目的

RinOS独自Webエンジン基盤として、upstreamのLadybirdコードを取り込みつつ、
外部プラットフォーム依存（Skia, OpenSSL, curl, ICU, libtommath, HarfBuzz/FreeType,
Vulkan, Metal 等）を**全て除去**し、RinOS純正ライブラリで完全に置き換える。

### 対象アーキテクチャ
- **i386** (32-bit)
- **x86_64** (64-bit)

### 維持するコンポーネント
- **LibJS** — JavaScript エンジン（Ladybird純正を維持）
- **AK** — ユーティリティ（RinOS platform define 追加のみ）
- **fmt** — `build/ladybird-third-party/fmt/`（ヘッダオンリー、維持）

### 無効化する機能 (Feature Profile)
| 機能 | 状態 |
|---|---|
| Web Workers | OFF |
| WebAssembly | OFF |
| Media (Audio/Video) | OFF |
| WebGL | OFF |
| Service Workers | OFF |

---

## 2. 依存置換マッピング

| Ladybird外部依存 | RinOS置換先 | 影響範囲 | Phase |
|---|---|---|---|
| Skia (2D/GPU rendering) | `libs/aquamarine/` | LibGfx, LibWeb/Painting | 5 |
| Vulkan / Metal | 除去（ソフトウェアレンダリング） | LibGfx | 5 |
| OpenSSL (crypto) | `libs/rintls/crypto/` | LibCrypto | 2 |
| OpenSSL (SSL/TLS) | `libs/rintls/` (TLS record/handshake) | LibTLS | 2 |
| libtommath (bignum) | `libs/rintls/crypto/bignum.h` | LibCrypto | 2 |
| curl (HTTP client) | `src/apps/workerd/` (workerd daemon) | Services/RequestServer | 4 |
| ICU 78.2 | `libs/rinicu/` (IPC client → rinicud) | LibUnicode | 3 |
| Rust crate (libunicode_rust) | C/C++ 代替 (`libs/libunicode/`) | LibUnicode | 3 |
| HarfBuzz / FreeType | aquamarine TrueType + stb_truetype | LibGfx/Font | 5 |
| Fontconfig | 除去（固定フォントパス） | LibGfx/Font | 5 |
| WOFF2 | 最小限の自前デコーダ | LibGfx/Font | 5 |
| libjpeg-turbo | `libs/jpeg/` | LibGfx/ImageFormats | 5 |
| libpng | `libs/png/` | LibGfx/ImageFormats | 5 |
| libwebp | `libs/webp/` | LibGfx/ImageFormats | 5 |
| libavif / libjxl / TIFF | 除外（将来追加） | LibGfx/ImageFormats | 5 |
| simdutf | `libs/libunicode/` | LibUnicode | 3 |

---

## 3. フェーズ定義

### Phase 0: ドキュメント・基盤整備
- **目標**: 移植方針ドキュメント作成、プラットフォームdefine追加、TODO追跡
- **成果物**:
  - `Documentation/RinOS_Porting.md` (本ドキュメント)
  - `AK/Platform.h` に `AK_OS_RINOS` 追加
  - `TODO.md` にLadybirdセクション追加
- **完了条件**: `AK_OS_RINOS` がビルド時に定義されること

### Phase 1: プラットフォーム基盤 (AK + LibCore + LibIPC)
- **目標**: Ladybird基盤ライブラリをRinOS上で動作させる
- **対象ファイル**:
  - `AK/Platform.h` — `AK_OS_RINOS` 定義、POSIX互換フラグ
  - `AK/StackInfo.cpp` — RinOS用スタック情報取得
  - `Libraries/LibCore/System.cpp` — syscallラッパー
  - `Libraries/LibCore/EventLoop.cpp` — `poll()`ベースイベントループ
  - `Libraries/LibCore/Socket.cpp` — AF_UNIX/AF_INET
  - `Libraries/LibCore/Process.cpp` — fork/exec/waitpid
  - `Libraries/LibCore/File.cpp`, `MappedFile.cpp`
  - `Libraries/LibIPC/` — Unix domain socket IPC
- **完了条件**:
  - AK単体テスト（コンテナ、文字列、ストリーム）がRinOSターゲットでコンパイル通過
  - LibCore EventLoop + Socket + File の基本テスト通過
  - AF_UNIXソケットでecho往復テスト通過

### Phase 2: 暗号・TLS置換 (rintls)
- **目標**: OpenSSL / libtommath 依存を完全に除去し、rintls で置換
- **置換マッピング**:
  | LibCrypto モジュール | rintls 置換先 |
  |---|---|
  | `Cipher/AES.cpp` | `rintls/crypto/aes.h` |
  | `Hash/SHA1.cpp` | `rintls/crypto/sha1.h` |
  | `Hash/SHA2.cpp` | `rintls/crypto/sha256.h` |
  | `Authentication/HMAC.cpp` | `rintls/crypto/hmac.h` |
  | `BigInt/*.cpp` | `rintls/crypto/bignum.h` |
  | `PK/RSA.cpp` | `rintls/crypto/rsa.h` |
  | `Curves/SECPxxxr1.cpp` | `rintls/crypto/ecdh.h` |
  | `Certificate/Certificate.cpp` | `rintls/x509/cert.h` |
- **Web Crypto必須で rintls に無い暗号**: ChaCha20, BLAKE2b, SHA-3, MD5 → 自前C実装追加
- **スコープ外**: ML-KEM, ML-DSA (post-quantum) → stub (`ENOSYS`)
- **LibTLS**: `TLSv12.cpp` を `rintls_new()/handshake()/send()/recv()` ラッパーに置換
- **完了条件**:
  - AES/SHA/HMAC/RSA/ECDH/X.509 テスト通過
  - TLS 1.2/1.3 ハンドシェイク成功
  - `nm` で OpenSSL/libtommath シンボル参照ゼロ

### Phase 3: Unicode/ICU置換 (rinicu + libunicode)
- **目標**: ICU 78.2 + Rust crate 依存を除去し、rinicu IPC + libunicode で置換
- **置換マッピング**:
  | LibUnicode 機能 | 置換先 |
  |---|---|
  | Locale | `rin_icu_locale_*()` via rinicu IPC |
  | Collator | `rin_icu_collator_*()` via rinicu IPC |
  | Segmenter | `rin_icu_segmenter_*()` via rinicu IPC |
  | NumberFormat | `rin_icu_number_formatter_*()` via rinicu IPC |
  | DateTimeFormat | `rin_icu_datetime_formatter_*()` via rinicu IPC |
  | PluralRules | `rin_icu_plural_rules_*()` via rinicu IPC |
  | DisplayNames | `rin_icu_display_name()` via rinicu IPC |
  | ListFormat | `rin_icu_list_format()` via rinicu IPC |
  | RelativeTimeFormat | `rin_icu_relative_time_format()` via rinicu IPC |
  | TimeZone | `rin_icu_time_zone_*()` via rinicu IPC |
  | IDNA | `rin_icu_*()` via rinicu IPC |
  | Normalize | `rin_unicode_normalize_utf8()` via libunicode |
  | CharacterTypes | `rin_unicode_is*()` via libunicode |
  | String (UTF) | `rin_unicode_decode/encode_utf8/16()` via libunicode |
- **完了条件**:
  - 正規化/セグメンテーション/ロケール テスト通過
  - LibJS Intl API テスト動作
  - ICU ヘッダ (`<unicode/*.h>`) / Rust 参照ゼロ

### Phase 4: ネットワーク置換 (workerd + resolved + rintls)
- **目標**: curl / OpenSSL::SSL ネットワーク依存を除去、workerdプロトコルで置換
- **実装**:
  - `LibRequests/WorkerdRequestClient.cpp` 新規作成
    - Unix socket `/tmp/workerd.sock` へ接続
    - `workerd_service_abi.h` プロトコルで fetch 実行
    - SHM 経由の大容量ボディ転送対応
  - `Services/RequestServer/` — curl依存コード削除
  - `LibDNS/` — resolved daemon 経由の名前解決に切替
  - `LibWebSocket/` — rintls + rinhttp 経由で直接実装
- **完了条件**:
  - HTTP/HTTPS fetch テスト通過
  - SHM大容量転送テスト通過
  - DNS解決（resolved経由）テスト通過
  - curl / OpenSSL シンボルゼロ

### Phase 5: 描画置換 (aquamarine) — 最大フェーズ
- **目標**: Skia / Vulkan / Metal を完全除去、aquamarineソフトウェアレンダリングで置換

#### 5A: aquamarine コア拡張
- `aq_path.h/.c` — パスオブジェクト（move/line/bezier/arc/close, scanline fill, stroke）
- `aq_canvas.h/.c` — キャンバス状態スタック（save/restore, 変換, クリッピング）
- `aq_gradient.h/.c` 拡張 — N-stopグラデーション（線形/放射/円錐）
- `aq_effects.h/.c` — ボックスシャドウ、ガウシアンブラー、CSSコンポジットモード
- `aq_truetype.h/.c` — TrueType/OpenTypeフォント（stb_truetypeベース）

#### 5B: LibGfx ブリッジ
- `PainterAquamarine.cpp/.h` — `Gfx::Painter` インターフェース実装
- `PathAquamarine.cpp/.h` — `Gfx::PathImpl` 実装
- `PaintingSurface.cpp` — `SkSurface` → `AqSurface` ラッパー
- `ImmutableBitmap.cpp` — `SkImage` → `AqSurface` (read-only) ラッパー
- `Font/TypefaceAquamarine.cpp/.h` — TrueTypeフォント連携
- Skia ファイル全削除

#### 5C: LibWeb 描画プレイヤー
- `DisplayListPlayerAquamarine.cpp/.h` — 全30+仮想メソッド実装
- `DisplayListPlayerSkia.cpp/.h` 削除

#### 5D: 画像デコーダ
- LibGfx ImageFormats → RinOS自前デコーダ（`libs/png/`, `libs/jpeg/`, `libs/webp/`, `libs/gif/`）
- AVIF/JXL/TIFF は除外

- **完了条件**:
  - aquamarine 単体テスト通過（パス、グラデーション、ブラー、フォント）
  - HTML + CSS + 画像のレンダリング出力確認
  - Skia / Vulkan / Metal シンボルゼロ

### Phase 6: ビルドシステム統合 & vcpkg完全除去
- **目標**: 全外部依存マニフェスト除去、`build_iso.sh` 統合
- **実装**:
  - `vcpkg.json` スリム化（全外部依存除去）
  - `src/webengine/CMakeLists.txt` 完成
  - `RinLadybirdPlatform.cpp/.hpp`, `RinLadybirdRuntime.cpp` 実装
  - `generate_ladybird_buildinfo.py` 実装
  - `scripts/build_iso.sh` にladybirdビルド統合
- **完了条件**:
  - i386/x86_64 両方で cmake 成功
  - ISO生成に ladybird 関連バイナリ含有
  - vcpkg外部パッケージへの参照ゼロ

### Phase 7: 統合テスト & 最終検証 ✅
- **目標**: RinOS上でWebエンジンとして動作する端到端の検証
- **テスト項目**:
  - QEMU上でHTML + CSS + 画像のページ表示
  - JavaScript (DOM操作, Intl API) 実行
  - HTTP/HTTPS fetch、リダイレクト、大容量DL
  - TLS 1.2/1.3 証明書検証
- **完了条件**:
  - 基本Webページが表示されること
  - メモリリーク無し
  - TODO.md全フェーズチェック完了
- **成果物（ソースグレップ統合テスト）**:
  - `tests/ladybird_rinos_porting_phase8_smoke_test.c`
    - Phase 0-7 全フェーズの不変条件を検証
    - AK_OS_RINOS ガード存在、CMake条件化、禁止シンボル非存在
    - rintls/rinicu/aquamarine 置換パターン確認
    - vcpkg-rinos.json 依存マッピング一貫性検証
  - `tests/webengine_ladybird_artifact_smoke_test.c`
    - 全フェーズの成果物ファイル存在確認（40+ファイル）
    - Phase 0 (Platform.h) から Phase 8 (テスト自身) まで網羅

---

## 4. コーディング規約

### プラットフォーム分岐
- RinOS固有コードは `#ifdef AK_OS_RINOS` で囲む
- `AK` と `LibCore` 以外のライブラリには `#ifdef` を設置しない（ブリッジ層で吸収）
- 1ファイル内で `#ifdef` が多くなる場合は、別 `.cpp` ファイルに分離する

### RinOS API参照
```cpp
// RinOS native library headers are referenced via absolute include paths:
#include <rintls/rintls.h>          // TLS/crypto
#include <rinicu/rin_icu.h>         // ICU services (IPC client)
#include <libunicode/rin_unicode.h> // Low-level Unicode
#include <aquamarine/aquamarine.h>  // 2D/3D rendering
#include <workerd_service_abi.h>    // workerd wire protocol
```

### コミット規約
- 各フェーズ末にローカル `git commit`
- メッセージ形式: `Phase N: <summary>`
- サブフェーズがある場合: `Phase NA: <summary>`

---

## 5. アーキテクチャ図

```
┌──────────────────────────────────────────────────────────┐
│                      RinOS WebEngine                      │
│                                                          │
│  ┌─────────┐  ┌─────────┐  ┌──────────┐  ┌───────────┐ │
│  │  LibWeb  │  │  LibJS  │  │ LibURL   │  │ LibCore   │ │
│  │ (DOM,CSS │  │ (JS     │  │ (URL     │  │ (OS       │ │
│  │  HTML,   │  │  engine)│  │  parse)  │  │  abstrac.)│ │
│  │  Layout, │  │         │  │          │  │           │ │
│  │  Paint)  │  │         │  │          │  │           │ │
│  └──┬───────┘  └─────────┘  └──────────┘  └───────────┘ │
│     │                                                     │
│  ┌──┴─────────────────┐  ┌──────────────┐                │
│  │ LibGfx             │  │ LibUnicode   │                │
│  │ PainterAquamarine  │  │ rinicu IPC + │                │
│  │ PathAquamarine     │  │ libunicode   │                │
│  │ TypefaceAquamarine │  └──────┬───────┘                │
│  └──┬─────────────────┘         │                        │
│     │                           │                        │
├─────┼───────────────────────────┼────────────────────────┤
│ RinOS Native Libraries          │                        │
│  ┌──┴──────────┐  ┌────────────┴┐  ┌──────────────────┐ │
│  │ aquamarine   │  │ rinicu (IPC)│  │ rintls           │ │
│  │ (2D/3D SW   │  │ → rinicud   │  │ (TLS 1.2/1.3    │ │
│  │  rendering) │  │ + libunicode│  │  AES,RSA,ECDH,   │ │
│  └─────────────┘  └─────────────┘  │  SHA,X.509)      │ │
│                                     └──────────────────┘ │
│  ┌──────────────────────────┐                            │
│  │ workerd (HTTP/image svc) │  ← Unix socket IPC        │
│  │ + resolved (DNS)         │  ← workerd_service_abi.h   │
│  └──────────────────────────┘                            │
└──────────────────────────────────────────────────────────┘
```

---

## 6. 検証チェックリスト

### シンボル検証（各フェーズ末に実行）
```bash
# OpenSSL シンボルがゼロであること
nm -u <binary> | grep -i 'ssl\|openssl\|crypto' && echo "FAIL" || echo "PASS"

# curl シンボルがゼロであること
nm -u <binary> | grep -i 'curl' && echo "FAIL" || echo "PASS"

# ICU シンボルがゼロであること
nm -u <binary> | grep -i 'icu\|u_init\|ucol_\|ubrk_' && echo "FAIL" || echo "PASS"

# Skia シンボルがゼロであること
nm -u <binary> | grep -i 'sk[A-Z]\|SkCanvas\|SkPaint\|GrContext' && echo "FAIL" || echo "PASS"

# libtommath シンボルがゼロであること
nm -u <binary> | grep -i 'mp_init\|mp_clear\|ltm_' && echo "FAIL" || echo "PASS"
```

### ヘッダ参照検証
```bash
# ICU ヘッダが include されていないこと
grep -r '#include.*<unicode/' libs/ladybird/Libraries/ && echo "FAIL" || echo "PASS"

# curl ヘッダが include されていないこと
grep -r '#include.*<curl/' libs/ladybird/ && echo "FAIL" || echo "PASS"

# OpenSSL ヘッダが include されていないこと
grep -r '#include.*<openssl/' libs/ladybird/Libraries/ && echo "FAIL" || echo "PASS"
```

---

## 7. 決定記録 (ADR)

### ADR-001: LibJS を維持する
- **日付**: 2026-03-31
- **決定**: Ladybird の LibJS を JS エンジンとして維持
- **理由**: quickjs / V8 への置換はスコープ外。LibJS は Ladybird と強く統合されており、置換コストが高い
- **代替案**: quickjs (libs/quickjs/), V8 (libs/v8/)

### ADR-002: HarfBuzz の初期維持
- **日付**: 2026-03-31
- **決定**: Phase 5 では HarfBuzz テキストシェーピングを維持し、後続で独自シェーパーに移行
- **理由**: テキストシェーピングは複雑な Unicode 処理を含み、独自実装は Phase 5 のスコープを超える
- **移行計画**: aquamarine に glyph shaping API を追加後、HarfBuzz を除去

### ADR-003: AVIF / JPEG XL / TIFF 除外
- **日付**: 2026-03-31
- **決定**: Phase 5D ではこれらの画像形式を除外（stub 化）
- **理由**: RinOS に自前デコーダがなく、優先度が低い
- **将来**: 必要に応じて段階的に追加

### ADR-004: Post-quantum 暗号の stub 化
- **日付**: 2026-03-31
- **決定**: ML-KEM / ML-DSA は `ENOSYS` を返す stub とする
- **理由**: rintls に実装がなく、Web Crypto API での要求頻度が極めて低い

### ADR-005: workerd を HTTP リクエストバックエンドとして使用
- **日付**: 2026-03-31
- **決定**: Ladybird の RequestServer (curl) を RinOS の workerd daemon で置換
- **理由**: workerd は既に `src/apps/workerd/` に実装済みで、Unix socket IPC + SHM によるHTTP fetch / 画像デコードを提供
- **プロトコル**: `src/shared/workerd_service_abi.h` (magic: WRV2, version: 3)
