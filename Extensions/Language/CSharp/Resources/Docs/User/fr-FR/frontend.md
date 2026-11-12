# Frontend C#

`VeroScriptCSharpFrontend` est le frontend sémantique C# partagé par BindGen et
le Transpiler C#.
Roslyn est utilisé en interne, mais le modèle
public appartient à VeroScript et n'expose pas les objets Roslyn.

Le frontend accepte plusieurs fichiers et conserve espaces de noms, classes,
structs, interfaces, records, enums, delegates, champs, propriétés,
événements, constructeurs, méthodes, opérateurs, attributs, documentation XML,
formes génériques, emplacements source et métadonnées `ref`/`in`/`out`.

Le mappage comprend les primitifs C#, tableaux unidimensionnels, `List<T>`,
`Dictionary<TKey,TValue>`, `SortedDictionary<TKey,TValue>`, `HashSet<T>`,
`Queue<T>`, `Stack<T>`, enums déclarés et types hôte connus. Les types nullable,
pointeurs, delegates, `dynamic`, types non résolus et paramètres génériques
ouverts échouent explicitement. Ce module est une bibliothèque d'analyse de
build ; utilisez C# BindGen pour produire le catalogue et le code généré, et
`VeroScriptCSharpTranspiler` pour la conversion stricte C# vers VeroScript et
ses source maps.

## Arité générique des conteneurs standard

<a id="container-mapping-arity"></a>

Le mappage exige l’arité déclarée : un argument pour `List<T>`, `HashSet<T>`, `Queue<T>` et `Stack<T>`, deux pour `Dictionary<TKey,TValue>` et `SortedDictionary<TKey,TValue>`. Un type personnalisé de même nom mais d’arité différente n’est pas converti en conteneur intégré mal formé ; il exige un mappage explicite de type hôte connu ou échoue avec `VSCF2008`.

## Accessibilité des déclarations

L’accessibilité des propriétés et celle des types imbriqués sont évaluées séparément. Les classes de base implicites du CLR sont distinguées des héritages déclarés : un consommateur ne doit pas exposer un accesseur privé ni traiter `System.ValueType` comme une classe hôte de base.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/frontend.md) · [fr-FR](frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
