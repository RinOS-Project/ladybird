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
- Painter.cpp: PainterAquamarineスタブ（TODO: 完全実装）
- Path.cpp: PathImplAquamarineスタブ（TODO: 完全実装）
- TextLayout.cpp: SkTextBlobなしのグリフバウンド計算
- VectorGraphic.cpp: ブランクビットマップ返却（TODO: PainterAquamarine統合）
- YUVData.cpp: Skia YUV型を除外、コアバッファのみ保持
- CMakeLists.txt: aquamarineリンク追加、AK_OS_RINOS定義追加

## 未完了フェーズ

### Phase 5: LibWeb/LibWebView 統合
- ネットワーク層 workerd統合
- WebEngine RinOS対応

### Phase 6: ビルドシステム統合
- RinOSクロスコンパイル設定
- ISO統合
