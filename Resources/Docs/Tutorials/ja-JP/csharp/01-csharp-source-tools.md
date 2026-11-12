# 01: C# バインディング成果物を生成する

同じマネージド宣言からカタログ、API リファレンス、実行時登録ソース、AOT bridge を生成します。意味解析 frontend はビルド時にソースを解析し、スクリプト Session は作成しません。

対応する `veroscript-csharp-bindgen` と `veroscript` を PATH に置き、書込可能な学習ディレクトリと、選択 SDK に対応する .NET host project を用意します。すべてのコマンドを同じ場所で実行します。生成、host のコンパイル、callback 登録は別の操作です。

以下の AOT 実行は生成された `net8.0` project を使います。この target をビルドできる .NET SDK と、実行用の対応 .NET runtime を用意します。

## 手順 1: 入力を用意する

host 宣言を `HostApi.cs` として保存します。

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

呼び出す側を `app.vs` として保存します。

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## 手順 2: 実行時登録を生成する

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

`bindings.json`、`Bindings.g.cs`、`Bindings.vs.txt` が生成されます。登録ソースを managed C# SDK と元の `HostApi.cs` と共にコンパイルし、スクリプトのロード前に生成された登録入口を呼びます。カタログだけでは callback は登録されません。設定済み host でこのスクリプトを実行すると `42` を表示してゼロを返します。

設定済み VM host で、`app.vs` のロード前に生成された登録入口を呼びます。`session` は host が持つ `VeroScriptSession` です。

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## 手順 3: AOT bridge を生成する

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

`HostApi.cs`、`Bindings.Aot.g.cs` と生成アプリケーションの全単位を同じ .NET project でコンパイルします。bridge は `VeroScriptInterop` なしで managed code を直接呼びます。生成 project の runtime 依存は維持し、ソース生成だけを実行ファイルとみなさないでください。

生成された `generated/app/App.csproj` は `EnableDefaultCompileItems=false` を指定し、生成単位を明示的に列挙します。既存項目を保ったまま、以下の `ItemGroup` を `Project` 要素内に追加します。パスは `App.csproj` 基準です。

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

学習ディレクトリから managed application を実行します。期待出力は `42`、終了コードはゼロです。

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

この AOT project には `Bindings.Aot.g.cs` を使います。`Bindings.g.cs` は手順 2 の VM host 用です。bundle 再生成は `App.csproj` を置き換えるため、再生成後にこれらの入力を再追加します。

## 手順 4: プロジェクトとして生成する

以下を `Models/State.cs` に保存し、手順 1 の `HostApi.cs` も使います。

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

両ファイルを一緒に渡し、同じ生成のカタログ、登録、bridge、reference、manifest を保存します。partial/関連宣言は出力前にマージします。同名 stem のファイルには安定した path-hash suffix が付きます。

この project 生成後に AOT application を再ビルドする場合、まず新しい merged catalog で手順 3 の AOT コマンドを繰り返します。ビルド前に project 入力を再追加し、`<Compile Include="../../Models/State.cs" />` も含めます。catalog、bridge、host 宣言、生成 application は同じ入力集合に揃えます。

## 手順 5: 契約を確認する

カタログの名前・signature を host source と比較し API reference を確認します。既定では公開宣言が対象です。定義した `[VeroScriptBinding]` と `--require-attribute` で対象型を選び、`[VeroScriptIgnore]` で除外します。未対応 nullable/pointer/adapter signature は診断になります。不一致を metadata 編集で隠さず、明示的な wrapper を用意します。

## 練習

1. 対応する読取専用 property を追加し契約を確認します。
2. binding attribute を定義して一つの型に付け、`--require-attribute` の結果を比較します。
3. 実行時登録と AOT bridge の利用 host を説明します。
4. 未対応 nullable signature の診断を確認してから元に戻します。

[BindGen リファレンス](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/bindgen.md) · [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/aot.md) · [C# ソース変換](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/transpiler.md) · [実行時の値と所有権](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/csharp/01-csharp-source-tools.md) · [fr-FR](../../fr-FR/csharp/01-csharp-source-tools.md) · [ja-JP](01-csharp-source-tools.md) · [ko-KR](../../ko-KR/csharp/01-csharp-source-tools.md) · [zh-CN](../../zh-CN/csharp/01-csharp-source-tools.md) · [zh-TW](../../zh-TW/csharp/01-csharp-source-tools.md)
