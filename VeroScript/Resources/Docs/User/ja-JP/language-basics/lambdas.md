# ラムダと関数の値

ラムダは、保存、渡し、呼び出しが可能な関数値です。関数タイプでは `function` のスペルが使用されます。 `fn` はそのコンパクトなエイリアスです。

## 関数の種類

関数型は、パラメーター リストと戻り値の型を組み合わせたものです。パブリック コードでは矢印の周囲にスペースを入れて、長い宣言も読みやすいようにします。

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

関数の値は、パラメーター、戻り値、またはローカル変数にすることができます。

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int` と `function(int) -> int` は同等です。公開ドキュメントや長い宣言では、通常、完全な `function` スペルが使用されます。

## ラムダ式

ラムダは式本体またはブロック本体のいずれかを使用できます。

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

通常の関数に使用されるのと同じ括弧構文を使用して関数値を呼び出します。

```VeroScript
int a = increment(41);
int b = square(5);
```

関数型に参照パラメーターがある場合、呼び出しでは明示的な参照マーカーを使用する必要があります。公開されている例では、`T&` と `&expression` が好まれます。 `ref T` および `ref expression` のスペルは引き続きサポートされます。

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## 値のキャプチャ

ラムダは、それを囲んでいるスコープからローカル変数をキャプチャできます。通常のラムダと `[=]` リクエスト値のキャプチャ。明示的なリストでは、リストされている名前のみが取得されます。

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

値キャプチャはクロージャ作成時に初期化され、既定では読み取り専用です。引数リストの後に `mutable` を付けると、所有するキャプチャを変更でき、次の呼び出しにも保持されます。関数値のコピーは環境を共有し、作成式の再評価は独立した環境を作ります。通常のキャプチャでは const は解除されません。

VM、Mixed、JIT、Core 対応 AOT は実行サービスを共有します。独立 C++/C# ソース出力は値キャプチャとスカラー static に対応します。参照初期化キャプチャ、複雑な static オブジェクト、外側の static の参照には Core 経路が必要です。Python ソース出力は引き続き Lambda とローカル static に未対応です。

キャプチャやレシーバーを所有する Lambda が参照を返す場合も、独立 C++ 出力には Core AOT が必要です。ネイティブ参照では、関数値の解放後までクロージャーの記憶域を保持できません。

空のキャプチャ リスト `[]` は、囲むローカル名をキャプチャしませんが、グローバル名は通常の名前検索を通じて利用可能なままです。

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## リファレンス キャプチャ

VeroScript は参照キャプチャをサポートしています。 `[&]` は、使用する周囲のローカル変数を参照によってキャプチャし、`[&name]` は名前付き変数のみを参照によってキャプチャします。デフォルトのキャプチャは、明示的な値のキャプチャと組み合わせることができます。

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

利用可能なキャプチャ フォームは次のとおりです。

- `[&]`: 参照によってローカルを囲むキャプチャが使用されます。
- `[&name]`: `name` を参照によってキャプチャします。
- `[=]` または通常の `function(...)` ラムダ: 値でローカルを囲むキャプチャが使用されます。
- `[=, &name]`: `name` を除き、デフォルトで値によってキャプチャします。
- `[&, name]`: `name` を除き、デフォルトで参照によるキャプチャ。
- `[]`: 囲んでいるローカルをキャプチャしません。

参照キャプチャされたローカルは、安全な共有ストレージに昇格されます。したがって、クロージャは、ダングリング スタック スロットを保持することなく、クロージャを作成した関数よりも存続できます。囲んでいるスコープ、同じバインディング上の別のクロージャ、および現在のクロージャは、同じ変更可能な状態を観察します。囲んでいるバインディングが const の場合、参照キャプチャは読み取り専用の性質を保持します。

明示的なキャプチャ リスト内の名前は、許可された囲みバインディングを参照する必要があります。キャプチャでは、デフォルトのキャプチャですでにカバーされているフォームを重複して繰り返すことはできません。空のキャプチャ リストは、それを囲んでいるローカルにアクセスできません。グローバルとオブジェクト メンバーは、引き続き独自の名前検索とハンドル セマンティクスの対象となります。

## メンバー関数からラムダを返す

メンバー関数は、現在のオブジェクトの状態を取得する関数値を返すことができます。

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

ここで、`value` は、スコープ外になった通常のローカルではなくオブジェクト メンバーです。クロージャは依然としてクラスハンドルとオブジェクトの有効期間セマンティクスに従います。参照キャプチャは、任意のオブジェクトが値として自動的にコピーされるかどうかではなく、バインディングへのアクセス方法を変更します。

ラムダがレシーバー自体を使用する場合、レシーバーはそれを囲む型の
セマンティクスに従います。クラス レシーバーは共有ハンドルとして保持
されるため、後から行ったフィールド変更を観測します。構造体レシーバーは
値のスナップショットとしてキャプチャされます。ネストしたラムダも、同じ
規則に従って、すでにキャプチャされたレシーバーを保持またはコピーします。
キャプチャなしのラムダは `this` を使用できません。

## 参照との関係

参照パラメータ、参照の戻り値、および安全な参照ソースの呼び出しルールについては、[参照と参照のセマンティクス](references.md) で説明されています。参照キャプチャはクロージャ ルールであり、通常の値キャプチャで置き換えることはできません。

完全な例については、[サンプルインデックス](../examples.md) の Lambda エントリを参照してください。

## 初期化キャプチャ、mutable、ローカル static

`auto` を使う場合は、`() mutable -> void { ... }` のように Lambda の戻り値型を明示してください。代入先などに `function() -> void` のような関数型の文脈があれば、Lambda の戻り値の矢印を省略できます。既存の文脈による型推論の規則が適用され、`mutable` 自体が本体から戻り値型を推論することはありません。

ローカル静的定数では、`static const int value = 42;` と `const static int value = 42;` は同じ意味です。表記を統一するため `static const` を推奨します。

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

初期化キャプチャの右辺は外側のスコープで左から右へ一度だけ評価され、新しい名前は本体内でのみ有効です。`[&name = expression]` は保持可能な左辺値を要求し、const を維持します。グローバル変数、型とローカルの static は直接参照され、値のスナップショットには初期化キャプチャを使います。

ローカル static は最初に宣言へ到達した際に初期化され、後の呼び出しと共有されます。同じラムダ式から作るクロージャは関数 static を共有します。失敗後のクリーンアップが成功すれば再試行でき、再帰的初期化はエラーです。プログラム状態、Session、worker は分離され、状態をクリアすると初期化前に戻ります。static 参照変数は未対応です。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/lambdas.md) · [fr-FR](../../fr-FR/language-basics/lambdas.md) · [ja-JP](lambdas.md) · [ko-KR](../../ko-KR/language-basics/lambdas.md) · [zh-CN](../../zh-CN/language-basics/lambdas.md) · [zh-TW](../../zh-TW/language-basics/lambdas.md)
