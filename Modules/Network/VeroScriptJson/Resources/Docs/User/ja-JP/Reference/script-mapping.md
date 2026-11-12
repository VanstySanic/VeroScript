# スクリプトオブジェクトの変換

json::converter<T> は活動中 VM の script 値を変換し、入出力 Schema を生成します。native extension 登録後に bindings、processors、dependencies、Owners を module environment へまとめて渡し、project owner は json provider に依存を宣言してからコンパイルします。カタログだけでは processor を提供できません。native C++ 変換は独立です。

write(ref value) は const ref を明示します。write_with_options/read_with_options/try_write/try_read と options 形式は通常の失敗で false と json::error、出力保持、成功時にエラーを消去します。取消、timeout、回復不能 VM エラーは実行を終了します。

bool／整数／有限 float／string は型と範囲を保持し、整数を double にしません。enum は member 名で JsonName により変更できます。array/deque/queue/stack/priority_queue は values() の順、stack は底から上です。set は重複拒否、multiset は個数保持です。string key の map/ordered_map は object、それ以外と全 multimap は key_value_pairs が必要です。公開 struct/class instance field を変換し、class は null を許容、json::value は元値を保持します。hash の順序はコンテナー契約、挿入順には ordered を使います。特殊 host 型や function には明示 converter が必要です。

JsonName(value) は名前、JsonIgnore は入出力除外、JsonRequired は存在必須（null 可否は別）、JsonSerializeOnly は入力拒否、JsonDeserializeOnly は出力除外です。JsonProperty(name,setter) は引数なし const getter を明示し、setter なしなら出力専用です。JsonFactory(type) は create(value,context)、JsonConverter(type) は adapter を指定します。アクセス可能な継承公開 field を含み、static／非公開は除外、const は出力専用です。省略された非必須 field は通常構築の値を保ちます。重複／矛盾は失敗し、未知 field は set_ignore_unknown(true) 以外拒否です。

mapping_options は ignore_unknown/numeric_enums/key_value_pairs/exact_floating と setter、set_limits(depth,nodes) を提供し、depth は 1–256、node は正数です。値コピー設定ごとに独立変換 context を持ちます。instance_pointer は JSON Pointer escape、class の循環を検出し、別 branch の共有は繰り返し出力します。decode は新しい値を成功時だけ確定しますが、factory／property／converter の外部作用は戻しません。

adapter は static encode(const Target&,json::value&,mapping_context)、decode(json::value,Target&,mapping_context)、schema(mapping_context) を定義します。custom_converter<Target,Adapter> は create(value,context) -> Target も必要です。非既定構築 struct は factory/create が必要、class は null から開始できます。context.fail/fail_range/adopt(error) を使い、診断なし false は Callback です。子の converter::encode/decode/schema_nested は context を共有し、独自 Schema は実際の変換を表します。

schema() は入力、schema_with_options(options,false) は出力です。必須／ignore／未知／禁止 field、整数範囲、要素、enum、null 可否を方向別に生成し、再帰は $defs、コンストラクターは呼びません。schema_nested cache は型と方向を区別し、set_schema_input 変更時に別方向の定義を流用しません。

自動生成は module／namespace 直下の型が対象です。local／nested 型には module helper を生成せず、その JsonFactory/JsonConverter は scope エラーです。見える scope で custom_converter を使ってください。JsonPolymorphic は非 generic table class 自身に static method を生成し、local／nested も可能です。所有権、可視性、アクセス、構築規則を維持します。field／alias／base／adapter は宣言 module、import／re-export、template arity／既定値／module 優先に従って解決します。type/value parameter（整数、bool、enum）や修飾 constexpr を保ち、異なる closed type の Schema cache は別、static constexpr field は除外です。

JSON processor 付き .vslib は生成 method を保持します。通常 library の公開 field も consumer 側で生成でき、構築とアクセス制約は残ります。生成の操作数、source byte、work 予算を守ります。AOT／他 host の対応経路はそれぞれ確認し、VM の確認だけを全 platform の保証にしません。

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

```VeroScript
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/script-mapping.md) · [fr-FR](../../fr-FR/Reference/script-mapping.md) · [ja-JP](script-mapping.md) · [ko-KR](../../ko-KR/Reference/script-mapping.md) · [zh-CN](../../zh-CN/Reference/script-mapping.md) · [zh-TW](../../zh-TW/Reference/script-mapping.md)
