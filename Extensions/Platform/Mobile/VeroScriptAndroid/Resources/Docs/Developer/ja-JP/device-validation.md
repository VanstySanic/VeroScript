# Android AAR の対象検証

## 前提とビルド

この独立アプリケーションはビルド済み基本 AAR を Java/JNI と C11/C++17 Prefab で利用し、二つ目の Core はビルドしません。既定の release APK は R8 を有効にし、この検証アプリだけに debug 署名鍵を使います。Python 3.11+ と [Android ビルドガイド](index.md)で指定する JDK、Gradle、SDK、NDK、CMake を用い、最初に一致する SDK 成果物を新しい出力先へビルドします。`--with-asmjit` はオプションの JIT AAR も準備します。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## 明示した対象で実行

エミュレーターを起動するかデバッグを許可した端末を接続し、`adb devices` の正確なシリアル番号を選びます。一致する基本 AAR を渡してください。例ではインストール ABI に x86_64 を明示します。

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

オプションのバックエンドと制御付きネイティブ実行カウンターには `--jit-aar <absolute-jit.aar>` を追加します。ARM64 は `--abi arm64-v8a`、Bionic `sysconf` のページサイズ検証は `--page-size 4096` または `--page-size 16384`、非縮小版は `--debug` です。API 26 と新しい版、両ページサイズを構成別の出力先で検証します。JNI 検証時の CheckJNI はテスト用エミュレーターで別途有効にしてください。

## 結果と検証範囲

ランナーは [ビルド設定](../../../../Resources/Tests/Device/build.gradle.kts) で宣言したテストアプリ とそのデータだけをアンインストール／置換し、対象端末は停止しません。APK、ログ、`commands.json`、`device-smoke.json` を保存します。最終 APK の 16 KB ZIP 整列、各ケース、`INSTRUMENTATION_CODE: -1` を検証するため、adb の終了コードだけでは合格しません。各試行の最初に `acceptancePassed` を false とし、要求 ABI／ページサイズを含む全検証後だけ true にします。

ケースはアセット import／Unicode 名、UTF-8／NUL 出力、float32 丸め、メモリ／実行／出力上限、キャッシュ、権限、失敗後復旧、明示バックエンド、取消、同時実行拒否、終了、ローカライズ、JNI AOT カタログの寿命を扱います。カタログ検証は生成 AOT 業務コードを実行しません。エミュレーターのネイティブ変換で動く ARM64 ライブラリは実 ARM64 ハードウェアの検証ではありません。対象の実体と成果物ハッシュを保存してください。Python、C#、描画、最終アプリケーションは別途検証します。

次の Python 専用回帰はネイティブコマンドを模擬し、ランナーのロジックだけを検証します。Android ランタイムの検証ではありません。検証アプリの存在、ホスト確認、ビルドのみでは対象検証は完了しません。

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/device-validation.md) · [fr-FR](../fr-FR/device-validation.md) · [ja-JP](device-validation.md) · [ko-KR](../ko-KR/device-validation.md) · [zh-CN](../zh-CN/device-validation.md) · [zh-TW](../zh-TW/device-validation.md)
