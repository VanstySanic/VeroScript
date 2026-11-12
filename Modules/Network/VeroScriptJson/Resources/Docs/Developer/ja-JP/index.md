# JSON の保守

データ層は DOM／text／stream を所有します。Schema は記録された patch 付き jsoncons 1.10.0 と独立 QuickJS-ng 0.17.0 regex／Unicode を使い、JavaScript runtime は含めません。厳密な数値 adapter を維持します。C++ 変換は Core 非依存、script 変換は module processor が所有します。単純 struct module は縮小 support、それ以外の import、generic、継承、注釈、実行 member、複雑な初期値は完全 support を使用し、宣言順、scope、closed type identity を保持します。

module CMake を使います。root は VEROSCRIPT_BUILD_JSON、static／dynamic bind は VEROSCRIPT_JSON_BUILD_BINDINGS で制御します。対応する installed Core SDK があれば Core を再構築せず bind を構築できます。native は VeroScript::Json、COMPONENTS Bindings は VeroScript::JsonBindings です。Bindings install component は data と license に追加するもので、dynamic module は lib/veroscript/modules に配置します。登録 identity は製品 version だけで決めません。

[ネットワーク: ソースの所属 — ビルドとネイティブモジュール登録](../../../../../VeroScriptNetwork/Resources/Docs/Developer/ja-JP/index.md#network-execution-assembly) を参照してください。

実行が許可される時、native data、static／dynamic 登録、source module、binary library を個別検証します。VEROSCRIPT_JSON_SCHEMA_CORPUS は固定 suite と format、未知 format は注釈モードです。VEROSCRIPT_JSON_NORMALIZATION_CORPUS は Unicode 17 NormalizationTest.txt、VEROSCRIPT_JSON_CORPUS は JSONTestSuite です。取得は Resources/Tools/Tests の明示 helper のみ、通常 build は download しません。source／corpus／toolchain identity と実結果を記録し、過去の件数で現行 host、JIT/AOT、mobile、browser を認定しません。

6言語の json.json、安定 ID と引数、share/VeroScriptJson/Resources/Localization 配置を維持します。json profile は置換であり累積 overlay ではありません。固定 input で Resources/Tools/Unicode/generate_schema_unicode.py を実行して Unicode 表を更新し、Python を製品依存にしません。原文 notice と source inventory を保持します。

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/ja-JP/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)
