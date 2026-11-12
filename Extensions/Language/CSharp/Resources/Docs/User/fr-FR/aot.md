# AOT C#

`veroscript aot --target=csharp` convertit un programme VeroScript vérifié en
source C#. L'application générée peut être compilée normalement ou publiée
avec NativeAOT .NET. Elle ne charge pas `VeroScriptInterop` à l'exécution et
est distincte du SDK d'intégration `VeroScriptCSharp`.

## Fichier unique

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

Ajoutez `-I`/`--import-path` pour les modules de l'application situés dans un
autre répertoire. Le résultat est une source C#, pas un exécutable ; ajoutez-la
à un projet SDK ciblant .NET 8 ou .NET 9. Le fichier n'est remplacé qu'après
une génération réussie.

## Bundle source

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

Le bundle contient `App.runtime.g.cs`, `App.program.g.cs`, `App.entry.g.cs`,
`App.csproj` et `App.manifest.json`. Le manifeste fixe la liste et l'ordre des
unités générées. Les fichiers sont des unités `partial` ordinaires.

<a id="aot-project-inputs"></a>

### Entrées hôtes du projet généré

Le `.csproj` généré utilise `EnableDefaultCompileItems=false` : seuls les éléments `Compile` explicites sont compilés. Ajoutez les déclarations hôtes gérées originales et le bridge AOT ; placer ces fichiers à côté du projet ne suffit pas. L’implémentation de classe partielle du bridge doit être compilée dans le même assembly que le programme généré. Conservez toutes les unités indiquées par le manifeste. L’enregistrement VM actif est un artefact SDK distinct.

La régénération remplace le fichier projet. Réappliquez les éléments `Compile` supplémentaires ou référencez unités générées et entrées hôtes depuis un projet appartenant à l’application. Le [tutoriel de liaison](../../../../../../../Resources/Docs/Tutorials/fr-FR/csharp/01-csharp-source-tools.md) donne les chemins et l’ordre des opérations.

## Limites volontaires

La cible prend en charge les scalaires, chaînes, `pair`, `auto`, conteneurs,
fonctions, génériques ouverts non contraints, valeurs par défaut, variadiques,
surcharges, paramètres `ref`, espaces de noms, `using namespace`, enums,
globals, imports, le contrôle structuré, `switch`, les sauts, `throw` et
`try`/`catch`/`finally`, ainsi que `algorithm`, `numeric` et `random`.
Elle prend aussi en charge les valeurs de fonction et appels indirects, les
lambdas expression/bloc et captures, les classes et structs VeroScript,
constructeurs, membres d'instance/statiques/virtuels, héritage simple,
retours par référence et alias de référence locaux.

Cette surface comprend les littéraux `char` signés sur 8 bits, les groupes de
déclarations locales de même type, `do`/`while` et les listes d’expressions
d’initialisation ou d’itération des boucles `for` classiques. Les chaînes
ordinaires et brutes préservent leur séquence exacte d’octets VeroScript, y
compris les octets isolés introduits par `\xNN` ; les scalaires Unicode de saut
de ligne sont émis sous forme d’échappements afin de ne pas invalider les
littéraux C# générés ni les commentaires de position source.

Les paramètres de type génériques, ainsi que les locaux, champs, liaisons de
boucle/catch et paramètres lambda qui se masquent lexicalement, reçoivent des
noms C# hygiéniques. Un initialiseur est émis dans l’environnement VeroScript
précédent avant que sa nouvelle liaison devienne visible, même si C# le lierait
autrement à lui-même ou refuserait l’imbrication.

Dans une liste d'initialisation de champs d'un type générique ouvert, la cible
C# ne prend en charge que l'initialisation directe de `T` vers `T`. La
construction d'un paramètre de type non contraint avec zéro ou plusieurs
arguments est refusée à l'émission AOT ; utilisez un type de champ concret ou
construisez la valeur avant de la transmettre comme `T`.

Lorsqu'une lambda utilise un membre d'instance, un `this` de classe capturé
conserve le récepteur et observe ses mutations ultérieures, tandis qu'un `this`
de structure est un instantané de valeur. Les lambdas imbriquées conservent le
récepteur déjà capturé. Une lambda sans capture ne peut pas utiliser `this`.

Les classes, structs, enums, membres et conteneurs compatibles d'un Binding
Catalog v1 peuvent être abaissés par `--binding-catalog` et le pont AOT généré,
avec des appels managés directs et sans réflexion runtime.

## Exclusions explicites

Interfaces et implémentation d'interfaces, héritage multiple concret,
héritage non public ou de struct, destructeurs déterministes, déclarations
friend/using-member, paramètres génériques contraints ou par défaut,
surcharges ne différant que par const ou par direction de référence,
corps précompilés, décomposition de maps, `foreach` par référence, tableaux
natifs fixes, opérateurs/indexeurs hôte, membres hôte génériques, incrémentation
de propriété hôte et conteneurs hôte nécessitant une conversion d'élément sont
refusés. Les diagnostics sont émis au lieu de modifier silencieusement le
contrat.

`concurrent.vs`, `channel<T>`, `task<T>` et `task_group<T>` ne sont pas encore
abaissés par C# AOT. Utilisez le [SDK C#](index.md) avec la VM native pour la
concurrence runtime, les bindings hôte, la réflexion, l'interception ou les
diagnostics JIT.

La surface des conteneurs comprend aussi `MultiMap`, `OrderedMultiMap`,
`MultiSet`, `OrderedMultiSet` et `PriorityQueue`, en plus des maps, ensembles,
queues, stacks et deques ordinaires.

Les conversions des nombres flottants en entiers du C# AOT tronquent vers zéro et contrôlent la valeur binaire par rapport à la plage cible avant la conversion. Elles préservent les valeurs entières représentables sur 64 bits sans arrondi décimal. Les cibles non signées rejettent les entrées négatives, y compris les fractions négatives ; NaN, les infinis et les valeurs hors plage échouent.

<a id="aot-floating-conversions"></a>

Le C# AOT conserve la conversion du cœur via double des entiers vers float, y compris dans les opérations mixtes et les affectations composées. Les comparaisons flottantes mixtes utilisent des opérandes double ; le reste flottant signale l’erreur du cœur indiquant que le modulo est défini uniquement pour les entiers.

## Mémoire du runtime

L’AOT autonome ne prend actuellement pas en charge les requêtes, buffers, vues ou types de `std::memory`. Les appels ou l’utilisation de ces types produisent une erreur explicite de génération ; importer `memory.vs` ou `core.vs` sans utiliser ces fonctionnalités reste pris en charge.

[Runtime SDK](index.md)

## Préparation des déclarations et limites des captures

La génération C# utilise la préparation des déclarations de Core, notamment les définitions hors classe et les méthodes générées. Les hôtes natifs peuvent fournir des processeurs d’annotations via `FCodegenOptions.AnnotationProcessing` avec `FCSharpCodegenBackend` ; leur code doit rester valide pendant la génération.

Les liaisons capturées par valeur sont en lecture seule par défaut. Une lambda `mutable` conserve les modifications de ses propres captures entre les appels ; les copies de la valeur fonction partagent cet environnement, tandis que des évaluations distinctes créent des environnements indépendants. Les captures avec initialisation par valeur sont évaluées une fois, de gauche à droite, dans la portée englobante. La capture ordinaire d’une liaison const reste en lecture seule. Les variables locales `static` scalaires sont initialisées à la première utilisation et partagées par déclaration, y compris entre fermetures issues de la même expression lambda ; les instanciations génériques et les invocations du point d’entrée généré ont des états distincts. Une initialisation échouée peut être retentée ; une initialisation récursive échoue. Les captures avec initialisation par référence, les static locaux non scalaires et l’accès d’une lambda imbriquée au static local d’une fonction englobante nécessitent le runtime Core et sont rejetés par la génération C# autonome.

C# AOT autonome prend en charge les requêtes de capacités, les dimensions, les couleurs, le curseur et l’effacement de console. Une sortie redirigée ne fournit aucun contrôle ni séquence d’échappement. La fin de l’entrée restaure les attributs et le curseur Windows d’origine ; les autres terminaux réinitialisent le style et affichent le curseur après modification, même si le script échoue.

C# AOT partage un type de délégué généré pour les alias primitifs équivalents dans les signatures, y compris les paramètres/retours par référence et les conteneurs de valeurs imbriqués. `long`/`int64` et `unsigned long`/`uint64` restent équivalents ; le signe, la largeur, l’identité nominale et les qualificatifs `ref`/`const ref` restent distincts.

## Modèles pair utilisateur

Les types `pair<T, U>` définis par l’utilisateur emploient les champs et méthodes génériques ordinaires. Si ce modèle à deux paramètres remplace le type intégré souple, l’AOT autonome refuse explicitement `entries()` et l’itération des paires clé-valeur des maps ; utilisez le backend VM pour ces opérations. L’itération des valeurs d’une map dont les éléments sont des pair utilisateur reste prise en charge.

Les entrées de génération de fichier avec options (`FCSharpCodegenBackend.EmitFromFile` et `EmitCSharpSourceBundleFromFile`) chargent le module principal et ses imports via le chargeur Core. Les environnements immuables et `SourceOverlays` s'appliquent aussi aux chemins d'entrée virtuels. La sortie autonome rejette les contrats de paquets natifs qui nécessitent un adaptateur du runtime cible.

La surcharge de `EmitCSharpSourceBundleFromProgram` avec options utilise aussi le catalogue de liaisons publié par l'environnement de modules et rejette tout catalogue explicite en conflit.

Le point d’entrée généré initialise les membres statiques et les variables globales dans l’ordre enregistré par le compilateur, avant `main` et dans la limite de gestion des erreurs d’exécution. Les exceptions et `exit` pendant l’initialisation suivent les mêmes règles que l’exécution du point d’entrée. Lire ou référencer un stockage autre que `int` avant son initialisation nécessite le runtime Core : sa valeur zéro brute ne peut pas être représentée fidèlement par un champ C# typé. La vérification suit les appels statiquement connus et les liaisons directes de valeurs appelables. Une cible d’appel indirect non résolue au démarrage est rejetée tant qu’un tel stockage reste non initialisé ; la simple création d’une valeur appelable différée reste prise en charge. Les écritures suivies d’une lecture dans un flux linéaire et les lectures anticipées du zéro ordinaire de `int` restent prises en charge.

Après une compilation Core réussie, les graphes d’appels et d’initialisations de
types trop profondément imbriqués au démarrage sont rejetés avec un diagnostic demandant le runtime Core. Cette limite
protège la pile native d’inspection du générateur ; la récursion ordinaire et les
appels de démarrage séquentiels restent pris en charge.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/aot.md) · [fr-FR](aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
