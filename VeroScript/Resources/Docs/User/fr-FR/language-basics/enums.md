# Énumérations

Une énumération représente un ensemble fini de valeurs entières nommées. VeroScript prend en charge les énumérations ordinaires et les énumérations étendues (`enum class`) et permet de spécifier explicitement le type entier sous-jacent.

## Déclarer une énumération

Les membres d'une énumération ordinaire commencent par `0` par défaut, et les membres suivants augmentent de un :

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

Les membres peuvent également se voir attribuer des valeurs entières explicites. Un membre sans valeur explicite continue du membre précédent :

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

Les membres doivent être des expressions constantes entières représentables par le type sous-jacent. Les calculs contrôlés, les membres précédents et les appels [constexpr](../language-reference/constexpr.md) admissibles sont pris en charge ; références en avant, cycles et débordements sont des erreurs.

## Énumérations de portée

Utilisez `enum class` pour conserver les membres dans la portée du type enum :

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

Les membres d’une énumération de portée ne sont pas introduits sous forme de noms nus dans la portée englobante. Cela fait de `enum class` un bon choix lorsque les noms de membres peuvent entrer en collision ou lorsque différents types d'énumération doivent rester clairement distincts. La documentation et le nouveau code doivent préférer le formulaire `Type::Member` pour l'accès des membres.

Les membres d'une énumération ordinaire peuvent être utilisés directement :

```VeroScript
State state = Running;
```

## Types entiers sous-jacents

Lorsqu'aucun type sous-jacent n'est spécifié, une énumération utilise `int` par défaut. Pour contrôler explicitement sa plage représentable, utilisez des types entiers de largeur fixe tels que `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32` et `int64` :

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

Un type sous-jacent non signé ne peut pas représenter des valeurs négatives. Une fois qu'un type sous-jacent est spécifié, chaque valeur de membre doit correspondre à la plage de ce type.

## Utilisation des valeurs enum

Les valeurs d'énumération peuvent être comparées et utilisées comme clés ou éléments dans des conteneurs :

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

Les énumérations conviennent aux ensembles d’états finis et stables. Pour un `enum class`, conservez sa sémantique de type enum au lieu de le traiter comme un entier arbitraire qui peut être librement mélangé avec des valeurs non liées.

## Exporter une énumération

Une énumération peut être exportée pour être utilisée par d'autres modules via `import` :

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

Après avoir importé le module, utilisez le nom complet du type d'énumération pour accéder aux membres étendus :

```VeroScript
Operation operation = Operation::Add;
```

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/enums.md) · [fr-FR](enums.md) · [ja-JP](../../ja-JP/language-basics/enums.md) · [ko-KR](../../ko-KR/language-basics/enums.md) · [zh-CN](../../zh-CN/language-basics/enums.md) · [zh-TW](../../zh-TW/language-basics/enums.md)
