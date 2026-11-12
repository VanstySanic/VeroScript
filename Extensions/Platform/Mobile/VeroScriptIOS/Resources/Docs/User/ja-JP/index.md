# iOS — 実験的なモバイル埋め込み

実験的な静的 XCFramework は iOS 15 以上、実機 arm64 とシミュレーター arm64/x86_64 が対象です。`VeroScriptIOS` を import し Foundation/libc++ をリンクします。動的 framework として埋め込みません。`assets/stdlib` の階層を Bundle に保ち URL を確認します。`VSMConfiguration` は設定と `sourceResources` をコピーし、更新時は engine を再作成します。import と一致する絶対ラベルとアプリ所有の書き込み可能キャッシュを使い、読み取り専用 Bundle は使いません。ワーカーキューで実行し ARC が execute/cancel 中の寿命を保持します。呼び出しごとに新規状態で、取消後に完了を待って解放します。`outputUTF8`/`diagnosticsUTF8` は NUL を保持する `NSData` で、切り詰めは文字の途中になり得ます。この SDK は実験段階です。配備前に、使用するシミュレーターと実機でアプリケーションの統合を検証してください。

[実験的なモバイル埋め込み](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md)

## モバイル言語の明示設定

[モバイル言語の明示設定](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md)

## 管理バイト領域の上限

`VSMConfiguration.maxBufferBytes` は管理対象の単一バイトバッファを、`VSMConfiguration.maxRetainedBytes` は保持する基底ストレージを制限します。既定値はそれぞれ 16 MiB と 32 MiB で、0 は対応する制限を無効にします。`VSMEngine` は構成をコピーするため、作成前に設定してください。

## 実行制御付きネイティブ実行と静的 AOT

[実験的なモバイル埋め込み — 実行制御付きネイティブ実行と静的 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ja-JP/index.md#mobile-jit-selection) を参照してください。

[実験的なモバイル埋め込み — 実行制御付きネイティブ実行と静的 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ja-JP/index.md#mobile-core-aot) を参照してください。

<a id="ios-native-selection"></a>

iOS では `VSMAotProgram.programWithFactory:error:` でプログラムを作成し、`VSMConfiguration.aotPrograms`／`aotProgramId` を設定して `executePackage` で生成パッケージを実行します。カタログはプログラムを保持し、エントリーコードは静的リンクを維持します。ID に関数アドレスを入れてはいけません。`VSMAotProgramFactory` の出力ポインター自体は非 null で、格納するプログラム／エラーのポインター値は null を許します。ファクトリーは `vs_interop_status` を返し、Objective-C メソッドは null 許可のプログラムを返し、オプションのエラー引数で NSError を受け取れます。使用前に返されたプログラムを確認し、nil の場合はエラー出力を指定していれば NSError を確認してください。

## Bundle 資源と実行

<a id="ios-application-resources"></a>

静的な `VeroScriptIOS.xcframework` を Foundation／libc++ とリンクします。モジュールは C ABI 用の `VeroScriptInterop.h` も公開します。標準ライブラリはディレクトリ構造を保ってコピーしてください。例の `stdlibURL` は配備したディレクトリの確認済み URL、`cacheURL` は既存の書き込み可能なアプリ所有ディレクトリです。ホストのワーカーキューから呼び出します。エンジンは `sourceResources` を含む設定をコピーし、仮想絶対ラベルはインポートと一致させます。資源更新後はエンジンを作り直します。

```objc
@import VeroScriptIOS;

// Call on a host worker queue after validating both URLs.
static VSMExecutionResult* RunExample(NSURL* stdlibURL, NSURL* cacheURL, NSError** error)
{
	VSMConfiguration* config = [VSMConfiguration new];
	config.stdlibDirectory = stdlibURL.path;
	config.cacheDirectory = cacheURL.path;
	VSMEngine* engine = [[VSMEngine alloc] initWithConfiguration:config error:error];
	if (!engine)
	{
		return nil;
	}
	NSData* source = [@"void main(){print(42);}" dataUsingEncoding:NSUTF8StringEncoding];
	return [engine executeSource:source fileLabel:@"/veroscript/scripts/main.vs" error:error];
}
```

このソースが成功すると `success == YES`、終了コード 0、`outputUTF8` のバイト列は `0x34 0x32 0x0A` です。nil は API 失敗なので、要求した NSError を確認します。非 nil で `success == NO` の場合はスクリプトの診断／エラーを確認します。NUL や切り詰められた UTF-8 の末尾を保持する必要がある場合は NSData のまま扱います。これは期待される動作の例であり、アプリケーション検証の代わりにはなりません。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
