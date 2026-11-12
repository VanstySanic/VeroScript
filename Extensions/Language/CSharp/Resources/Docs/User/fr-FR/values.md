# Valeurs C# et propriété

`VeroScriptValue` possède un handle de valeur Interop indépendant ; ce n’est pas un objet enfant d’une session. Libérez les valeurs possédées avec `Dispose()` ou `using`. `Clone()` crée un autre handle possédé selon la sémantique de clonage du runtime, sans garantir une copie profonde. `Reset()` remplace la valeur par une valeur vide. Synchronisez toute mutation ou libération concurrente.

## Contenus dépendant du module

Posséder un handle ne rend pas son contenu indépendant de la session. Les objets
et fonctions de script et les conteneurs modifiables à emplacements de valeurs de
module restent limités au thread créateur de la session et deviennent indisponibles
à sa fermeture, même vides. Un stockage créé par l’hôte avec ces emplacements se
lie à la session lors de sa première utilisation ; ses alias partagés suivent cette
propriété. `Clone()` ne supprime pas ces limites. Les rappels des workers VM ne
peuvent exporter que des valeurs possédées indépendantes ; les conteneurs vides à
emplacements variant/fonction ne peuvent pas être clonés vers un stockage possédé
externe. Un retour emprunté privé peut toujours transmettre des valeurs dans le
worker. Libérez les handles après usage, même après la fermeture de la session.

## Valeurs scalaires

Les méthodes de lecture doivent correspondre au type du script :

| Écriture | Lecture | Type script |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

`SetEnum(typeName, value)` / `GetEnum()` traitent les énumérations typées. `Kind` et `TypeName` décrivent la représentation courante. `SetNull()` crée `nullptr` ; `IsNull` teste uniquement cette valeur. Après `Reset()`, `Kind` vaut `Invalid` et `IsNull` est faux. L’accès après libération lève `ObjectDisposedException`. Une référence hôte nulle reste soumise au contrat du paramètre destinataire.

`Set(VeroScriptValue)` affecte une valeur complète en conservant l’identité runtime des conteneurs, objets hôtes, fonctions et canaux. La destination doit être modifiable ; une valeur empruntée en lecture seule ne peut pas servir de destination.

## Conteneurs et canaux

`SetArray`, `SetSequence`, `SetMap`, `SetSet` et `SetChannel` exigent des noms de types explicites. Les surcharges prenant `VeroScriptValueKind` sélectionnent le type exact : `SetSequence` accepte les séquences, dont `Queue`, `PriorityQueue`, `Stack` et `Deque` ; `SetMap` accepte `Map`, `OrderedMap`, `MultiMap`, `OrderedMultiMap` ; `SetSet` accepte `Set`, `MultiSet`, `OrderedMultiSet`.

Les maps ordinaires remplacent la valeur d’une clé répétée ; les multi-maps conservent chaque association. Les sets ordinaires éliminent les doublons ; les multi-sets les conservent. Une file de priorité présente d’abord l’élément de plus haute priorité aux opérations runtime `top` et `pop`.

Lisez les séquences avec `SequenceCount` / `GetSequenceElement`, les maps avec `MapCount` / `TryGetMapValue` et les sets avec `SetCount` / `SetContains`. `EnumerateMap` et `EnumerateSet` nécessitent `VeroScriptCapabilities.ContainerEnumeration` et visitent des instantanés en lecture seule. Leurs paramètres `VeroScriptBorrowedValue` expirent au retour de chaque appel du visiteur ; utilisez `Clone()` dans cet appel pour conserver une valeur.

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

Les canaux exposent `ChannelSend`, `ChannelTrySend`, `ChannelSendFor`, `ChannelReceive`, `ChannelTryReceive`, `ChannelReceiveFor` et `ChannelClose`. `ChannelTrySend` et `ChannelSendFor` renvoient un booléen ; les délais sont exprimés en millisecondes. Les réceptions non bloquantes ou limitées dans le temps renvoient une valeur possédée ou `null`. `ChannelCount`, `ChannelCapacity` et `IsChannelClosed` indiquent l’état courant. Une réception bloquante ne doit pas immobiliser un thread qui doit rester réactif. Les opérations de canal sont synchronisées par le runtime ; pendant leur usage concurrent, ne réinitialisez, ne remplacez et ne libérez pas le wrapper.

## Objets hôtes et fonctions conservées

`SetHostObject(typeId, typeName, managedObject)` maintient la charge utile managée en vie tant que la valeur native la conserve. La surcharge `Func<string, bool> isType` signale les sous-types compatibles. `GetHostObject()` renvoie `VeroScriptHostObjectInfo` ; `GetHostObject<T>()` vérifie le type managé.

`VeroScriptCallable` possède une fonction ou fermeture conservée. Libérez-le après usage. Par défaut, seul le thread créateur peut l’invoquer. `RetainCallable(..., ownerThreadOnly: false)` permet une invocation sur un autre thread, mais l’hôte doit la sérialiser avec toutes les opérations de la session associée et protéger son propre état. Le verrou interne du callable ne protège pas toute la session.

## Emprunts et durée de vie

Les contextes et valeurs empruntés des fonctions natives, intercepteurs, fabriques génériques, sérialiseurs, notifications et visiteurs restent limités au thread de l’appel courant. Un accès depuis un autre thread lève `VeroScriptException` avec `WrongThread` ; un accès après le retour lève `InvalidOperationException`. Clonez un contenu indépendant autorisé sur le thread du rappel avant de le transférer ; le clonage ne détache pas les contenus dépendant du module.

## API numériques exactes

C# garde bigint/rational Core comme valeurs hôtes opaques VeroScriptValue. VeroScriptCapabilities.Numerics annonce le service natif, sans conversion automatique System.Numerics.BigInteger ni service numérique géré public. Utilisez des fonctions de script typées ou l’ABI C native publique.

<a id="managed-call-lifetime"></a>

## Durée de vie des appels managés

Les opérations natives conservent les wrappers valeur/paquet fournissant leurs handles jusqu’à la fin de l’appel et de la copie de toute vue native retournée. Cela inclut les valeurs produites par un indexeur `IReadOnlyList<VeroScriptValue>` et les récepteurs d’opérations channel bloquantes ou de visiteurs. Les wrappers conservés sont des snapshots : remplacer une case du tableau d’arguments pendant un rappel ne redirige pas l’écriture native `ref`/`out` vers son remplaçant. Ces garanties n’autorisent pas une libération explicite concurrente et ne prolongent pas les emprunts du rappel.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/values.md) · [fr-FR](values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
