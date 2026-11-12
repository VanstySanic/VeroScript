# C++ frontend、BindGen、Transpiler

## フロントエンドの AST

`VeroScriptCppFrontend` は有界な C++17 subset を token、source location、attribute、type、parameter、template、record、enum、variable、function、`typedef`/`using` alias、namespace、body の stable model に parse します。表現可能な場合、qualified name、C linkage、storage/semantic qualifier、constructor/destructor、friend、同 file の out-of-class definition を保持します。

`FParseOptions` は include/pragma、unknown preprocessor directive、function-body parse を、`FTypeMapOptions` は raw host pointer、rvalue reference、known host/enum type、type parameter、project alias を制御します。いずれも hidden global policy ではなく explicit option です。

collector は input をまたいで互換 declaration/forward declaration を merge し、一方の input の conventional member definition を他方の complete record と対にできます。duplicate definition、duplicate input path、internal-linkage collision、anonymous namespace、unsupported template、ownership/control-flow の意味を保存できない construct は拒否します。

## バインディングカタログの生成

BindGen は machine-readable Binding Catalog JSON、optional API reference、project manifest を emit します。merged catalog が authoritative で、API reference は review artifact であり compile しません。schema が表現できる場合だけ structured attribute、alias、enum identity、overload、member flag、documentation を保ちます。project generation は全 declaration を先に解決し、同 basename を deterministic に命名して output を一 transaction で置換します。

## ソーストランスパイラー

Transpiler は direct VeroScript meaning のある construct だけを lower します。VeroScript source と declaration-level source map を emit し、project mode は input ごとの module/map と manifest を生成します。既定で real VeroScript compiler が generated source を compile します。unsupported ownership、RTTI、pointer access、advanced template、`std` facility、attribute は明示的に失敗します。

メンバー `operator()` と `operator[]` はオーバーロードと参照パラメーターを保持します。ソース変換では参照戻り値を含む参照シグネチャに `T&` / `const T&` を使用し、演算子の実引数はコアの規則に従って暗黙的に参照へ束縛されます。BindGen は演算子と参照パラメーターのフラグを保持しますが、現在の Binding Catalog では格納先と寿命を表現できないため、ネイティブ参照戻り値を明示的に拒否します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/frontend-and-tools.md) · [fr-FR](../fr-FR/frontend-and-tools.md) · [ja-JP](frontend-and-tools.md) · [ko-KR](../ko-KR/frontend-and-tools.md) · [zh-CN](../zh-CN/frontend-and-tools.md) · [zh-TW](../zh-TW/frontend-and-tools.md)
