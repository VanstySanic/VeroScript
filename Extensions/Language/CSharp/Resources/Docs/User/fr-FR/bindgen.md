# C# BindGen

`VeroScriptCSharpBindGen` consomme le modèle sémantique C# et produit les métadonnées Binding Catalog v1. Il peut aussi générer un enregistrement sans réflexion pour `VeroScriptCSharp`, une référence API et un pont AOT pour les programmes C# générés.

## Choisir les sorties

La CLI exige `--catalog`. `--registration`, `--aot-bridge`, `--api-reference` et `--api-reference-dir` sélectionnent des sorties supplémentaires ; omettez les options d’enregistrement et de pont pour produire seulement les métadonnées. Le Catalog décrit le contrat sans installer de callbacks. Compilez l’enregistrement avec `VeroScript.CSharp`. Compilez le pont AOT avec les déclarations managées d’origine et le programme C# généré ; il appelle directement le code, sans réflexion ni `VeroScriptInterop`.

La bibliothèque utilise d’autres valeurs par défaut : `CSharpBindGenOptions.EmitRegistrationSource` et `EmitApiReference` valent `true`, tandis que `EmitAotBridgeSource` vaut `false`. Pour produire les métadonnées sans adaptateur exécutable, définissez explicitement `EmitRegistrationSource=false` et `EmitAotBridgeSource=false`. Une restriction d’un adaptateur facultatif ne rend pas automatiquement les métadonnées correspondantes non prises en charge.

## Règles communes aux entrées

Les classes, structs, enums et membres publics sont considérés par défaut. `--require-attribute` sélectionne les types marqués `[VeroScriptBinding]` ; `[VeroScriptIgnore]` exclut les déclarations. Types ou membres hôtes génériques, async, pointeurs, types nullable ou delegate, paramètres facultatifs ou `params`, opérateurs, retours et champs par référence, indexeurs, membres requis et surcharges non représentables sont rejetés. Les constructeurs des classes abstraites sont omis. Les sorties facultatives ajoutent leurs propres restrictions.

### Clés de conteneur

Les clés de map et ordered-map, ainsi que les éléments de set, peuvent être
`sbyte`, `int`, `uint`, `long`, `ulong`, `string` ou une enum déclarée.
`sbyte` correspond au `char` signé 8 bits de VeroScript ; toute autre forme de
clé échoue avant qu’un Binding Catalog n’atteigne l’importateur runtime.

## Limites de l’enregistrement exécutable

Les conteneurs générés pour l’enregistrement sont des tableaux/listes, maps, ensembles, queues ou stacks à un seul niveau, avec éléments scalaires, chaînes ou enums déclarées. Les clés de map et éléments de set doivent aussi respecter les règles ci-dessus. Les conteneurs imbriqués et éléments objets hôtes ne sont pas pris en charge. Pour les multi-maps, multi-ensembles, queues prioritaires et deques, utilisez l’API de valeurs du SDK ou un wrapper hôte.

L’enregistrement rejette les champs, propriétés et méthodes conteneurs statiques avec `VSCB3004`. Il rejette aussi les structs exposant des champs, propriétés ou méthodes d’instance publics hors constructeurs avec `VSCB3001`, même en lecture seule. Ces limites concernent ce générateur ; les métadonnées seules et le pont AOT sont vérifiés séparément. Le pont AOT rejette les méthodes d’instance publiques des structs, les champs d’instance modifiables et les setters publics ordinaires avec `VSCB3006`, car il ne peut préserver la réécriture du récepteur boxé ; voir [C# AOT](aot.md).

## Commande pour un fichier

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## Commande de projet et sorties

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

Le mode projet fusionne les déclarations partielles, attribue des références stables et prépare toutes les sorties choisies avant remplacement. Un échec de génération préserve les sorties ; un échec d’écriture entraîne une tentative de restauration. `--reference` est répétable. `--language-version` choisit la version syntaxique C# et vaut `latest` par défaut. `--api-reference` et `--api-reference-dir` sont exclusifs. Pour joindre les références API à `--manifest`, utilisez `--api-reference-dir` ; `--manifest` seul n’exige pas de références API.

## Limites d’exécution

Les callbacks peuvent s’exécuter simultanément sur plusieurs workers VM ; synchronisez l’état hôte partagé. Le pont AOT appelle directement les déclarations managées. Conservez ensemble le Catalog, le pont, les déclarations d’origine et le programme issus d’une même génération.

## Limites des conteneurs générés

Les métadonnées du Binding Catalog peuvent décrire les maps ordonnées. L’enregistrement live rejette `SortedDictionary<TKey, TValue>` avec `VSCB3002`, et le pont AOT généré avec `VSCB3003` ; utilisez un wrapper hôte explicite pour leur intégration exécutable. Le marshalling live crée des copies gérées : seuls les paramètres `ref`/`out` sont réécrits après un callback réussi. Les mutations des conteneurs passés par valeur ne modifient pas le conteneur script. Les ponts AOT transmettent directement les conteneurs gérés pris en charge et conservent leur identité.

## Chemins de sortie liés

Sous Windows, les alias de chemins `\\?\`, `\\.\` et GUID de volume sont comparés selon leur emplacement final dans le système de fichiers. Ces alias ne permettent pas de contourner les mêmes contrôles ; les sorties distinctes restent prises en charge.

Les CLI BindGen et Transpiler résolvent les liens existants de fichiers et de répertoires avant de préparer les sorties. Les liens symboliques ou jonctions Windows ne permettent ni d’écraser une source d’entrée ni de désigner deux fois la même sortie. Les sorties distinctes dans un répertoire lié restent prises en charge.

Les classes hôtes abstraites conservent leurs méthodes prises en charge et les instances renvoyées par des fabriques ; leurs constructeurs ne sont exportés ni dans le Catalog, ni dans l'enregistrement direct, ni dans le pont AOT. Les types imbriqués dépendant des paramètres génériques d'un type englobant sont rejetés avec `VSCB1001`, même s'ils sont sélectionnés par attribut.

Les paramètres AOT `ref`, `out`, `in` et `ref readonly` partagent le stockage d'origine des scalaires directement compatibles et des conteneurs pris en charge. Les références nécessitant une conversion de représentation pour les chaînes, caractères, enums ou objets hôtes sont rejetées avec `VSCB3007` ; générez le Catalog avec `EmitRegistrationSource=false` et `EmitAotBridgeSource=false` pour fournir un pont partial explicite.

## Collisions de noms de sortie

Les noms des références API BindGen sont comparés sans tenir compte de la casse sur chaque hôte. Les mêmes noms de base reçoivent un hachage stable du chemin, puis un suffixe numérique en cas de collision restante. L’allocation suit l’ordre des chemins ; réordonner la liste d’entrée ne change pas ces noms. Consultez les enregistrements retournés ou le manifeste pour localiser les fichiers `.api.vs.txt`.

<a id="binding-marker-attributes"></a>

Les attributs de sélection appartiennent aux déclarations d’entrée ; le SDK n’en injecte pas les définitions. Les options de bibliothèque utilisent par défaut `BindingAttributeName="VeroScriptBindingAttribute"` et `IgnoreAttributeName="VeroScriptIgnoreAttribute"`, avec possibilité de choisir des noms équivalents. L’enregistrement généré utilise `VeroScript.GeneratedBindings.VeroScriptCSharpBindings` ; `RegistrationNamespace` et `RegistrationClassName` le personnalisent. Ces noms n’activent pas une sortie désactivée.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/bindgen.md) · [fr-FR](bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
