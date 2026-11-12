# 18 : Choisir les modules de la chaîne C++

Ce tutoriel réalise un flux d’intégration native. Vous allez choisir entre
intégration runtime, génération de bindings, conversion C++ vers VeroScript et
AOT, puis exécuter l’outil adapté.

Prérequis : les outils compatibles `veroscript`, Cpp BindGen et Transpiler accessibles dans le chemin des exécutables, et un dossier de travail inscriptible. Les chemins ci-dessous désignent vos entrées : préparez des déclarations prises en charge dans `Include/Types.hpp` et `Include/Api.hpp`, des définitions dans `Source/Types.cpp` et `Source/Game.cpp`, et un `script.vs` valide, ou remplacez tous les chemins par vos fichiers existants. Exécutez les commandes depuis ce dossier. Les résultats attendus sont un `bindings.json` fusionné avec références API et manifeste, des modules VeroScript avec cartes source, puis `generated.cpp` pour l’AOT. La génération de ces fichiers n’enregistre pas les callbacks hôtes du runtime.

## Étape 1 : choisir la frontière

Consultez la [référence du SDK C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/index.md) :

- `VeroScriptCpp` pour une session C++ vivante et les appels de scripts ;
- `VeroScriptCppBindGen` quand les déclarations C++ sont la source du catalogue ;
- `VeroScriptCppTranspiler` pour convertir un sous-ensemble C++ en modules ;
- `VeroScriptCppAot` ou `veroscript aot` pour générer du C++17 depuis VeroScript.

BindGen décrit une API native, Transpiler convertit le comportement source et
AOT convertit VeroScript pour une construction native : leurs directions ne
sont pas interchangeables.

## Étape 2 : produire et vérifier un catalogue

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

Le catalogue est destiné au compilateur et aux outils ; la référence API est
destinée aux personnes. Les callbacks doivent encore être enregistrés par
l’hôte.

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

Pour plusieurs fichiers, utilisez le mode projet afin de fusionner les
déclarations avant l'émission :

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp --output-dir build/generated --manifest build/generated/transpile.manifest.json
```

Une déclaration refusée doit être adaptée par un wrapper hôte explicite, pas
masquée dans le catalogue.

## Étape 3 : convertir une source C++ puis générer de l’AOT

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json

veroscript aot script.vs -o build/generated.cpp
```

En mode projet, toutes les entrées sont fusionnées avant émission et le
manifeste doit être conservé avec les sources et cartes générées. La sortie AOT
est du C++17, pas un exécutable ; vérifiez le runtime, les callbacks et la
synchronisation dans le projet cible.

Pour produire un ensemble C++ déterministe en plusieurs fichiers, utilisez l’API `FCppAotSourceBundle`. Conservez ensemble sa source d’entrée, ses fragments générés et son manifeste ; ce sont les entrées du projet C++ cible, pas un exécutable déjà construit.

Les globaux de premier niveau et les champs statiques générés restent isolés
par travailleur natif. L'AOT initialise les accesseurs globaux de chaque
travailleur dans l'ordre des déclarations avant `main`. N'utilisez pas ces
globaux comme mécanisme implicite de partage entre threads ; utilisez des
canaux ou un état hôte synchronisé.

Lorsqu'un lambda membre utilise son récepteur, l'AOT conserve la sémantique du
type source : un récepteur de classe reste un objet vivant conservé, tandis
qu'un récepteur de structure est un instantané de valeur. Les lambdas imbriqués
conservent le récepteur déjà capturé et un lambda sans capture ne peut pas
utiliser `this`. L'AOT C++ refuse également la capture d'un récepteur de classe
depuis le corps d'un destructeur, car le récepteur est déjà en cours de
destruction.

## Exercices

1. Comparez une entrée du catalogue avec l’enregistrement C++ correspondant.
2. Transpilez deux fichiers et inspectez le manifeste et les cartes source.
3. Générez de l’AOT pour un script utilisant un channel et notez l’état hôte à
   synchroniser.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/cpp/01-cpp-toolchain.md) · [fr-FR](01-cpp-toolchain.md) · [ja-JP](../../ja-JP/cpp/01-cpp-toolchain.md) · [ko-KR](../../ko-KR/cpp/01-cpp-toolchain.md) · [zh-CN](../../zh-CN/cpp/01-cpp-toolchain.md) · [zh-TW](../../zh-TW/cpp/01-cpp-toolchain.md)
