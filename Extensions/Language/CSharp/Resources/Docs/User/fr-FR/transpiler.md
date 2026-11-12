# Transpileur C#

`VeroScriptCSharpTranspiler` convertit un sous-ensemble C# 12 strict et
statiquement analysable en source VeroScript. Il utilise les informations
sémantiques de Roslyn avec `VeroScriptCSharpFrontend` et n'effectue pas de
remplacement textuel de tokens. Il s'agit d'un outil de conversion de source,
distinct du runtime C#, de BindGen et du backend AOT VeroScript vers C#.

## Conversion d'un fichier

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript` désigne l'exécutable VeroScript ou la racine du SDK utilisée
pour valider la source générée. La validation est activée par défaut. Le
fichier VeroScript et sa carte source ne sont écrits qu'après l'analyse, la
conversion, la validation et la préparation de sortie ; une conversion
échouée ne remplace pas une sortie existante.

L'API publique fournit `CSharpTranspiler.TranspileSource` et
`CSharpTranspiler.TranspileFile`. Le résultat contient la source générée, le
JSON de carte source et des valeurs `CSharpDiagnostic`. Les options
`CSharpTranspileOptions` contrôlent la validation, le validateur, le chemin
généré, le délai maximal et les options d'analyse C#.

## Conversion d'un projet

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

Le mode projet analyse toutes les entrées comme une seule compilation C#,
résout les symboles entre fichiers, déduit les imports explicites et conserve
les chemins relatifs à `--project-root`. Il écrit un fichier VeroScript et une
carte source `.map.json` par entrée. Le manifeste par défaut est
`transpile.manifest.json` ; `--manifest` permet de choisir un autre chemin.
Les fichiers générés et le manifeste sont validés puis engagés comme un seul
ensemble ; un échec de préparation déclenche un retour arrière.

L'API de projet est `CSharpTranspiler.TranspileProject`, avec
`CSharpTranspileProjectOptions`. Le résultat contient les fichiers générés,
le JSON du manifeste déterministe et les diagnostics. Chaque entrée doit être
dans la racine du projet et ne peut apparaître qu'une seule fois.

Si deux entrées produisent le même nom de sortie relatif, le nom en conflit
reçoit un suffixe de hachage de chemin déterministe.

## Options de ligne de commande

| Option | Portée et rôle |
| --- | --- |
| `-o`, `--output` | Chemin de sortie d'un fichier. Obligatoire en mode fichier et interdit en mode projet. |
| `-h`, `--help` | Affiche l'utilisation et termine la commande. |
| `--version` | Affiche la version du transpileur et termine la commande. |
| `--source-map` | Chemin de la carte source d'un fichier. Par défaut `<output>.map.json` ; interdit en mode projet. |
| `--output-dir` | Active le mode projet et choisit le répertoire de sortie. |
| `--manifest` | Chemin du manifeste projet. Par défaut `transpile.manifest.json` dans le répertoire de sortie. |
| `--project-root` | Racine utilisée pour calculer les noms relatifs des sorties. Chaque entrée doit y être située. |
| `--reference` | Ajoute une assembly de métadonnées ; répétez l'option pour plusieurs assemblies. |
| `--language-version` | Choisit la version C# utilisée pour l'analyse. |
| `--veroscript` | Désigne l'exécutable VeroScript ou la racine du SDK servant de validateur. |
| `--timeout` | Définit le délai de validation en secondes ; la valeur par défaut est 120. |
| `--no-validate` | Désactive la validation du compilateur ; seulement si une autre étape explicite valide la source générée. |

Le mode projet est sélectionné lorsque plusieurs entrées sont fournies ou que
`--output-dir` ou `--manifest` est présent. Il exige toujours `--output-dir`.

## Métadonnées de sortie

Une carte source enregistre les chemins source et généré ainsi qu'un tableau de
correspondances. Chaque correspondance indique la ligne générée et le chemin,
la ligne et la colonne sources correspondants :

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

Le manifeste de projet utilise les champs stables suivants :

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

Les chemins du manifeste sont relatifs au projet et utilisent `/` comme
séparateur. Conservez le manifeste, les sources générées et les cartes source
de la même génération ensemble.

## Surface C# prise en charge

Le sous-ensemble prend notamment en charge :

- namespaces, enums, classes, structs et classes statiques utilisées comme
  déclarations au niveau du namespace ;
- champs publics, protégés et privés, propriétés automatiques symétriques,
  constructeurs, membres statiques, membres d'instance et méthodes ;
- types génériques et fonctions libres sans contraintes, types primitifs et
  conteneurs VeroScript standard ;
- inférence locale, appels, création d'objets et de collections, indexation,
  conversions, arithmétique, affectations, conditions, boucles, y compris
  `do`/`while` et les boucles `for` classiques avec plusieurs déclarations ou
  expressions d’itération, `foreach` et `switch` ;
- `throw`, `try`, `catch` et `finally` ordinaires ;
- alias de types, variables locales constantes, `params`, types délégués ou
  fonctionnels et lambdas expression ou bloc ;
- méthodes retournant une référence, variables locales de référence,
  étiquettes et `goto`, abaissés vers les formes VeroScript correspondantes.

`Console.Write` et `Console.WriteLine` conservent leur comportement distinct
concernant le retour à la ligne. Le comportement des fermetures C# par
référence est également conservé pour les lambdas prises en charge.
Les littéraux de chaîne conservent NUL et les autres caractères de contrôle pris
en charge. Les substituts UTF-16 non appariés échouent de manière sûre, car
VeroScript ne possède pas de représentation UTF-8 sans perte pour eux.

Les appels de membres génériques explicites conservent leurs arguments de
type lorsqu'ils sont représentables, par exemple `Helpers.Pick<long>(1)`
devient `Helpers::Pick<int64>(1)`. Les déclarations de types imbriqués sont
refusées avec un diagnostic plutôt que d'être émises sous une forme ambiguë ;
placez le type à la portée du namespace pour le transpiler.

## Limites explicites

Les fonctionnalités qui ne peuvent pas être représentées fidèlement sont
refusées avec le diagnostic `VSCT2001` au lieu d'être approximées silencieusement. Les
limites actuelles comprennent :

- `async`/`await`, tâches, itérateurs et syntaxe de requête LINQ ;
- déclarations de délégués et événements ;
- sémantique des références nullables, code unsafe ou pointeurs, réflexion et
  `dynamic` ;
- contraintes génériques et interfaces ;
- filtres de `catch` ;
- propriétés asymétriques ou calculées ;
- chaînage des constructeurs ;
- types partiels répartis sur plusieurs fichiers.

`--reference` est répétable pour les assemblies de métadonnées et `--language-version` choisit la version syntaxique C#. Ces options n’élargissent pas la sémantique VeroScript prise en charge. `--timeout` limite la validation de la source générée. Utilisez `--no-validate` seulement lorsqu’une autre étape explicite effectue la validation du compilateur ; une génération réussie ne prouve pas que la sortie est exécutable.

## Abaissement des entiers et limites numériques

Le Transpiler conserve le débordement circulaire C# par défaut des additions, soustractions, multiplications, négations, incréments, affectations composées et conversions entières, selon la largeur résolue. Le nombre de positions de décalage est masqué par 31 ou 63. L’affectation composée évalue une seule fois la cible et son ancienne valeur avant l’opérande droit ; les propriétés automatiques deviennent des champs. La division et le reste entiers lèvent des exceptions de script interceptables pour un diviseur nul ou un débordement minimum signé/-1. Les constantes `double` de valeur entière gardent leur type flottant.

<a id="numeric-and-console-boundaries"></a>

Les contextes explicites `checked`/`unchecked`, la division ou le reste flottants non constants, les conversions non constantes de flottant en entier et les constantes flottantes non finies échouent avec `VSCT2001`, même sans validation. Roslyn évalue les constantes numériques finies à la compilation. L’analyse utilise le réglage unchecked par défaut et n’importe pas l’option de vérification de débordement d’un projet consommateur.

Les conversions entières d’enum conservent les bits selon la largeur sous-jacente déclarée. Les enums non signés sur 64 bits, les mutations arithmétiques/bit à bit d’enum, la réduction non constante de `double` en `float` et la conversion non constante d’un entier sur 64 bits en `float` échouent avec `VSCT2001`. Console accepte un argument chaîne, entier ou booléen, ainsi que `Console.WriteLine()` sans argument. `Console.Write` utilise `std::console::write` sans saut de ligne ; `Console.WriteLine` utilise `std::print` avec saut de ligne. Le texte booléen est `True`/`False`, également dans les concaténations et `+=`. Les fonctions standard qualifiées empêchent une fonction source homonyme d’intercepter la sortie ou la conversion textuelle entière. Les surcharges Console formatées/à plusieurs arguments et le formatage de flottants, enums, objets ou tableaux sont rejetés au lieu de modifier le texte du CLR.

Le transpileur C# applique les promotions des opérandes de Roslyn avant les comparaisons mixtes entre entiers et flottants, préservant ainsi l’arrondi float de C# avant la comparaison du cœur.

Les branches conditionnelles numériques appliquent les conversions implicites choisies par Roslyn, préservant les largeurs entières et l’arrondi float ; seule la branche sélectionnée est évaluée. Les conversions non constantes d’entiers de 64 bits vers float sont également rejetées dans les branches conditionnelles.

## Chemins de sortie liés

Sous Windows, les alias de chemins `\\?\`, `\\.\` et GUID de volume sont comparés selon leur emplacement final dans le système de fichiers. Ces alias ne permettent pas de contourner les mêmes contrôles ; les sorties distinctes restent prises en charge.

Les CLI BindGen et Transpiler résolvent les liens existants de fichiers et de répertoires avant de préparer les sorties. Les liens symboliques ou jonctions Windows ne permettent ni d’écraser une source d’entrée ni de désigner deux fois la même sortie. Les sorties distinctes dans un répertoire lié restent prises en charge.

`--timeout` et `ValidationTimeout` couvrent, pour chaque invocation, la fin du validateur et la lecture des deux flux de sortie. Si un lanceur se termine alors qu’un processus enfant conserve un canal redirigé ouvert, la validation échoue avec `VSCT2004` à l’expiration du délai. Une validation échouée ne remplace pas les fichiers de sortie CLI.

Les champs statiques et les propriétés automatiques acceptent une initialisation omise ou constante à la compilation. Les initialisations statiques non constantes et les constructeurs statiques échouent avec `VSCT2001`, même sans validation, car l'initialisation globale VeroScript ne préserve pas le moment d'initialisation des types CLR, les effets de bord ni l'ordre des dépendances. Initialisez explicitement l'état statique dynamique dans `Main` ou une autre méthode contrôlée par l'application.

Les champs et propriétés automatiques de type chaîne, tableau ou collection mappée exigent une initialisation explicite non nulle. Les collections mappées comprennent `List<T>`, `Dictionary<TKey, TValue>`, `SortedDictionary<TKey, TValue>`, `HashSet<T>`, `Queue<T>` et `Stack<T>`. L'absence d'initialisation et l'affectation de null à ces types échouent avec `VSCT2001`, même sans validation : remplacer une référence CLR nulle par une chaîne ou un conteneur vide modifie l'égalité et les exceptions. Les comparaisons d'égalité et d'inégalité des tableaux et collections mappées non nuls pris en charge avec null restent acceptées ; les comparaisons de chaînes avec null sont hors du sous-ensemble.

## Collisions de noms de sortie

Les noms de sortie du projet Transpiler sont comparés sans tenir compte de la casse et après normalisation Unicode NFC sur chaque hôte. L’allocation suit l’ordre des chemins. Une collision reçoit un suffixe de hachage de chemin déterministe, puis un suffixe numérique si ce nom est déjà occupé. Consultez les enregistrements retournés ou le manifeste pour connaître les noms réels.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/transpiler.md) · [fr-FR](transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
