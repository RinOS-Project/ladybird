# Ladybird → RinOS 完全移植ガイド

本ドキュメントは `libs/ladybird/` のLadybirdソースをRinOS固有の依存に完全に書き換える
移植プロジェクトの方針・設計判断・完了条件を網羅的に記録するものです。

---

## 1. 移植の目的

RinOS独自Webエンジン基盤として、upstreamのLadybirdコードを取り込みつつ、
ホストのパッケージ管理や外部プラットフォーム依存（Skia, OpenSSL, curl, ICU,
libtommath, HarfBuzz/FreeType, Vulkan, Metal 等）を除去し、RinOS純正ライブラリと
リポジトリ内で固定された依存だけで構成する。

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
| Media (Audio/Video) | ON (FFmpeg CPU decode + RinOS audio) |
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
| curl (HTTP client) | `Services/RequestServer/` + `resolved` + `rintls` | Services/RequestServer | 4 |
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
| FFmpeg | `libs/FFmpeg/`をi386/x86_64向けに静的ビルド | LibMedia | Media |

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

### Phase 4: ネットワーク置換 (RequestServer + resolved + rintls)
- **目標**: curl / OpenSSL::SSL ネットワーク依存を除去し、Ladybird の RequestServer を RinOS transport に載せ替える
- **実装**:
  - `Services/RequestServer/` — curl依存コード削除、`RinHTTPTransport` による直接 HTTP/1.1 + TLS 実装
  - `resolved` — DNS lookup の正式依存
  - `rintls` — HTTPS / WebSocket の正式依存
  - `LibWebSocket/` — rintls + direct socket transport 経由で直接実装
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
- `aq_text.h/.c` — 組み込み PSF bitmap font の lookup と raster support。TrueType/OpenType parser ではない

#### 5B: LibGfx ブリッジ
- `PainterAquamarine.cpp/.h` — `Gfx::Painter` インターフェース実装
- `PathAquamarine.cpp/.h` — `Gfx::PathImpl` 実装
- `PaintingSurface.cpp` — `SkSurface` → `AqSurface` ラッパー
- `ImmutableBitmap.cpp` — `SkImage` → `AqSurface` (read-only) ラッパー
- `Font/TypefaceRinOS.cpp/.h` — RinOS UI 用 PSF fallback
- `Font/TypefaceTrueTypeRinOS.cpp/.h` — SFNT TrueType `glyf` outlines の bounded native reader
- Skia ファイル全削除

`PathAquamarine.cpp` は PSF glyph の連続した set-bit run を矩形 contour に変換する。`aq_font_load_psf()` は入力 bytes を借用するため、Path と display-list player は成功した load の `Core::Resource` を static lifetime で保持する。resource を局所変数のまま破棄してから `AqFont` を再利用してはならない。

`TypefaceTrueTypeRinOS` は外部 `FontFace` 向けに最大64 MiBのfont bytesを自身で保持し、SFNT/TTC の `cmap` format 4/12、`hmtx`、単純・composite `glyf` contour を有界に読み取る。glyph advance とdesign metricsを文字組みに、quadratic outlineを `PathAquamarine` と `DisplayListPlayerAquamarine` のfill pathへ渡すため、対応fontをPSFの別glyphへ置換しない。CFF/`OTTO`、WOFF/WOFF2、variable/color/bitmap font、complex-script shaping、subpixel AA は未実装であり、該当fontまたは破損したoutlineはload/drawを成功扱いにしない。

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
  - host前段は `LAGOM_TOOLS_ONLY=ON` の Lagom tools/code generators のみに限定
  - `deps.lock`固定のFFmpegを静的・PIC・LGPL・decode-onlyで先行クロスビルド
  - `RINOS_FFMPEG_ROOT` imported targetとRinOS PlaybackStreamをLibMediaへ接続
  - `scripts/build_iso.sh` にladybirdビルド統合
  - `RINOS_HELPER_SERVICES_ONLY=ON` はdesktop frontendとそのresource bundleを
    除外する一方、LibWeb/LibCryptoのnative provider CTestを構成できる。desktop
    harnessである`test-web`はこのprofileに登録しない。
  - 同 profile のnative CTestは target syscallを発行せず、`LibCore`の
    POSIX named-shared-memory providerでRinOS AnonymousBufferのname handoffを
    実行する。対象 ISO は従来どおり`rin_runtime.c`のsyscall ABIを使う。
    native CTest の`LibCompress`だけはhost zlibを明示linkし、製品RinOS zlib
    経路を置換しない。
  - Aquamarine rendererを選ぶRinOS profileは`VulkanContext.cpp`をsource graph
    に入れない。Vulkanの有無とlink条件を食い違わせてnative CTestや製品linkへ
    未解決`vk*` symbolを持ち込まない。
- **完了条件**:
  - i386/x86_64 両方で cmake 成功
  - ISO生成に ladybird 関連バイナリ含有
  - vcpkg外部パッケージへの参照ゼロ
  - H.264/AAC MP4およびVP9/Opus WebMをカスタムI/O経由でデコード可能
  - i386 AC97 / x86_64 Intel HDAでpause/resume/drain/flushと再生位置取得が可能

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
// Ladybird の IPC endpoint / generated headers は host Lagom tools で生成する
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
│  │ RequestServer            │  ← RinHTTPTransport       │
│  │ + resolved + rintls      │  ← Ladybird IPC           │
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

### ADR-004: rintls の post-quantum provider を使用する
- **日付**: 2026-08-24
- **決定**: ML-KEM / ML-DSA は `ENOSYS` stub にせず、rintls provider を経由して実 key generation、encapsulation/decapsulation、signature/verification を実行する
- **理由**: `LibCrypto` は P-256/P-384/P-521、ML-DSA-44/65/87、ML-KEM-512/768/1024、Ed25519/Ed448、X25519/X448 を rintls API に接続済みであり、Unavailable 表示や synthetic result は安全な代替にならない。RinOS WebCryptoのNIST鍵対生成もrintlsがprivate/publicを同時生成し、両方の公開点検証後だけLadybirdへ公開する
- **残件**: WebCrypto 全 API の entropy failure injection、WebContent isolation、consumer ISO/QEMU JavaScript evidence が未完了であり、P1.4 は完了扱いにしない

### ADR-005: RequestServer を RinOS transport に載せる
- **日付**: 2026-03-31
- **決定**: Ladybird の RequestServer を維持し、内部 transport を RinOS の direct socket + `resolved` + `rintls` に置換
- **理由**: Browser → WebContent → RequestServer の公式プロセス境界を保ったまま、curl/OpenSSL/workerd を退役できる
- **補足**: host前段では helper service を組まず、`LAGOM_TOOLS_ONLY=ON` の Lagom tools/code generators のみをビルドする

### ADR-007: worker helper の起動失敗は renderer を終了させない
- **日付**: 2026-08-26
- **決定**: `RequestWorkerAgent` の Browser/WebContent transport が切断された場合、`PageClient` は空の worker transport を返す
- **理由**: `WorkerAgentParent` が空の transport を非同期 `error` event へ変換するため、単一の Dedicated/Shared Worker helper 起動失敗で WebContent 全体を `exit(0)` させない
- **残件**: product image/QEMU での worker 実行、crash recovery、および File Portal mediation は未完了であり、P1.4 の worker 項目を完了扱いにしない

### ADR-008: WASI の closed descriptor は即時に失効させる
- **日付**: 2026-08-26
- **決定**: WASI `fd_close` が host close に成功した時点で、WASI→host descriptor map から同じ entry を削除する
- **理由**: host が同じ数値の descriptor を再利用しても、closed WASI descriptor が別 object の権限を得ないようにする
- **残件**: WASI rights/inheriting の全実装、product image/QEMU での Wasm execution は未完了であり、P1.4 を完了扱いにしない

### ADR-006: 固定済みFFmpegをdecode-onlyで組み込む
- **日付**: 2026-08-08
- **決定**: `deps.lock`の`libs/FFmpeg`を変更せず、i386/x86_64ごとに静的・PICで先行ビルドしてLibMediaへリンクする
- **構成**: avutil/avcodec/avformat/swresampleのみ。ネットワーク、プログラム、デバイス、フィルター、エンコード、外部コーデック、GPL/nonfreeは無効
- **ライセンス**: 有効化する構成はLGPL-2.1-or-later。固定リビジョンとライセンスファイルは`deps.lock`で追跡する
- **I/O**: HTTPはRequestServer/Ladybirdが取得し、FFmpegにはカスタムI/Oで渡す
