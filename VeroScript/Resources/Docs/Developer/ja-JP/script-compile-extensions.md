# スクリプトによるコンパイル拡張

拡張は std::compiler で注釈説明と処理器を登録する通常の VeroScript モジュールです。Core は業務ソースの処理前にロードし、実行時は生成済みバイトコードを使用します。

## ロード

エントリ先頭で、コメント、空白、BOM、先行する compiler import の後に記述します。

```vs
import compiler "builder.vs";
```

パスはリテラルです。通常の import は補助コード、import compiler は登録実行と登録依存を表します。循環を拒否します。後続モジュールはエントリかホストが既にロードした拡張のみ使えます。自分の未登録構文に依存できません。

ホストは `FCompileOptions::ScriptCompileExtensions.Roots` に Path と任意の RegistrationEntry を設定します。Loader、C++ Session、Codegen は同じ構成を使います。bAllowSourceImports=false でも構成済みルートは使えます。オーバーレイ、import、stdlib の規則は既存サービスを使います。

## 登録とサービス

拡張は compiler.vs を import し、ルート自身の登録関数を export します。

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

引数なしファクトリは fn(std::compiler::context,std::compiler::output)->void を返します。各タスクで新しい VM 状態を作り、登録とファクトリ生成は一度だけ行います。コールバックの状態は段階間で保持します。ネイティブとスクリプトは同じ順序・競合検査を使います。説明のみの登録には実行動作はありません。

Context は段階、イベント、モジュール、宣言、引数、既定値、修飾子、ジェネリック、注釈、コメント、位置を提供します。Output は add_declaration、edit_source、replace_declaration、complete_body、replace_body、set_metadata、report_diagnostic、submit_operation を提供します。完全な操作には子メタデータと由来を含め、Core の権限、Snapshot、競合、型検査を必ず通します。

registration、context のサービス、output は呼出し中のみ有効です。所有する読み取り専用値は保持できますが、古い scope は次のバッチを変更できません。UTF-8 範囲は文字を分割できません。ジェネリックは宣言時に一度処理して通常の代入を行い、ユーザー本体、override、既定本体は共通パイプラインを使います。

初期化、登録、ファクトリ、コールバックは総予算とキャンセルを共有します。コンパイル VM はコンソール、直接ファイル・環境操作、タスク、ディレクトリ変更、アドレス、時刻取得を禁止します。追加ネイティブ binding はホストが明示します。未宣言の非決定性は準備結果キャッシュを無効にします。信頼できないネイティブコードの完全なサンドボックスではありません。

configuration と read_dependency は所有する情報を返します。リソースは現在の拡張ルート基準です。ソース、パッケージ、構成、サービス契約、存在・不在の依存をキャッシュ検査へ含めます。

## パッケージと接続

CompileScriptCompileExtensionSource は登録せずコンパイルします。BuildPackage の Library に CompileExtensionManifest をコピーします。ロードは初期化前にエントリ、版、サービス指紋、binding、依存を検査します。業務パッケージは生成操作を再生し、生成元拡張を実行しません。

C++ は ScriptCompileExtensions、C は vs_interop_session_set_script_compile_options、C# は SetScriptCompileExtensions、Python は set_script_compile_extensions を使います。セッション設定は作成スレッドでアイドル時に変更します。CLI は --compiler-extension、--compiler-config、--no-compiler-imports、--compiler-instructions、--compiler-timeout と compiler-package source.vs output.vslib を提供します。

ツールとソース AOT は同じ準備プログラムを使い、独立出力は処理器 VM を保持しません。JIT は業務コード、コンパイル拡張は常に VM で実行します。キャッシュはホストのソース/import キーに加え構成と全依存を再検査します。

例：[builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs)、[main.vs](../../../Examples/ScriptCompileExtensions/main.vs)。

<!-- docs-navigation -->

---

[開発者ドキュメント](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

Language: [en-US](../en-US/script-compile-extensions.md) · [fr-FR](../fr-FR/script-compile-extensions.md) · [ja-JP](script-compile-extensions.md) · [ko-KR](../ko-KR/script-compile-extensions.md) · [zh-CN](../zh-CN/script-compile-extensions.md) · [zh-TW](../zh-TW/script-compile-extensions.md)
