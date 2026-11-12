# 10: パッケージの構築とアプリケーションの配布

スクリプトが成果物プログラムになったら、アプリケーションまたはライブラリとしてパッケージ化します。 `.vsapp` は実行することを目的としており、`.vslib` は別のスクリプトによってインポートされることを目的としています。

## ステップ 1: ライブラリ モジュールを作成する

`package_library.vs` を作成します。

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

エクスポートされた宣言のみがライブラリ API の一部になります。ライブラリを構築して検査します。

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## ステップ 2: アプリケーションのエントリ ポイントを作成する

`package_app.vs` を作成します。

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

アプリケーションをビルドして実行します。

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

アプリケーション パッケージにはそのエントリ ポイントと依存関係が含まれているため、実行時にソース ライブラリ パッケージをそばに置く必要はありません。 `package_app.vs` は、デフォルトでソース モジュールをインポートします。 `-D VERO_USE_BINARY_PACKAGE` は、生成された `DemoLibrary.vslib` に切り替えます。したがって、アプリケーションのコンパイル中にこれを使用できる必要があります。

## ステップ 3: 公共の境界を確認する

エクスポートされていない関数は、独自のライブラリ ソース内で使用できますが、ダウンストリーム スクリプトから呼び出すことはできません。パッケージを公開する前に、次のことを確認してください。

1. 目的の型と機能のみがエクスポートされます。
2. パラメータの型、戻り値の型、列挙値は安定しています。
3. パッケージのバージョンは、ターゲット ランタイムおよびバインディングと互換性があります。
4. 生成されたファイルに対して `package inspect` が実行されました。

配布サンプルには、[`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md)、[`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs)、および [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs) に完全な例があります。

## 演習

1. エクスポートされた `subtract` 関数を追加します。
2. `export` を削除し、アプリケーションのコンパイル エラーを確認します。
3. パッケージのバージョンを設定し、リリースに使用した正確なビルド コマンドを記録します。

[パッケージと配布](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/packages.md) を参照してください。次: [デバッグと AOT 生成](11-debugging-and-aot.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/10-packages-and-distribution.md) · [fr-FR](../../fr-FR/toolchain/10-packages-and-distribution.md) · [ja-JP](10-packages-and-distribution.md) · [ko-KR](../../ko-KR/toolchain/10-packages-and-distribution.md) · [zh-CN](../../zh-CN/toolchain/10-packages-and-distribution.md) · [zh-TW](../../zh-TW/toolchain/10-packages-and-distribution.md)
