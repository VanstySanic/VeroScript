# スクリプトによるコンパイル拡張

<a id="source-extension-activation"></a>

ホストやアプリが提供する拡張で、通常の検査前に注釈を処理したり宣言を生成できます。エントリーファイル先頭で、任意のコメントや先行するコンパイラー import の後に import compiler を置いて有効化します。通常の import は拡張登録を実行しません。

## 説明だけを登録する拡張

最初のファイルを notes.vs、次のファイルを main.vs として保存します。この拡張は @Note の説明だけを登録し、業務上の動作は生成しません。

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

出力は A descriptive annotation. です。Runtime の標準ライブラリに compiler.vs が必要です。core.vs はこのコンパイル専用エントリーを再エクスポートしません。

## パッケージと制約

compiler-package は登録処理を実行せず .vslib 拡張を生成します。同じ業務プログラムから import compiler を除いた consumer.vs を用意し、notes.vslib を明示的に選択します。ソース版とパッケージ版を同時に有効化しないでください。

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

[コマンドラインツール — スクリプトによるコンパイル拡張](../../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md#cli-compiler-options) を参照してください。

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[注釈](../language-basics/annotations.md)

拡張全体の既定予算は VM 命令 1,000,000 回と 2,000 ms です。0 は該当する予算を無効にします。ブロック中のネイティブコールバックを強制中断する制限ではありません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md) · [en-US](../../en-US/standard-library/compiler.md) · [fr-FR](../../fr-FR/standard-library/compiler.md) · [ja-JP](compiler.md) · [ko-KR](../../ko-KR/standard-library/compiler.md) · [zh-CN](../../zh-CN/standard-library/compiler.md) · [zh-TW](../../zh-TW/standard-library/compiler.md)
