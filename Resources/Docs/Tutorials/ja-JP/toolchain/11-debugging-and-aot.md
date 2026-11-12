# 11: デバッグと AOT の生成

このチュートリアルでは、デバッガーによる検査と、配布先に合わせた AOT ターゲットの選択について説明します。

## ステップ 1: 監視可能なスクリプトを準備する

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

これを `debug_demo.vs` として保存し、通常どおり確認します。

```powershell
veroscript --check debug_demo.vs
```

## ステップ 2: DAP デバッグ セッションを開始する

標準入出力モードは、エディターまたは別のデバッグ クライアントを起動して管理するのに適しています。

```powershell
veroscript --debug debug_demo.vs
```

TCP 経由で接続するクライアントの場合:

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

クライアントでブレークポイント、ステッピング、および変数検査を構成します。デバッグでは VM デバッグ パスを使用するため、通常の実行のパフォーマンス測定としてデバッグ時の動作を使用しないでください。

source breakpoint は active な code に設定します。DAP client では
`setBreakpoints` で送信します。launch 前に設定した breakpoint は pending となり、program の
load 後に client は同じ breakpoint ID の `reason: "changed"` を持つ `breakpoint` event を受け取ります。
必ず返された verified state と line を使ってください。`#if` で除かれた行、source の範囲外の行、
executable location のない行は unverified のままです。

## ステップ 3: AOT ソースを生成する

スクリプトに動的読み込みが必要ない場合は、配布先に合う AOT ターゲットを選択します。既定は C++17 です。

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp` はリンクされた実行可能ファイルではありません。ホスト プロジェクトには、互換性のある C++17 コンパイラと VeroScript ランタイム サポートが必要です。生成されたファイルを検査し、直接実行するのではなく、ターゲット プロジェクトにビルドします。

スタンドアロン Python プログラムには Python を明示的に選択します。

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

C# または NativeAOT の配布には source bundle を生成します。

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

対応範囲と制限は [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/aot.md) と [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/aot.md) を参照してください。意味を保てない構文では診断を返して生成を停止します。

## モードの選択

- 高速イテレーション、ホット リロード、対話型デバッグ: VM またはデフォルトの混合バックエンド。
- ブレークポイントと変数の検査: `--debug`。
- ネイティブ C++ ビルド: C++ AOT を使用し、ホスト バインディングと同時実行を検証します。
- スタンドアロン Python: `--target=python` を使用し、対象 Python でテストします。
- .NET または NativeAOT: `--target=csharp` を使用し、生成 bundle をビルドします。

## 演習

1. `calculate` 内にブレークポイントを設定し、`input` と `result` を検査します。
2. 入力を変更し、通常の出力とデバッグ出力を比較します。
3. 配列を使用するスクリプトの C++ AOT ソースを生成し、ターゲット プロジェクトでコンパイルできることを確認します。
4. 同じスクリプトを Python または C# へ生成し、各ターゲットの制限に関する診断を比較します。

[デバッグ](../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md)、[C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/aot.md)、[C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/aot.md) を参照してください。次へ: [ホスト バインディングとスクリプトの統合](../advanced/12-host-bindings.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/11-debugging-and-aot.md) · [fr-FR](../../fr-FR/toolchain/11-debugging-and-aot.md) · [ja-JP](11-debugging-and-aot.md) · [ko-KR](../../ko-KR/toolchain/11-debugging-and-aot.md) · [zh-CN](../../zh-CN/toolchain/11-debugging-and-aot.md) · [zh-TW](../../zh-TW/toolchain/11-debugging-and-aot.md)
