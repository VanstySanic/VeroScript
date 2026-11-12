# AOT C++

Le backend AOT traduit VeroScript en source C++17 pour une construction native.
Il convient aux déploiements qui n’ont pas besoin de charger dynamiquement les
scripts à l’exécution.

## Génération en ligne de commande

```powershell
veroscript aot script.vs -o generated.cpp
```

La sortie est une unité de traduction C++, pas un exécutable lié. Elle contient
le code de compatibilité requis. Compilez et liez ce code avec une chaîne C++17
compatible et les bibliothèques système nécessaires. Les bindings hôtes
nécessitent aussi les implémentations hôtes correspondantes.

## API programmée

`VeroScriptCppAot` fournit :

- `EmitCppFromSource` et `EmitCppFromFile` pour une source ou un fichier ;
- `EmitCppFromProgram` pour un `FProgram` déjà compilé ;
- `EmitCppSourceBundleFromProgram` pour produire un `FCppAotSourceBundle` ;
- `BuildCppSourceBundle` pour reconstruire un bundle depuis une source générée ;
- `FCppCodegenBackend`, nommé `cpp`, avec l’extension `.cpp`.

Les noms de bundle acceptent les lettres ASCII, les chiffres, `_` et `-`. Les noms de périphériques Windows (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) sont rejetés sans distinction de casse sur toutes les plateformes pour préserver la portabilité des fichiers. `CON_model` et `COM10` restent valides.

Les API source acceptent les chemins d’import, les définitions, les types hôte
connus, les imports implicites et les politiques d’import circulaire et
d’accès au projet. L’écriture d’un fichier est transactionnelle et la source
et la sortie doivent être différentes.

Un environnement sans catalogue de liaisons peut utiliser les liaisons hôtes explicites de `FCodegenOptions::Bindings`. La préparation et la vérification emploient le même catalogue ; le projet cible doit implémenter les fonctions libres déclarées dans le code généré. L’AOT autonome refuse toujours les programmes exigeant des contrats de paquets natifs à l’exécution.

## Bundles et limites

Un `FCppAotSourceBundle` contient `Files` et `ManifestJson`. Son fichier `.cpp`
d’entrée reconstruit une seule unité de traduction à partir de fragments
`.generated.inl`; le manifeste indique le standard C++ minimal et la
bibliothèque de threads requise. Les fragments ne sont pas des unités de
traduction indépendamment liables.

Les variables globales de script et les champs statiques de classe conservent
leur isolation par travailleur natif. Le stockage AOT passe par des accesseurs
avec un état `thread_local` local à la fonction. Au premier accès d'un
travailleur, les globaux sont initialisés dans l'ordre des déclarations du
script ; le point d'entrée généré établit cet état avant d'appeler `main`.
Utilisez les canaux ou l'état hôte synchronisé pour partager explicitement des
données entre travailleurs.

Lorsqu'une lambda de membre utilise le récepteur, le C++ généré préserve la
sémantique du langage : un récepteur de classe est conservé comme objet partagé
et observe les mutations ultérieures, tandis qu'un récepteur de structure est
capturé comme un instantané de valeur. Les lambdas imbriquées conservent ce
récepteur. Une lambda sans capture ne peut pas utiliser `this`, et une lambda
qui capture le récepteur de classe depuis un destructeur est refusée, car le
récepteur entre déjà en destruction.

L'émetteur peut utiliser un stockage automatique pour une variable locale de
classe lorsque l'analyse prouve que l'objet ne sort pas de sa portée lexicale.
Un objet renvoyé, une référence vers un champ ou une fermeture qui s'échappe
conserve au contraire la propriété partagée. Il s'agit d'une optimisation du
stockage généré ; elle ne modifie ni l'identité de classe, ni les destructeurs,
ni les règles de durée de vie du langage.

Le backend couvre les classes, structures, interfaces, héritage, dispatch
virtuel, génériques, contraintes d’interface, conteneurs, surcharges,
paramètres par défaut et variadiques, références, lambdas, énumérations,
opérateurs, `nullptr`, `switch`, `foreach`, labels, sauts et bibliothèques
documentées de chaînes, mathématiques, fichiers et chemins.

Les opérateurs numériques scalaires `++` et `--` évaluent leur cible une seule fois, conservent les propriétaires temporaires pendant l'opération et respectent les promotions, conversions contrôlées et débordements de la VM. Les surcharges de `++` et `--` sur les types utilisateur ne sont pas prises en charge ; utilisez une méthode de mise à jour explicite.

Dans une liste d'initialisation de champs d'un type générique ouvert, la cible
C++ ne prend en charge que l'initialisation directe de `T` vers `T`. La
construction d'un paramètre de type non contraint avec zéro ou plusieurs
arguments est refusée à l'émission AOT ; utilisez un type de champ concret ou
construisez la valeur avant de la transmettre comme `T`.

Le VM reste préférable pour le chargement dynamique, le débogage et le
rechargement à chaud. L’AOT utilise des threads hôte pour la concurrence ;
validez les bindings, la durée de vie des callbacks et la synchronisation dans
le projet cible.

## Couverture syntaxique actuelle

L’AOT C++ émet aussi les littéraux `char` signés 8 bits, les groupes de
déclarations locales de même type, `do`/`while` et les listes d’initialisation
ou d’itération des `for` classiques. Il préserve le masquage lexical : chaque
initialiseur de déclaration est évalué dans l’environnement VeroScript qui le
précède avant que la nouvelle liaison ne soit visible. Le C++ généré peut
utiliser des temporaires d’hygiène ou des noms locaux distincts pour conserver
cette règle ; ces noms d’implémentation ne font pas partie de l’ABI AOT ni
d’un changement de comportement du source.

## Mémoire du runtime

L’AOT autonome ne prend actuellement pas en charge les requêtes, buffers, vues ou types de `std::memory`. Les appels ou l’utilisation de ces types produisent une erreur explicite de génération ; importer `memory.vs` ou `core.vs` sans utiliser ces fonctionnalités reste pris en charge.

[Runtime SDK](index.md)

## Retours par référence et durée de vie

<a id="reference-return-lifetime"></a>

Lorsqu’un appel renvoyant une référence est lu comme une valeur, AOT copie cette valeur avant de libérer le récepteur conservé, y compris pour les appels sans argument. La liaison des références reste distincte de la conversion des valeurs et de la déduction générique. Les références numériques suivent les mêmes promotions et contrôles de réduction et de dépassement que les valeurs ordinaires. Une affectation via une méthode renvoyant une référence évalue le récepteur et les arguments, appelle la méthode une seule fois, puis évalue le membre droit.

Un alias local peut conserver le stockage du récepteur lors d’un accès direct à un champ de classe ou d’un appel à une méthode résolue, non redéfinissable, qui renvoie directement son propre champ non statique et non référence. Le récepteur, ses bases, ses champs et les types dérivés connus ne doivent nécessiter aucun nettoyage du script ni posséder de ressources opaques. Réaffecter le récepteur initial n’invalide pas cet alias. Les références au stockage global ordinaire et aux récepteurs de structure directs restent prises en charge.

Dans la VM, conserver le stockage d’un champ ne retarde pas la destruction du script. AOT rejette donc explicitement ces chemins de référence de classe lorsqu’un nettoyage géré est nécessaire. Renvoyer un alias conservé localement, une référence d’origine non prouvée ou une référence sortant du stockage d’un callable conservé nécessite également la VM ; le générateur signale le cas non pris en charge.

Les alias indexés persistants et les références indexées renvoyées nécessitent des chemins de stockage gérés. Les références persistantes obtenues depuis des récepteurs temporaires de type valeur nécessitent aussi des propriétaires gérés. AOT rejette explicitement ces cas ; utilisez la VM.

Les champs implicites de structure, y compris les champs génériques et les éléments des tableaux fixes initialisés par défaut, suivent la sélection normale du constructeur par défaut et exécutent son corps. Les initialiseurs explicites construisent directement la valeur du champ, sans construction par défaut préalable.

## Traitement de compilation et métadonnées source

`FCodegenOptions::CompileProcessing` configure les processeurs de source et de déclarations. La préparation conserve leurs instances entre les phases ; l’émission d’un programme préparé ne les exécute pas à nouveau. Les métadonnées à la compilation et les déclarations générées sont utilisables par C++ AOT. Les requêtes de métadonnées à l’exécution (annotations, commentaires, nœuds source/de déclaration et définitions d’annotations) nécessitent la VM ; l’émission C++ autonome les rejette explicitement.

## Extensions et sources en mémoire

`FCppCodegenBackend` transmet `FCodegenOptions::ScriptCompileExtensions`, `SourceOverlays` et `StdlibSearchPaths` au chargeur Core. `EmitFromFile` applique la même résolution de sources que Core/Session, y compris une entrée entièrement en mémoire ; un chemin vide est refusé. `EmitFromSource` utilise son argument source explicite pour l'entrée et les sources en mémoire pour les dépendances.

Les scripts de compilation s'exécutent uniquement pendant la génération. Le C++ métier émis ne conserve pas leur VM et n'exige pas leurs liaisons à l'exécution. Les liaisons métier nécessitent toujours leurs implémentations cibles. Les réglages des liaisons de compilation, contrôleurs, limites et dépendances sont décrits dans [Intégration](embedding.md). Les requêtes de métadonnées source à l'exécution nécessitent toujours la VM.

## Modèles pair utilisateur

Les types `pair<T, U>` définis par l’utilisateur emploient les champs et méthodes génériques ordinaires. Si ce modèle à deux paramètres remplace le type intégré souple, l’AOT autonome refuse explicitement `entries()` et l’itération des paires clé-valeur des maps ; utilisez le backend VM pour ces opérations. L’itération des valeurs d’une map dont les éléments sont des pair utilisateur reste prise en charge.

<a id="cleanup-container-operations"></a>

Le C++ AOT autonome rejette explicitement `array<T>.remove` / `remove_at` et l’affectation par index à `map<K,T>` / `ordered_map<K,T>` lorsque la structure `T` nécessite un nettoyage de script, y compris via ses champs ou ses bases. Cette limite concerne les opérations citées ; les modes VM, Mixed et JIT les prennent entièrement en charge.

L'AOT C++ autonome permet de conserver ou de publier le `this` d'une classe lorsque sa construction réussit. Si la construction échoue alors qu'une autre référence conserve encore l'objet, le programme généré signale une erreur fatale au lieu de reprendre dans un bloc catch du script avec un nettoyage retardé des sous-objets. Utilisez le backend VM ou Core AOT si ce cas nécessite une récupération gérée. Les références temporaires libérées pendant le déroulement de pile ne déclenchent pas cette restriction.

<a id="standalone-file-io"></a>

Les opérations de fichier C++ autonomes prises en charge acceptent des chemins UTF-8 et refusent NUL intégré. Sous Windows, les longs chemins absolus, dont les UNC complets, utilisent le traitement natif des chemins longs ; les chemins relatifs restent relatifs sans préfixe étendu automatique. Les limites du système de fichiers, des droits et de l’OS restent applicables. Les requêtes lexicales suivent séparément [la référence de base](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/foundation.md).

Les arguments par référence vers un élément de conteneur nécessitent également une identité gérée par Core. L’AOT C++ autonome les refuse explicitement afin de ne pas cibler un conteneur de remplacement ni accepter après coup un élément initialement absent. Utilisez Core AOT, VM, Mixed ou JIT pour ces appels ; les lectures et affectations ordinaires par indice restent disponibles.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/aot.md) · [fr-FR](aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
