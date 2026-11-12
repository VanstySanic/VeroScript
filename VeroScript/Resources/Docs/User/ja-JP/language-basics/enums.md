# 列挙型

enum は、名前付き整数値の有限セットを表します。 VeroScript は通常の列挙型とスコープ付き列挙型 (`enum class`) をサポートしており、基になる整数型を明示的に指定できます。

## 列挙型の宣言

通常の列挙型のメンバーはデフォルトで `0` から始まり、後続のメンバーは 1 ずつ増加します。

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

メンバーには明示的な整数値を割り当てることもできます。明示的な値のないメンバーは、前のメンバーから継続します。

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

列挙子の値は基になる型で表現可能な整数定数式である必要があります。検査付き演算、先行する列挙子、条件を満たす [constexpr 呼び出し](../language-reference/constexpr.md)を使用できます。前方参照、循環参照、オーバーフローはエラーです。

## スコープ付き列挙型

`enum class` を使用して、メンバーを列挙型のスコープ内に保持します。

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

スコープ付き列挙型のメンバーは、それを囲んでいるスコープ内に裸の名前として導入されません。このため、メンバー名が衝突する可能性がある場合、または異なる列挙型を明確に区別しておく必要がある場合には、`enum class` が適切な選択肢となります。ドキュメントと新しいコードでは、メンバー アクセス用に `Type::Member` フォームを優先する必要があります。

通常の列挙型のメンバーは直接使用できます。

```VeroScript
State state = Running;
```

## 基礎となる整数型

基礎となる型が指定されていない場合、列挙型はデフォルトで `int` を使用します。表現可能な範囲を明示的に制御するには、`int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64` などの固定幅の整数型を使用します。

```VeroScript
enum class ByteState : uint8
{
	Empty = 0u,
	Full = 1u
};

enum class SignedLimit : int16
{
	Minimum = -32768,
	Maximum = 32767
};
```

符号なしの基になる型は負の値を表すことができません。基になる型が指定されると、すべてのメンバー値がその型の範囲内に収まる必要があります。

## 列挙値の使用

列挙値は比較でき、コンテナ内のキーまたは要素として使用できます。

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u,
	Paused = 2u
};

map<RunState, string> labels = map<RunState, string>();
labels[RunState::Running] = "running";

set<RunState> states = set<RunState>();
states.add(RunState::Paused);
```

列挙型は、有限で安定した状態のセットに適しています。 `enum class` の場合、無関係な値と自由に混合できる任意の整数として扱うのではなく、列挙型のセマンティクスを保持します。

## 列挙型のエクスポート

列挙型は、`import` を通じて他のモジュールで使用できるようにエクスポートできます。

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

モジュールをインポートした後、完全な列挙型名を使用してスコープ付きメンバーにアクセスします。

```VeroScript
Operation operation = Operation::Add;
```

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/enums.md) · [fr-FR](../../fr-FR/language-basics/enums.md) · [ja-JP](enums.md) · [ko-KR](../../ko-KR/language-basics/enums.md) · [zh-CN](../../zh-CN/language-basics/enums.md) · [zh-TW](../../zh-TW/language-basics/enums.md)
