# Liaisons hôtes et catalogues de liaisons

Les liaisons d'hôte exposent les fonctions, les types, les membres et les métadonnées d'un hôte C++ vers VeroScript. Un catalogue de liaison est une description JSON de cette interface publique. La CLI peut le valider et le formater, et charger un ou plusieurs catalogues lors de la vérification ou de l'exécution d'un script.

Un catalogue est un contrat d'interface. Il ne remplace pas le code d'enregistrement C++ et ne fournit pas d'implémentation d'exécution en soi. Une entrée dans un catalogue signifie que le compilateur peut voir l'entrée ; l'hôte doit toujours enregistrer l'implémentation correspondante avant l'exécution du script.

## Fichier et version du catalogue

L'objet de niveau supérieur contient les champs suivants. Les champs du tableau peuvent être omis lors de l'importation d'un catalogue ; L'export hôte les écrit tous. Si `version` est présent, il doit s’agir de l’entier `1`.

| Champ | Type | Descriptif |
| --- | --- | --- |
| `version` | Entier | Version au format catalogue-reliure ; actuellement `1`. |
| `functions` | Tableau | Descriptions des fonctions hôtes appelables à partir de scripts. |
| `genericTypeTemplates` | Tableau | Descriptions des modèles génériques de type hôte. |
| `types` | Tableau | Héberger des classes, des structures, des énumérations et leurs membres. |
| `typeAliases` | Tableau | Descriptions des alias de type d'hôte. |
| `annotations` | Tableau | Vocabulaire d'annotation pour la réflexion, les outils et la documentation. |

Un catalogue vide complet peut s’écrire comme suit :

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

Enregistrez le fichier sous `.json`. L'étiquette `json` sur le bloc de code est uniquement l'étiquette de mise en évidence de la syntaxe de la documentation ; cela ne fait pas du JSON un script `.vs`.

## Descriptions des fonctions

Chaque objet de `functions` nécessite au moins `name`, `returnType` et `paramTypes` :

| Champ | Type | Obligatoire | Descriptif |
| --- | --- | --- | --- |
| `name` | Chaîne | Oui | Nom visible pour les scripts. |
| `returnType` | Chaîne | Oui | Nom du type de script, tel que `int`, `string` ou `object`. |
| `paramTypes` | Tableau de chaînes | Oui | Types de paramètres dans le même ordre que l’enregistrement C++. |
| `documentation` | Chaîne | Non | Documentation des fonctions. |
| `paramNames` | Tableau de chaînes | Non | Noms des paramètres dans l’ordre des paramètres. |
| `paramDocumentation` | Tableau de chaînes | Non | Documentation des paramètres dans l'ordre des paramètres. |
| `returnDocumentation` | Chaîne | Non | Documentation sur la valeur de retour. |
| `annotations` | Tableau d'objets | Non | Instances d’annotation sur la fonction. |
| `paramAnnotations` | Tableau de tableaux d'objets | Non | Annotations des paramètres ; les positions extérieures correspondent à `paramTypes`. |
| `returnHostTypeName` | Chaîne | Non | Nom ou signature du type d'hôte pour un retour `object`, `enum` ou `function`. |
| `paramHostTypeNames` | Tableau de chaînes | Non | Noms de type d'hôte parallèles à `paramTypes`. |
| `returnElementTypeName` | Chaîne | Non | Type d'élément d'un retour de conteneur unaire. |
| `paramElementTypeNames` | Tableau de chaînes | Non | Types d’éléments des paramètres de conteneur unaire. |
| `returnKeyTypeName` | Chaîne | Non | Type des clés d’une map renvoyée. |
| `returnValueTypeName` | Chaîne | Non | Type des valeurs d’une map renvoyée. |
| `paramKeyTypeNames` | Tableau de chaînes | Non | Types des clés des paramètres de map. |
| `paramValueTypeNames` | Tableau de chaînes | Non | Types des valeurs des paramètres de map. |
| `paramFlags` | Tableau d'entiers | Non | Modes de passage de paramètres ; omis signifie que tous les paramètres sont transmis par valeur. |
| `typeParams` | Tableau de chaînes | Non | Paramètres de type d’une fonction hôte générique. |
| `scriptReturnTypeName` | Chaîne | Non | Type de retour utilisé côté script d’un appel générique. |
| `genericHostObjectCast` | Booléen | Non | Active la conversion générique d'objet hôte pour l'enregistrement générique correspondant. |

Chaque tableau parallèle utilise les mêmes positions de paramètres : `paramHostTypeNames[1]` et `paramFlags[1]` décrivent `paramTypes[1]`. Les longueurs ou positions incohérentes doivent être corrigées conformément au diagnostic du validateur CLI.

### Noms de type et métadonnées du conteneur

`returnType` et `paramTypes` utilisent les noms de types pris en charge par les liaisons d'hôte :

| Catégorie de type | Noms acceptés |
| --- | --- |
| Types primitifs et appelables | `void`, `bool`, `char`, `short`, `int`, `long`, `float`, `double`, `string`, `wstring`, `unsigned`, `unsigned int`, `unsigned char`, `unsigned short`, `unsigned long`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `enum`, `function`, `fn` |
| Objet hôte | `object`, `HostObject` |
| Conteneurs unaires | `array`, `DynamicArray`, `set`, `DynamicSet`, `multiset`, `MultiSet`, `ordered_multiset`, `OrderedMultiSet`, `queue`, `stack`, `deque`, `channel`, `priority_queue`, `PriorityQueue` |
| Cartes | `map`, `DynamicMap`, `ordered_map`, `OrderedMap`, `multimap`, `MultiMap`, `ordered_multimap`, `OrderedMultiMap` |

Les types d’éléments de conteneur, de clés et de valeurs sont stockés dans des champs de métadonnées distincts :

| Type | Champs de retour | Champs de paramètres |
| --- | --- | --- |
| `array`, ensembles, files d'attente, piles, deques, `channel` et files d'attente prioritaires | `returnElementTypeName` | `paramElementTypeNames` |
| `map`, `ordered_map`, `multimap` et `ordered_multimap` | `returnKeyTypeName`, `returnValueTypeName` | `paramKeyTypeNames`, `paramValueTypeNames` |
| `object`, `enum` et `function` | `returnHostTypeName` | `paramHostTypeNames` |

Par exemple, une fonction renvoyant un objet hôte `Widget` et acceptant `array<int>` et `map<string,double>` peut décrire ses types comme suit :

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### Indicateurs de paramètres

Chaque élément `paramFlags` est un masque de bits :

| Valeur | Signification |
| --- | --- |
| `0` | Passé par valeur. |
| `1` (`0x01`) | Référence non `const`. |
| `3` (`0x01 | 0x02`) | Référence `const`. |

Les autres combinaisons de bits ne sont pas valides ; le bit `const` n'est valide qu'avec le bit de référence. Les indicateurs doivent correspondre à la signature C++ enregistrée. Un catalogue ne peut pas transformer une fonction C++ par valeur en fonction de référence.

## Types d'hôtes et membres

Chaque objet de `types` nécessite `name` et `kind`. `kind` doit être `enum`, `struct` ou `class` :

| Champ | Type | Descriptif |
| --- | --- | --- |
| `name` | Chaîne | Nom visible pour les scripts. |
| `kind` | Chaîne | `enum`, `struct` ou `class`. |
| `documentation` | Chaîne | Documentation du type. |
| `annotations` | Tableau d'objets | Instances d'annotation sur le type. |
| `inheritable` | Booléen | Indique si le type permet la dérivation. |
| `scopedEnum` | Booléen | Indique si une énumération utilise une sémantique d'accès étendue. |
| `hasClone` | Booléen | Si le comportement de copie est fourni. |
| `hasEquals` | Booléen | Si une comparaison d'égalité est fournie. |
| `hasHash` | Booléen | Si le hachage est fourni. |
| `hasSubclass` | Booléen | Si la construction de sous-classes d'hôtes est prise en charge. |
| `genericTemplateName` | Chaîne | Modèle d'hôte générique à partir duquel ce type a été instancié. |
| `genericTypeArguments` | Tableau de chaînes | Arguments de type réels pour le modèle générique. |
| `members` | Tableau d'objets | Constructeurs, méthodes, propriétés, champs ou membres d'énumération. |

Chaque membre nécessite `kind` ; les membres ordinaires ont également besoin de `name`. Les types de membres pris en charge sont :

| `kind` | Signification |
| --- | --- |
| `constructor` | Constructeur; son nom peut être une chaîne vide. |
| `method` | Méthode d'instance. |
| `staticMethod` | Méthode statique. |
| `propertyGetter` | Acquéreur de propriété. |
| `propertySetter` | Setter de propriété. |
| `enumMember` | Membre d'énumération. |
| `field` | Champ d'instance. |
| `staticConstant` | Constante statique. |

Les membres acceptent les champs de documentation de fonction, d'annotation, de paramètre, de retour et de métadonnées de type. Ils prennent en charge en outre :

| Champ | Type | Descriptif |
| --- | --- | --- |
| `enumValue` | Entier | Valeur d'énumération ; il doit s'agir d'un entier JSON exact compris dans la plage de numéros JSON sécurisée. |
| `enumValueInt64` | Chaîne | Valeur enum 64 bits entièrement signée ; utilisez-le en dehors de la plage exacte de numéros JSON. |
| `operator` | Booléen | Si le membre est un opérateur. |
| `constMethod` | Booléen | Indique si la méthode est `const`. |
| `mutableThis` | Booléen | Indique si la méthode peut muter l'objet actuel. |
| `nativeName` | Chaîne | Nom d'hôte natif ; le nom visible dans le script est `name`. |

Les membres utilisent les mêmes significations pour `returnType`, `paramTypes` et `paramFlags` en tant que fonctions. Utilisez `enumMember` pour les entrées d'énumération. Pour une valeur en dehors de la plage entière exacte JSON, n’écrivez pas de numéro JSON approximatif dans `enumValue` ; écrivez plutôt sa représentation décimale dans `enumValueInt64`.

## Modèles génériques et alias de type

### `genericTypeTemplates`

Chaque modèle générique contient :

| Champ | Type | Obligatoire | Descriptif |
| --- | --- | --- | --- |
| `name` | Chaîne | Oui | Nom du modèle. |
| `arity` | Entier `1`–`255` | Oui | Nombre de paramètres de type. |
| `documentation` | Chaîne | Non | Documentation du modèle. |

### `typeAliases`

Chaque alias contient :

| Champ | Type | Obligatoire | Descriptif |
| --- | --- | --- | --- |
| `name` | Chaîne | Oui | Nom d’alias. |
| `typeParams` | Tableau de chaînes | Oui | Paramètres de type d'alias ; il est peut-être vide. |
| `targetTypeName` | Chaîne | Oui | Nom du type de cible. |
| `documentation` | Chaîne | Non | Documentation sur les alias. |

Par exemple:

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## Vocabulaire et instances d'annotation

Le tableau `annotations` de niveau supérieur est un vocabulaire d'annotation. Chaque entrée prend en charge :

| Champ | Type | Descriptif |
| --- | --- | --- |
| `name` | Chaîne | Nom de l'annotation. |
| `detail` | Chaîne | Brève description de la catégorie ou de la signature. |
| `documentation` | Chaîne | Documents d'annotations. |
| `arguments` | Tableau d'objets | Descriptions des arguments pour l’annotation. |
| `caseSensitive` | Booléen | Valeur par défaut : `true` ; `false` ignore la casse des lettres ASCII pour cette définition. |
| `targets` | Entier | Masque des cibles, `131071` par défaut (toutes les cibles prises en charge) ; valeurs de `1` à `131071`. |

Les noms conservent leur graphie source. Les inscriptions dont les graphies acceptées se recoupent sont rejetées. Les noms des arguments et les clés de métadonnées restent sensibles à la casse. Chaque description peut contenir un tableau récursif `children` de même structure, décrivant des arguments imbriqués et non des valeurs exécutables. Utilisez `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)` pour une inscription native. Le catalogue n’installe pas les rappels des processeurs.

Chaque argument de vocabulaire prend en charge `name`, `detail` et `documentation`. Un vocabulaire documente les noms d'annotations disponibles ; il n'applique pas automatiquement une annotation à une fonction ou à un type.

Le champ `annotations` sur une fonction, un type ou un membre contient des instances d'annotation :

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

Le champ `value` peut être omis. L'omission signifie un argument indicateur sans valeur ; `"value": ""` signifie que l'argument a explicitement une valeur de chaîne vide. Ces significations sont distinctes. `paramAnnotations` est un tableau bidimensionnel d'instances d'annotation de paramètres et ses positions extérieures doivent correspondre à `paramTypes`.

Téléchargez l’[exemple de catalogue de bindings](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json) depuis la documentation Resources intégrée, puis enregistrez une copie nommée `bindings.sample.json` dans un dossier de travail inscriptible. Il illustre les métadonnées d’annotations personnalisées et les types hôtes ; ne supposez pas qu’une archive SDK fonctionnelle le contient. Exécutez les commandes suivantes depuis ce dossier et remplacez `app.vs` par votre script.

## Validation, formatage et chargement

La CLI fournit des commandes de maintenance du catalogue :

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

Sans `-o` ou `--output`, `bindings format` réécrit le catalogue d’entrée. Avec un chemin de sortie, il écrit ce fichier à la place. La sortie standard contient une confirmation d’écriture, pas le JSON formaté ; lisez le fichier de sortie pour obtenir le résultat.

`validate` vérifie les types JSON, les champs obligatoires, les valeurs d'énumération prises en charge, les tableaux parallèles et les plages numériques. `format` émet JSON dans le format stable de la chaîne d'outils. Lors du formatage vers un nouveau fichier, utilisez un chemin temporaire et examinez le résultat avant de remplacer l'original.

Chargez un ou plusieurs catalogues lors de la vérification ou de l'exécution d'un script avec l'option répétable `--binding-catalog` :

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

Un catalogue importé offre une visibilité et des métadonnées au moment de la compilation. Les descriptions de fonctions et de membres importées ne remplacent pas les rappels d'exécution ; si l'hôte n'a pas enregistré l'implémentation correspondante, le script peut toujours échouer au moment de l'exécution.

L'importateur applique également des limites de ressources : taille de fichier maximale `128 MiB`, valeurs JSON `4,194,304` et profondeur d'imbrication `256`. Ces limites empêchent un catalogue de consommer des ressources excessives lors de la validation.

## Enregistrement des liaisons en C++

Un hôte intégré utilise normalement `VeroScriptSession` pour enregistrer les liaisons. Le périmètre d’enregistrement public comprend :

- `RegisterNative` pour les fonctions natives simples ou les fonctions structurées avec métadonnées de paramètres.
- `RegisterHostType` pour les classes hôtes, les structures et les énumérations.
- `RegisterHostMember` pour les constructeurs, les méthodes, les membres statiques, les propriétés et les champs.
- `RegisterHostOperator` pour les opérateurs sur les types d'hôtes.
- `RegisterHostTypeClone` pour le comportement de copie de type hôte.
- `SetHostTypeInheritable` et `SetHostEnumScoped` pour la sémantique des types et des énumérations.
- `RegisterHostTypeSubclassFactory` pour construire des instances de types d'hôtes dérivables.
- `RegisterGenericHostTypeTemplate` et `InstantiateGenericHostType` pour les modèles génériques de type hôte.
- `GetBindingCatalog` pour la lecture du catalogue exporté par la session en cours.

Après l'enregistrement de l'hôte, exportez le catalogue et exécutez le validateur CLI afin que la documentation, la vérification statique et l'enregistrement du runtime restent alignés. Les membres hôtes statiques sont normalement accessibles sous le nom `Type.staticMethod(...)` ; les constantes hôtes utilisent `Type::Constant`. Pour le cycle de vie plus large du moteur et l'utilisation de `CheckSource`/`RunSource` ou `CheckFile`/`RunFile`, voir [Intégration de l'hôte](embedding.md).

## Arguments d'objet hôte nullables

Un membre hôte statique dont le paramètre explicite est un `HostObject` peut
recevoir `nullptr` du script. Le callback reçoit un handle d'objet hôte vide et
doit le vérifier avant de déréférencer la charge. Un récepteur d'instance null
reste invalide ; cette règle concerne l'argument explicite de l'appel statique.

## Principes de compatibilité

Un catalogue contraignant fait partie de l'interface publique. L'ajout d'une fonction ou d'un membre est normalement une extension compatible ; la suppression d'une entrée, la modification d'un type de paramètre ou d'un indicateur de référence, la modification d'une valeur d'énumération, la modification d'un nom de type d'hôte ou la modification de la visibilité d'un membre peuvent interrompre les scripts. Lors de la mise à niveau d'un catalogue ou de liaisons d'hôte, exécutez ensemble la validation du catalogue, les vérifications de script et les tests d'intégration du projet cible.

## Surcharges hôte

<a id="host-overload-resolution"></a>

Les méthodes hôte ordinaires, statiques ou d’instance, et les fonctions natives libres acceptent les surcharges selon les types de paramètres normalisés et leurs qualifications de référence. Les signatures de paramètres identiques et les différences limitées au retour sont refusées. La sélection utilise les types des arguments, les conversions numériques et le repli variant ; les candidats de même rang produisent un diagnostic d’ambiguïté. Les membres nommés ont une identité native stable dérivée de leur signature. Les protocoles existants des constructeurs et opérateurs restent valables. `FindFunction` par nom seul renvoie null pour un ensemble de surcharges ; `GetFunctions()` énumère les déclarations et leurs documentations distinctes. Le catalogue conserve toutes les surcharges lors des échanges. Les inscriptions existantes à signature unique utilisent les mêmes API publiques.

Sans `ref`, le comportement existant des entrées natives par valeur ou handle est conservé ; remplacer le stockage appelant demande un `ref` explicite. Une surcharge par valeur est prioritaire pour un argument ordinaire compatible. Les références explicites vérifient le type du stockage et son accès en écriture ; `const ref` accepte les variables const. Les extensions de compilation conservent toutes les surcharges. VM, Mixed, JIT et Core AOT partagent la sélection du cœur. L’export C++ autonome garde son contrat de pont en liaison C et signale les surcharges hôte nommées non prises en charge en orientant vers Core AOT.

## Métadonnées des rappels natifs

IScriptCallInvoker::GetActiveModule() expose des métadonnées sémantiques empruntées en lecture seule pendant le rappel natif. Un hôte peut renvoyer nullptr si elles sont indisponibles. Ne conservez ni ce pointeur, ni l’invocateur emprunté, ni des références aux métadonnées après le rappel ; conserver une valeur ou le propriétaire du programme ne prolonge pas cet emprunt.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/bindings.md) · [fr-FR](bindings.md) · [ja-JP](../ja-JP/bindings.md) · [ko-KR](../ko-KR/bindings.md) · [zh-CN](../zh-CN/bindings.md) · [zh-TW](../zh-TW/bindings.md)
