# RinOS Ladybird ポーティング進捗

## 完了フェーズ

### Phase 0: プラットフォーム定義 ✅
- `AK_OS_RINOS` マクロ定義
- ポーティングドキュメント作成

### Phase 1: AK + LibCore + LibIPC ✅ (27af56f120)
- AK基盤型のRinOS対応
- LibCore SystemCall/Socket/Process移植
- LibIPC Unix socket → RinOS IPC変換

### Phase 2: LibCrypto/LibTLS → rintls ✅ (f96e0aa736)
- OpenSSLバックエンドをrintlsに置換
- 52ファイル変更、+2982/-48行

### Phase 3: LibUnicode ICU → rinicu/libunicode ✅ (fb710dd79c)
- 22ファイル変更、+2337/-28行
- RinICUBridge.h/.cpp: rinicu IPCクライアントブリッジ
- ICU.h/ICU.cpp: LocaleData/TimeZoneData軽量化
- Locale.cpp: 5関数をrinicu経由に変換
- Collator.cpp: RinCollatorImpl (rinicu照合API)
- Segmenter.cpp: RinSegmenterImpl (GRAPHEME/WORD/SENTENCE/LINE)
- Normalize.cpp: rin_icu_normalize (NFC/NFD/NFKC/NFKD)
- String.cpp: libunicode大文字小文字変換
- Utf16String.cpp: UTF-8経由でString.cppに委譲
- NumberFormat.cpp: RinNumberFormatImpl + 複数規則
- DateTimeFormat.cpp: RinDateTimeFormatImpl
- DisplayNames.cpp: rin_icu_display_name (8種別)
- ListFormat.cpp: RinListFormatImpl
- RelativeTimeFormat.cpp: RinRelativeTimeFormatImpl
- TimeZone.cpp: rinicu タイムゾーンAPI
- CharacterTypes.cpp: libunicodeベース文字分類 + ハードコード属性テーブル
- IDNA.cpp: ASCII通過、非ASCII拒否の簡易実装
- DurationFormat.cpp: 標準デジタルフォーマット（":"区切り）
- UnicodeKeywords.cpp: 静的キーワードデータ
- Calendar.cpp: グレゴリオ暦のみ実装
- Calendars/*.cpp: #ifndef AK_OS_RINOSガード
- CMakeLists.txt: rinicu/libunicodeリンク設定

### Phase 4: LibGfx Skia → aquamarine ✅
- 16ファイル変更、Skia依存コードを#ifdef AK_OS_RINOSガードで分離
- Bitmap.cpp: 手動バイリニア/ニアレストネイバースケーリング、手動プレマルチプライ変換
- ColorSpace.cpp: sRGBのみの簡易カラースペース実装
- Filter.cpp/FilterImpl.h: スタブフィルター実装（Skia画像フィルターパイプラインなし）
- Font/Font.cpp: HarfBuzzベースフォントメトリクス（SkFont不使用）
- Font/FontDatabase.cpp: TypefaceSkiaフォールバック無効化
- Font/Typeface.cpp: TypefaceSkia参照ガード
- ImmutableBitmap.cpp: Bitmapのみの実装（SkImage/GPU不使用）
- PaintingSurface.cpp/.h: Bitmap直接ラップ（SkSurface不使用）
- Painter.cpp: PainterAquamarine は矩形・パス・ビットマップ描画を Aquamarine へ接続済み。ビットマップは最近傍／線形、global alpha、SourceOver／Copy、無効な rect の拒否を実装した。Filter、その他の合成演算子、paint-style の shader、cap／join／dash／blur、非矩形 clip は未接続で、入力を成功扱いせず fail-close する。
- PainterAquamarine の `fill_rect`／`clear_rect` は、identity では安全な整数矩形 fast path、非 identity では有限性・clip・1,048,576 pixel budget付きの変換矩形パスへ接続した。未対応の filter／合成／stroke semantics と browser/QEMU evidence は引き続き未完了である。
- Path.cpp: PathImplAquamarine は有限点の bounded flattening、glyph outline、text-on-path と convex polygon intersection を実装した。concave／self-intersecting／boolean 多輪郭の完全演算は未実装で、bounding-box近似へ戻さず fail-close する。
- TextLayout.cpp: SkTextBlobなしのグリフバウンド計算
- VectorGraphic.cpp: `Painter::create()` 経由で PainterAquamarine を使い、変形後の intrinsic bounds を destination へ fit/center して描画
- YUVData.cpp: Skia YUV型を除外、コアバッファのみ保持
- CMakeLists.txt: aquamarineリンク追加、AK_OS_RINOS定義追加

## 未完了フェーズ

### Phase 5: LibWeb/LibWebView 統合
- ネットワーク層 workerd統合
- WebEngine RinOS対応

### Phase 6: ビルドシステム統合
- RinOSクロスコンパイル設定
- ISO統合
