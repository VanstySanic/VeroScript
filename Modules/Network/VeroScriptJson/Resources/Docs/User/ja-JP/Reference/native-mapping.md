# C++ 型マッピング

VeroScriptJsonMapping.h を include し VeroScript::Json にリンクします。Core は不要です。TObjectMapping<T> で field／property を明示して TryEncode/TryDecode を使います。成功時だけ出力を置き換えます。変換を準備してから通常の構築と代入を行い、既定コンストラクターがなければ shared_ptr<T> factory を渡せます。コンストラクター／property／converter の外部副作用は rollback しません。

名前は正当な UTF-8 と NUL を許容し、path は RFC 6901 で escape します。未知 field は bIgnoreUnknownFields 以外拒否します。FFieldOptions は required/ignore/serialize/deserialize を制御し、出力専用 field の入力は失敗、ignore は読み飛ばします。重複／方向矛盾は登録失敗です。基底 field は明示追加し、static や property の自動列挙はしません。登録した Property のみ呼び出します。

bool、64-bit までの整数、有限 float、所有 UTF-8 string の JSON 型を保持し、整数は double を経由しません。bExactFloatingPoint は丸めを拒否し、既定は検査付き丸めです。vector/list/deque/固定配列は array、固定長を検査します。set は重複拒否、multiset は保持し、順序は各コンテナーに従います。一意な string key の map は object、それ以外や multimap は bMapAsPairs が必要です。optional/shared_ptr/unique_ptr は null、通常の値は非 null です。RegisterEnum は登録名、bEnumsAsNumbers は整数ですが未登録値は拒否します。未対応型を空 object に置き換えません。

RegisterConverter<T> は型付き encode/decode/Schema を提供します。再帰 FMappingContext は path、予算、取消を共有します。各呼び出しは不変 registry snapshot を取得し、registry コピーは登録状態を共有しても更新は進行中の呼び出しに影響しません。Owner は code／destructor／RTTI を保持します。結果および入れ子の shared_ptr はコピー／alias も含め converter 集合を保有し、registry や外側の値を破棄しても有効です。値移動や別 control block には呼び出し側の Owner が必要です。最初の type key の RTTI Owner は registry／snapshot 解放まで残ります。callback／factory は Owner より先、registry lock の外で破棄し、挿入失敗でも同じです。

現在の再帰 path の循環は json.cycle、別 branch の共有は値を繰り返し出力し graph identity は復元しません。TryMappingSchema<T>(Input/Output,...) は方向、$defs/$ref、整数範囲、長さ、enum を共有します。converter Schema がなければ失敗します。構築不変条件、独自比較、pair map key の一意性、厳密 float は decoder が検査します。FMappingOptions::Limits と借用 FWork がコンパクト JSON の byte、depth、node、text、number を制限し、最初のエラー/path を保ち、取消時に出力を確定しません。

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/native-mapping.md) · [fr-FR](../../fr-FR/Reference/native-mapping.md) · [ja-JP](native-mapping.md) · [ko-KR](../../ko-KR/Reference/native-mapping.md) · [zh-CN](../../zh-CN/Reference/native-mapping.md) · [zh-TW](../../zh-TW/Reference/native-mapping.md)
