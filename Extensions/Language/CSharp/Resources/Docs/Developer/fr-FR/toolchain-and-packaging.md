# Outillage et contrat de paquet C#

## Limites strictes des outils

Lorsqu’un point d’entrée est présent, le Transpiler C# abaisse celui choisi par le compilateur C#, non une heuristique de nom de classe. Il accepte uniquement un `static void Main()` ou `static int Main()` sans paramètre, déclaré dans un type statique ; les points d’entrée ambigus, avec arguments ou dans un conteneur d’instance échouent avec `VSCT2001`. BindGen rejette les indexeurs C# car Binding Catalog v1 ne permet pas encore leur abaissement hôte. Aucun outil n’omet silencieusement une entrée ou un membre non pris en charge ; la génération ne commit ses sorties qu’après validation réussie.

## Frameworks et SDK

`VeroScriptCSharp` cible `net8.0` et `net9.0`, utilise C# 12.0, génère la documentation XML et active la validation API des paquets. Le paquet runtime est `VeroScript.CSharp`. CMake vérifie le SDK .NET installé avant build et exécute les tests managés pour les deux frameworks.

Les projets frontend, Transpiler et BindGen C# ciblent aussi `net8.0`/`net9.0`, mais leurs outils de build nécessitent un SDK .NET 9.0 ou ultérieur dans l’outillage CMake courant. C’est un prérequis de build d’outil, distinct des frameworks supportés par le paquet runtime. Les outils en ligne de commande empaquetés utilisent le report de runtime `Major` lorsque le runtime .NET 8 ou 9 demandé n’est pas installé, ce qui permet leur exécution sur une version majeure ultérieure prise en charge.

## Contenu du paquet

Le `.nupkg` runtime contient les assemblies managées net8.0/net9.0, la documentation API XML, les cibles d’intégration buildTransitive et la licence. Les symboles de débogage sont distribués séparément dans `.snupkg`. Les fichiers source README.md, API.md et MOBILE.md ne sont pas inclus ; obtenez les guides via la documentation Resources séparée. Le runtime Interop natif reste une entrée de déploiement de l’hôte. CMake installe les sorties managées propres à chaque framework et les paquets NuGet générés dans la zone SDK.

## Bundles source AOT C#

`VeroScriptCSharpAot` expose une unité de compilation C# générée et un `FCSharpAotSourceBundle` déterministe. Un bundle contient les fichiers source relatifs du support runtime, programme lowered et point d’entrée, avec un projet SDK et `ManifestJson` lisible par machine. `BuildCSharpSourceBundle` empaquette du C# déjà émis sans réécrire la source générée.

Les entrées source, fichier et programme vérifié acceptent les mêmes contrôles loader : chemins import/implicit import, `Defines`, politique d’import circulaire et d’accès projet. Les overloads avec options portent aussi catalogue hôte et réglages de génération de code. Un échec de load, vérification sémantique, lowering ou construction du Bundle retourne un échec sans Bundle partiel prêt à construire. Les chemins relatifs et champs manifest sont des contrats de sortie et ne sont jamais localisés.

## Vérifications d’enregistrement généré

Le packaging BindGen comprend génération du catalogue, compilation et tests runtime de l’enregistrement généré, tests d’import de catalogue C++ et vérification de paquet. Un changement de source C# générée ou de forme de catalogue est incomplet si un consommateur d’enregistrement généré ne compile plus ou ne s’exécute plus.

## Transpilation source

`VeroScript.CSharpTranspiler` et `VeroScript.CSharpTranspiler.Tool` fournissent une conversion C#-vers-VeroScript stricte. Le mode projet résout tous les fichiers en une compilation Roslyn, émet imports sémantiques inter-fichiers, source maps et manifest déterministe, puis commit atomiquement l’ensemble. Les sources générées sont compilées par défaut par le validateur VeroScript réel.

## Avis des dépendances des outils

Les sorties du frontend C#, de BindGen CLI et de Transpiler CLI conservent les avis Roslyn originaux sous `licenses/` ; le NuGet du frontend et les deux paquets d’outils .NET les incluent aussi. Ils complètent l’avis MIT du projet. Lors d’un changement de version Roslyn, mettez à jour les [sources des avis](../../../Licenses/README.md) et conservez les fichiers complets `Roslyn-LICENSE.txt` et `Roslyn-ThirdPartyNotices.rtf`, y compris l’encodage RTF. La vérification des paquets les compare aux sources. Ne localisez pas les textes de licence amont.

## Arité générique des conteneurs standard

Voir [Frontend C# — Arité générique des conteneurs standard](../../User/fr-FR/frontend.md#container-mapping-arity).

## Abaissement des entiers et limites numériques

Le Transpiler conserve le débordement circulaire C# par défaut des additions, soustractions, multiplications, négations, incréments, affectations composées et conversions entières, selon la largeur résolue. Le nombre de positions de décalage est masqué par 31 ou 63. L’affectation composée évalue une seule fois la cible et son ancienne valeur avant l’opérande droit ; les propriétés automatiques deviennent des champs. La division et le reste entiers lèvent des exceptions de script interceptables pour un diviseur nul ou un débordement minimum signé/-1. Les constantes `double` de valeur entière gardent leur type flottant. Les tests comparent l’exécution CLR et VeroScript.

Voir [Transpileur C# — Abaissement des entiers et limites numériques](../../User/fr-FR/transpiler.md#numeric-and-console-boundaries).

Le contrôle des paquets installe le Transpiler, génère du code, le valide avec le compilateur réel et l’exécute. Il vérifie la division et le reste non signés, le débordement avec rebouclage signé, le texte booléen et les retours à la ligne, la division par zéro interceptable, ainsi que le rejet des constantes de chaîne dans un contexte checked non pris en charge, même sans validation.

Les conversions des nombres flottants en entiers du C# AOT tronquent vers zéro et contrôlent la valeur binaire par rapport à la plage cible avant la conversion. Elles préservent les valeurs entières représentables sur 64 bits sans arrondi décimal. Les cibles non signées rejettent les entrées négatives, y compris les fractions négatives ; NaN, les infinis et les valeurs hors plage échouent. Les tests couvrent les limites entières en exécution managée et NativeAOT.

Voir [AOT C# — Exclusions explicites](../../User/fr-FR/aot.md#aot-floating-conversions).

Le transpileur C# applique les promotions des opérandes de Roslyn avant les comparaisons mixtes entre entiers et flottants, préservant ainsi l’arrondi float de C# avant la comparaison du cœur.

Les branches conditionnelles numériques appliquent les conversions implicites choisies par Roslyn, préservant les largeurs entières et l’arrondi float ; seule la branche sélectionnée est évaluée. Les conversions non constantes d’entiers de 64 bits vers float sont également rejetées dans les branches conditionnelles.

Voir [Runtime C# et adaptateur Interop — Libération des callables conservés](runtime-and-interop.md#managed-marshalling-maintenance).

[Android et iOS / MAUI](../../User/fr-FR/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

La compilation mobile utilise le même projet et paquet : `VeroScriptIncludeMobile=true` inclut les cibles .NET 8/9 et .NET 10 Android/iOS avec le SDK natif vérifié.

Lors de la suppression des implémentations inutilisées importées par `core.vs`, les appels résolus du bytecode conservent les dépendances exécutables, les corps génériques conservent leurs dépendances aux symboles libres et les dépendances de types sont collectées à partir des déclarations et expressions de l’AST en tenant compte des portées, avec résolution des noms publics par la table d’alias du Core. Les appels tels que `std::sqrt`, `std::algorithm::sort` et les types tels que `std::delegate` conservent les implémentations nécessaires. Les noms locaux, les noms des membres d’instance et le contenu des chaînes ordinaires ne sont pas traités comme des références à des fonctions globales.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
