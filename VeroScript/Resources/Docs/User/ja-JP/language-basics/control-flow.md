# 制御フロー

制御フロー ステートメントは、ステートメントの実行順序を決定します。 VeroScript は、条件、ループ、`switch`、ジャンプ、および関数リターンをサポートします。

## `if`

`if` は、条件に基づいてブロックを選択します。 `else if` および `else` はオプションです。

```VeroScript
if (score >= 60)
{
	print("pass");
}
else if (score >= 0)
{
	print("fail");
}
else
{
	print("invalid score");
}
```

条件は、言語のブール規則によって評価できる式である必要があります。中括弧は分岐境界を明示するため、単一のステートメントであっても中括弧を使用することをお勧めします。

## `while`

`while` は、反復するたびにその状態をチェックします。

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

最初に条件が false の場合、ループ本体は実行されません。

## `do ... while`

`do` は条件を調べる前に本体を一度実行します。末尾のセミコロンは必須です。

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

条件は `bool` でなければなりません。本体内の `continue` は条件評価へ進み、`break` はループを終了します。

## `for`

従来の `for` ステートメントには、初期化子、条件、および反復式が含まれています。

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

イニシャライザで宣言された変数のスコープは `for` ステートメントです。条件と反復式は省略できますが、ループには明確な終了条件が必要です。

初期化節と反復節には、それぞれコンマ区切りの式リストを記述でき、左から右へ評価されます。これは `for` 節だけの構文です。VeroScript に一般的なコンマ演算子はありません。初期化節では `int index = 0, end = 3` のような同型の宣言グループも使用でき、名前は左から右へ可視になり、そのスコープは `for` 文に限られます。

## 範囲ベースのループ

範囲ベースのループは、`:` を使用して配列またはコンテナーを反復処理します。

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

参照ループ変数を使用して要素をその場で変更します。

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

マップ コンテナはキー/値ループ形式もサポートします。正確な形式については、コンテナのリファレンスを参照してください。

## `break` および `continue`

`break` は最も近い外側のループまたは `switch` を終了します。`continue` は最も近い外側のループの次の反復へ進みます。`switch` は `continue` の対象ではありません。

```VeroScript
for (int value : values)
{
	if (value < 0)
	{
		continue;
	}
	if (value == 100)
	{
		break;
	}
	print(value);
}
```

## `switch`

`switch` は、式に基づいて `case` を選択します。現在の実装では、整数、列挙型、文字列、ワイド文字列をスイッチ値として受け入れます。

```VeroScript
switch (state)
{
case 0:
	{
		print("off");
		break;
	}
case 1:
	{
		print("on");
		break;
	}
default:
	{
		print("unknown");
		break;
	}
}
```

VeroScript では、マークのないフォールスルーは許可されません。実行を次の `case` に継続する必要がある場合は、`fallthrough` を明示的に使用します。

```VeroScript
switch (value)
{
case 1:
	{
		print("one");
	}
	fallthrough;
case 2:
	{
		print("one or two");
		break;
	}
default:
	{
		break;
	}
}
```

`fallthrough;` は `case` 直下の最後の文でなければなりません。分岐の内容を波括弧でまとめる場合は、そのブロックの後に置き、入れ子のブロック、条件、ループ内には置かないでください。

## 例外

VeroScript では、スクリプト例外とエンジンまたはランタイムの障害を区別します。
`throw expression;` は `void` 以外の任意のスクリプト値を送出します。`try` 文は
関数の内部に置き、少なくとも 1 つの `catch` または `finally` を含める必要があります。

```VeroScript
int main()
{
	try
	{
		throw "failed";
	}
	catch (const string& message)
	{
		print(message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

`catch` 節はソース順に照合されます。型付きハンドラーは `catch (Type name)`、
`catch (const Type& name)`、`catch (Type& name)`、または `catch (ref Type name)` と
記述できます。`catch (Type)` のように名前を省略することもできます。`catch (...)`
はすべてのスクリプト例外を受け取り、束縛を持たず、最後の `catch` 節でなければなりません。
型付きハンドラーは送出された値の型に一致し、クラスまたはインターフェイスでは代入可能な
派生オブジェクトも受け取ります。ハンドラーを一致させるための数値変換は行われません。

値による束縛はコピーです。参照による束縛は元の値を参照するため、書き込み可能な参照を
変更すると伝播する値も変わります。式のない `throw;` は `catch` ハンドラー内でのみ有効で、
値による束縛の変更後のコピーではなく、元の例外オブジェクトを再送出します。送出された値は
一致するハンドラーが見つかるまで呼び出しを越えて伝播します。

`finally` はハンドラーの後に置くことも、`catch` なしで使用することもできます。保護された
`try` または `catch` から制御が離れるとき、通常終了、`return`、ループ転送、スクリプト例外の
伝播、ランタイム障害の伝播を含めて実行されます。`return`、`goto`、外側のループを対象とする
`break`/`continue` など、`finally` の外へ出る非局所転送はコンパイラーに拒否されます。

`finally` 内ではループと入れ子の `try`/`catch` を使えます。内部で捕捉した例外は、保留中の外側の戻り値や例外を変更しません。未捕捉のスクリプト例外が `finally` を離れると、保留中の転送を置き換え、外側のハンドラーへ伝播します。クリーンアップでデストラクタを呼び、再びスクリプトを実行する場合も同じ規則に従います。

エンジンまたはランタイムの障害はスクリプト値ではないため `catch (...)` では抑止できません。
失敗した `assert` もこのランタイムエラーに該当しますが、その伝播中にも `finally` は実行されます。

## `goto` とラベル

ラベルは、名前とその後に続くコロンで構成されます。 `goto` はそのラベルにジャンプします。

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

`goto` は、実際にメリットを享受できるローカル制御フロー用に予約してください。過度のジャンプは関数の解析を困難にします。

## `return`

`return` は現在の機能を終了します。 `void` 以外の関数は、宣言された型と互換性のある値を返す必要があります。 `void` 関数は式なしで返される場合があります。

```VeroScript
int square(int value)
{
	return value * value;
}

void log_done()
{
	print("done");
	return;
}
```

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/control-flow.md) · [fr-FR](../../fr-FR/language-basics/control-flow.md) · [ja-JP](control-flow.md) · [ko-KR](../../ko-KR/language-basics/control-flow.md) · [zh-CN](../../zh-CN/language-basics/control-flow.md) · [zh-TW](../../zh-TW/language-basics/control-flow.md)
