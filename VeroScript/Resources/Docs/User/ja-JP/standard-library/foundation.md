# 標準ライブラリ基盤

<a id="foundation-imports"></a>

core.vs または個別モジュールをインポートします。std::io::read_* には io.vs が必要です。環境、時計、コンソールはランタイムが直接提供します。完全修飾名はインポートを代替しません。

parse/try_parse は固定幅整数、float、double、bool と別名に対応します。外側の ASCII 空白を除き全体を照合します。符号は一つ、符号なし型では -0 も拒否します。整数専用 base は 2..36 で接頭辞を除去しません。浮動小数点は表現可能な有限十進数、bool は true/false のみです。通常の形式・範囲エラーでは try_parse は false を返し出力を保持します。不正引数、上限、キャンセルは実行エラーです。

文字列位置は UTF-8 バイト単位です。replace は重複しない一致を置換し、空パターンでは原文を返します。split は空要素を保持し、trim と大文字小文字変換は ASCII 規則です。UTF-8 は厳密検証し、NUL、BOM、改行を保持します。region は buffer/view/readonly_view の実際のオーバーロードを指します。

<a id="io-contract"></a>

正常な EOF だけが空 optional です。空行は値のある空文字列で、最後の改行なし行も返します。MaxTextBytes は LF と末尾一つの CR を除いた行内容を数え、行内の CR は含めます。LF と末尾一つの CR を削除し、token は末尾区切りを残します。旧 read_line の EOF は空文字列、厳密 token の EOF はエラーです。read_bytes は上限超過を切り詰めずエラーにし、明示 0 は空ファイルだけ許可します。メモリ借用を検証します。書き込み失敗前の変更は戻さず、flush は永続化を保証しません。

パスはディスクへアクセスせず native/posix/windows の字句規則を使います。Windows の C:a と /a は絶対パスでなく、完全 UNC は絶対パスで出力は / 区切りです。NUL、不完全 UNC、デバイス接頭辞を拒否します。正規化はサンドボックス包含の証明になりません。sum_as は各要素を R へ変換して検査付き加算し、空配列を拒否します。accumulate_as は空なら初期値を返します。R は明示した bool 以外の基本数値型です。clamp は暗黙の混合型、逆順・NaN 境界を拒否し、入力 NaN を保持します。

同期ファクトリは非公開のチャネル役割を作ります。一般 send/recv/容量変更は拒否し、閉鎖と照会は可能です。mutex は非再帰で論理実行器が所有し、他者の解除はエラー、自身の try/時間付き取得は false、所有者終了時は閉鎖します。semaphore は数値カウンタで過剰解放を拒否します。synchronized は準備したスナップショットを発行し、算術失敗でも値を保持します。ハードウェアのロックフリー性は保証しません。

タスクの別名は Core 状態を共有します。await は worker 終了後に唯一の結果を消費し、失敗・結果不足や複数結果ではエラーになります。await_for の空 optional はタイムアウトだけで、タスクは有効なままです。消費前の準備失敗は結果を保持します。グループは null、不正、重複を先に検証し、部分失敗でも既消費分は戻さず size は残件数です。when_all は配列を保持してタスクを消費します。cancel_all は先に全件へ要求し、実際の終了まで worker を保持します。ブロック中のホスト呼出しを強制中断しません。

ホストの MaxTextBytes/MaxReadBytes/MaxSplitParts は操作ごとの uint64 で、0 は追加制限なしです。worker が設定を継承し、ネイティブ処理は現在の予算を分割して確認します。reverse/rotate/shuffle は中断後も要素の多重集合を保持し、監視経路では clone を呼ばず一括発行します。shuffle の対象と乱数状態の同一性は拒否し、消費済み乱数は戻しません。独立 AOT は Core 必須操作を出力前に明示拒否し、未使用インポートは許可します。

[文字列モジュール](string.md#integer-formatting) を参照してください。

setenv(name, "") は存在する空値を設定します。削除には unsetenv を使い、未存在の削除も成功します。try_getenv は未存在と空値を区別します。名前は空・=・NUL を拒否し、値は NUL を拒否します。名前の大小文字規則は Windows/POSIX に従います。

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

[同時実行モジュール — タスクのキャンセル要求](concurrent.md#concurrency-aot-boundary) を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/foundation.md) · [fr-FR](../../fr-FR/standard-library/foundation.md) · [ja-JP](foundation.md) · [ko-KR](../../ko-KR/standard-library/foundation.md) · [zh-CN](../../zh-CN/standard-library/foundation.md) · [zh-TW](../../zh-TW/standard-library/foundation.md)
