# Python — Mobile SDK

ヘッダーのサブディレクトリは実体化してください。`includeDirectory` 内のディレクトリのシンボリックリンクと Windows の junction/reparse ディレクトリは拒否されます。`includeDirectory` 自体は SDK 内の実ディレクトリへ解決できれば使用できます。`.h` ファイルへのリンクは SDK 内の対象に限り許可され、その内容も SDK の指紋に含まれます。

このページは `Extensions/Language/Python/Resources/Tools/Mobile` で Python モバイルパッケージをビルドして公開する保守担当者向けです。対象の CPython SDK を明示的に指定します。ホストのインタープリターやデスクトップ/macOS バイナリは対象リソースの代わりにはなりません。新しいディレクトリでビルドし、完全なファイル一覧を検証して対象プラットフォームの証拠を収集してから安定版を公開します。

Python：同じ major/minor のホスト CPython、CMake 3.25 以上、Android NDK/Ninja または macOS と完全な Xcode を使用します。対象は GIL 有効の CPython 3.13/3.14 release。minor ごとに別 package とし、全 slice の完全版番号を一致させます。debug/free-threaded は拒否します。SDK descriptor の相対パスはそのディレクトリを越えてはいけません。フィールドは `schemaVersion`、`platform`、`architecture`、`variant`、`minimumVersion`、`includeDirectory`、`library`、`sysconfigData`。対象ヘッダーと `build_time_vars` リテラルを AST で読み、対象 interpreter は実行しません。プラットフォーム、CPU、最低 OS、Python ABI、iOS install name を照合します。Android は arm64-v8a/x86_64（API 26）、iOS は実機 arm64 とシミュレーター arm64/x86_64（15.0）です。

Python package は拡張、facade、資源を所有し、CPython と依存はアプリが所有します。Android は展開された JNI パス、iOS は署名した動的 Python 拡張 framework と `.fwork`/`.origin` を使います。静的 C ABI XCFramework や禁止された native JIT とは別です。slice ごとに新規 build nonce/identity を持ち、`python-mobile.json` に全 hash、版、ABI、SDK 指紋、実機検証待ち状態を記録します。

公開前に全 inventory と binary を検証します。`device_smoke.py` は desktop を拒否し、再試行前に古い proof を無効化します。build ID、完全 Python/製品版、ABI、CPU、実機／simulator を一致させます。全 slice の proof と Android arm64 の 4 KB/16 KB 両環境が必要です。制限付き VM smoke は import、callback、診断、保持値、解放を検査します。署名済みアプリ、依存／APK ZIP 整列、iOS runpath／資源／署名、取消とアプリテストを別途検証します。ホストテストでは代用できません。

[Python](index.md)


## 出版文書

モバイルパッケージには機能ファイル、実行時リソース、ライセンスを含めます。README、マニュアル、サンプルは含めず、統合 User 文書は独立した Resources 配布物から取得します。ネイティブリソースのパスと実機での検証要件は維持します。


## 記述ファイルと再現

以下は Android arm64 と iOS 実機の記述例です。sysconfigData の名前は例示です。SDK の実ファイルを選び、他の ABI のファイルを改名して使用しないでください。Android x86_64 は `architecture` と全経路を変更します。iOS シミュレーターは `variant: simulator`、`architecture: arm64` または `x86_64` と単一アーキテクチャのバイナリを使用します。全スライスの完全なバージョンを一致させ、`@rpath/Python.framework/Python` を保持してください。

リポジトリルートで実行します。`--host-python` はホストインタープリター、`--cmake` はビルドドライバーを選択します。`--plan` は SDK を検証してビルドせずにコマンドを表示し、Android では `--ndk` も必要です。出力先は新規とし、中間ビルドは一時領域で行い、毎回新しい nonce を使用します。同梱の通知に加え、CPython と依存物のライセンスを保持してください。

ホストテスト `Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py` は AST、ホストと対象の設定、ユニバーサルシミュレーター、証拠を検証します。実機スモークは制御された VM/Mixed（100,000 命令、5 秒、256 フレーム、コンソール出力のみ）を使用し、デスクトップを拒否し、`-O` でも有効です。ネイティブ識別、ABI、バージョンを照合します。CPython とアプリのテスト、署名済みアプリの監査は別途必要です。

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

[Python](../../User/ja-JP/mobile.md)

## 記述ファイルからビルドする

ツールの実装だけではクロスコンパイルや実機実行の証拠になりません。対象 CPython SDK は明示的に提供します。ホスト CPython は対象と同じ major/minor、対象は GIL 有効の release 版です。Frontend/BindGen/Transpiler/AOT はホスト用です。拡張には Core、C++ 支援、Interop を静的リンクし、VM/Mixed はネイティブ JIT なしで動作します。

Android は NDK/Ninja と両 ABI の SDK が必要です。libpython、拡張の LOAD/RELRO の 16 KB 整合と `libc++_shared.so` の互換性を確認します。iOS は macOS と完全版 Xcode が必要です。ユニバーサル Python.framework は記述ファイル用に `xcrun lipo -thin` などで arm64/x86_64 へ分離し、`@rpath/Python.framework/Python` を維持します。

リポジトリルートで実行し、パッケージ作成は対象プラットフォームの**片方**を選択してください。山括弧のパスは実在の SDK や新出力先に置換します。SDK 検査は対象 Python を実行しません。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

## Android への配備

各 `slices/<target>/jniLibs/<abi>/` をアプリの `src/main/jniLibs/<abi>/` へコピーし、対象 libpython と依存も CPython 提供者の手順で配置します。アプリ全体で互換な `libc++_shared.so` を一つ使い、`pickFirst` で不一致を隠さないでください。対応 slice の `python/veroscript/` を Python リソースへ配置し、APK assets なら実ファイルへ展開します。`stdlib/`、`localization/`、`py.typed` を保持します。

ローダーは展開済みの実パスを要求します。アプリ Gradle モジュールを設定し、最初の import 前にインタープリターを設定します。

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir` はインストール済みアプリの絶対 `context.applicationInfo.nativeLibraryDir` で、必要ライブラリの存在を確認します。`application_python_directory` は `veroscript/` を含む配備先です。ローダーは `libveroscript_python.so` を `veroscript._veroscript` として読みます。ビルド機のパスではなくインストール済み APK で検証します。

## iOS への配備

外部 **Python.xcframework** と生成 **VeroScriptPython.xcframework** を **Embed & Sign** で追加します。Xcode が実機/ユニバーサルシミュレーターを選び、未署名の拡張をアプリの埋め込み工程で署名します。runpath に `@executable_path/Frameworks` を設定し、以下の配置を保ちます。

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

CPython とその標準ライブラリは別途設定し、`<app-bundle>/python` をモジュール検索へ追加します。読み取り専用 bundle へのバイトコード書き込みを無効化してください。`AppleFrameworkLoader` が `_veroscript.fwork` を読み、両シミュレーター共通のマーカーと `.origin` を使います。起源を修正せず `app_packages` へ移動しないでください。リソースへ `.so` をコピーしたり、準備済みマーカーを再処理したりしません。

## 監査とプラットフォームの証拠

監査は全ファイル、ファサード、標準ライブラリ、各言語カタログを対応ソース版と比較します。CPython 初期化子、依存、Android ABI/API/16 KB 整合、iOS platform/minimum OS/framework/Info.plist/マーカー、XCFramework 構成を検査します。余分なネイティブバイナリーはハッシュを追加しても拒否します。対応 checkout または sdist の `_native` から実行し、パッケージ内ハッシュだけで必要リソースを証明しないでください。

まず監査を実行します。`device_smoke.py` をアプリのテストへコピーし、実際のモバイル Python のワーカースレッドで、書き込み可能なレポート先を `run` に渡します。全 slice と Android arm64 の両ページ環境のレポートを出力し、必要な数の `--evidence` を指定します。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

```python
from device_smoke import run
run(writable_report_path)
```

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

新ビルドには新しい証拠が必要です。識別は `veroscript.HOST_PLATFORM`、製品版は `veroscript.__version__` を使い、カーネル機種名やバイトコード情報を含む `RUNTIME_VERSION` は使いません。`python-mobile.json` の `pending` は受け入れ完了ではありません。Android は全ネイティブ依存と `zipalign -v -c -P 16 4 <application.apk>`、AAB 経由の署名アプリを確認します。iOS は署名、リソース、runpath、実機と両シミュレーター ABI の import を確認します。CPython/依存のライセンスを保持し、CPython/アプリのテストとストア方針確認を別途行います。

## ビルド識別情報と Android ライブラリ

製品バージョンは `VeroScriptVersion.h` から取得します。CPython バージョンや複数の情報を結合したランタイム文字列で代用しないでください。Android では `ApplicationInfo.nativeLibraryDir` にライブラリが展開されていることを確認し、最初の `import veroscript` より前に、その絶対パスを `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` に渡します。

<a id="android-build-contract"></a>

Android ビルドは `VEROSCRIPT_BUILD_APPLICATION=ON` を明示し、`python/veroscript/` 内の `licenses/application/miniz/LICENSE`、`licenses/application/json/LICENSE.MIT`、`licenses/application/sha256/LICENSE` を保持します。ツールチェーン検証は指定されていれば NDK 解決後の `ANDROID_ABI`／`ANDROID_PLATFORM_LEVEL`、なければ CMake の Android／system フィールドを比較します。旧 NDK の `CMAKE_SYSTEM_VERSION=1` を対象 API として扱いません。16 KB 検証は LOAD の整列とページ丸め後の RELRO 保護が領域外の書き込みデータに重ならないことを確認し、全 RELRO 終端の 16 KB 整列は要求しません。対応 NDK の共有 STL には非整列 RELRO 末尾があり得ます。

<a id="compiler-nesting-acceptance"></a>

## ネイティブコンパイラのスタック

埋め込みホストは、8 MiB 以上のネイティブスタックを持つワーカーでコンパイルしてください。Android JNI や Apple dispatch の小さいスタックでは、パーサーのネスト上限診断より前に溢れる場合があります。Session の生成・使用・破棄は同じワーカーで行い、既存の Session やコールバックを暗黙に移動しないでください。アプリの Python スレッドを作る前にスタックを設定するか、ネイティブホストで確保します。device_smoke.run もそのワーカーで実行します。既存の4グループは深度200、深度512の上限診断と再利用を検証します。新しい証拠には compilerNestingChecked=true が必要で、古い証拠は受理されません。

`compilerNestingChecked` は証拠メタデータであり公開 Session API ではありません。`verify_mobile.py::verify_package` は schemaVersion 1 と一致する build／runtime／ABI／slice／成果物識別に加えて厳密な真偽値 true を要求し、run は exercise_runtime が成功してから記録します。Python Session／C ABI コンパイルは Mobile::FRunner のスタックアダプターを使いません。下の例にはアプリ所有の書込み可能ディレクトリ内の絶対 report_file を渡し、待機するこの関数は UI スレッド外で呼び出してください。模擬回帰はロジックの検査であり、対象バイナリを検証しません。

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
