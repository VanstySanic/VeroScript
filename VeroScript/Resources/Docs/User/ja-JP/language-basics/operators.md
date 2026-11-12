# 演算子

演算子は、式の値を計算、比較、結合、または変更します。 VeroScript は、算術演算子、比較演算子、論理演算子、ビット単位演算子、代入演算子、インデックス付け演算子、メンバー アクセス演算子、および条件演算子をサポートしています。

## 演算子のカテゴリ

|カテゴリー |オペレーター |
| --- | --- |
|算数 | `+`、`-`、`*`、`/`、`%` |
|比較 | `==`、`!=`、`<`、`>`、`<=`、`>=` |
|論理 | `!`、`&&`、`||` |
|ビットごと | `&`、`|`、`^`、`<<`、`>>` |
|割り当て | `=`、`+=`、`-=`、`*=`、`/=`、`%=`、`&=`、`|=`、`^=`、`<<=`、`>>=` |
|インクリメント/デクリメント | `++`、`--` |
|条件付き | `condition ? when_true : when_false` |
|アクセス | `.`、`::`、`[]`、`()` |

## 優先順位と結合性

次の表は優先順位の低いものから高いものの順です。

| レベル | 演算子 | 結合性 |
| --- | --- | --- |
| 代入 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | 右 |
| 条件 | `?:` | 右 |
| 論理 OR | `||` | 左 |
| 論理 AND | `&&` | 左 |
| ビット OR | `|` | 左 |
| ビット XOR | `^` | 左 |
| ビット AND | `&` | 左 |
| 比較 | `==`, `!=`, `<`, `>`, `<=`, `>=` | 左 |
| シフト | `<<`, `>>` | 左 |
| 加減 | `+`, `-` | 左 |
| 乗除剰余 | `*`, `/`, `%` | 左 |
| 前置 | `!`, `~`, 単項 `+`/`-`, 前置 `++`/`--`, `new`, `delete`, キャスト, `ref` | 右 |
| 後置/アクセス | 呼び出し `()`, 添字 `[]`, `.`, `::`, 後置 `++`/`--` | 左 |

`&&` と `||` は左オペランドを先に評価して短絡します。`?:` は条件を先に評価し、片方の分岐だけを評価します。それ以外では、副作用の順序が重要なときにオペランドや関数引数の相対評価順へ依存せず、別の文へ分けてください。演算子オーバーロードは優先順位や結合性を変更しません。一般的なコンマ演算子はなく、コンマ区切りの式は従来形式の `for` の初期化節と反復節だけで使用できます。

計算順序を明示するには、かっこを使用します。

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## 算術演算子

`+`、`-`、`*`、`/`、および `%` は数値演算を実行します。 `+` は、同じテキスト タイプの値を連結することもできます。

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string` と `wstring` を直接連結することはできません。

## 比較演算子と論理演算子

比較演算子はブール値の結果を生成します。論理演算子はブール式を結合します。

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

## 代入演算子

`=` は通常の割り当てを実行します。複合代入では、演算と代入を組み合わせます。

```VeroScript
value = 10;
value += 2;
value *= 3;
```

代入の両側が言語の変換規則を満たしている必要があります。代入によって、左側の変数の静的型は変更されません。

## アクセス演算子

インデックス演算子 `[]` は、配列またはコンテナ要素にアクセスします。メンバー アクセス演算子 `.` は、オブジェクト メンバーにアクセスします。

```VeroScript
int first = values[0];
point.x = 10;
```

名前空間と型メンバーでも `::` を使用できます。

```VeroScript
Color color = Color::Red;
```

値メンバーには `.` を使い、名前空間、型、enum メンバー、static メンバーには `::` を優先します。先頭の `::` はグローバル/ルート スコープから検索を開始し、現在の名前空間、ローカル エイリアス、`using namespace` のフォールバックを無視します。

## 演算子のオーバーロード

ユーザー定義型は、その型が選択された操作に参加するように演算子メソッドを宣言できます。オーバーロードは、コンパイラーのパラメーターと戻り値の型の規則を満たす必要があります。その型にとって予想外の意味を持つ演算子をオーバーロードしないでください。

メンバー演算子は、現在のオブジェクトを暗黙的な左オペランドとして使用します。非メンバー演算子関数は、オペランドを明示的に宣言できます。

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

演算子のオーバーロードは、二項演算と単項演算、複合代入、インデックス付け、関数呼び出し構文をカバーできます。オブジェクトを明示的にブール値に変換できるように、型で `bool operator bool()` などの変換演算子を宣言することもできます。

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```


組み込みのブール演算子 `&&` と `||` は常に短絡評価を維持し、ユーザー定義のオーバーロードで置き換えられることはありません。ユーザー定義演算子の候補は、少なくとも一方のオペランドがオーバーロード可能なユーザー型またはホスト型である場合にのみ検討されるため、一般型の両方のオペランドを変換しなければならない演算子は選択されません。

一部の比較フォームでは、`operator<` を使用して逆方向をサポートするなど、宣言された基本演算子からミラーリングされた動作を取得できます。型のセマンティクスを直接表現する演算子を宣言することを好み、オーバーロードされた演算子はよく知られた意味との一貫性を保ちます。

## 演算子オーバーロードの参照

演算子の引数は C++ と同様に暗黙に参照へ束縛されます。`a + b`、`a[i]`、`f(x)` に `&` や `ref` は不要です。通常の関数・メソッド呼び出しでは引き続き明示的な参照引数マーカーが必要です。`T&` はエイリアス解決後に同じ型となる書き込み可能な格納場所を要求し、定数、一時値、数値変換を拒否します。`const T&` は読み取り専用の格納場所と変換による一時値を受け入れます。

他の変換が同じなら、書き込み可能な左辺値は `const T&` より `T&` を優先します。値渡しと参照渡しの変換を区別できない場合は曖昧として拒否されます。

`T&` を返す演算子式は代入先やローカル参照別名の元にできます。`const T&` の結果は読み取り専用です。レシーバーの const とアクセス権も検証されます。参照戻り値の寿命検査は維持され、`return this` は現在のオブジェクトのハンドルを参照します。

前置 ++/-- のメンバー版は明示的な引数を取らず、後置版は値渡しの `int` タグを取ります。自由関数版はオペランドを取り、後置版ではタグも取ります。宣言した戻り値が式の結果になります。`void` の複合代入をオブジェクト値として使えず、非 void の結果も代入先へ自動的に書き戻されません。

`operator()` はデフォルト引数と本言語の型付き可変長引数を受け入れます。他の演算子では使用できません。`operator=` は引数を一つ取るメンバーに限ります。変数宣言の初期化は従来の意味を維持します。

`static` として宣言できるのは `operator()` と `operator[]` だけです。継承した演算子には、宣言元の型のジェネリック引数が適用されます。静的フィールドと名前空間のグローバル変数は参照引数に束縛できますが、一時構造体のフィールドは書き込み可能な参照に束縛できません。

自由関数の `operator[]`/`operator()`、比較の反転、複合代入の合成は VeroScript の拡張です。クラスのハンドルと構造体の値の意味は従来どおりです。C++ のポインター、右辺値参照、ムーブの完全対応ではありません。

この節の新機能は VM、mixed、JIT 実行バックエンドが対象です。ソース AOT エクスポーターの対応範囲は別です。新しい参照演算子の組み合わせ、代入のオーバーロード、後置タグ、呼び出し演算子のデフォルト・可変長引数は未対応であり、明示的にエラーになります。

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/operators.md) · [fr-FR](../../fr-FR/language-basics/operators.md) · [ja-JP](operators.md) · [ko-KR](../../ko-KR/language-basics/operators.md) · [zh-CN](../../zh-CN/language-basics/operators.md) · [zh-TW](../../zh-TW/language-basics/operators.md)
