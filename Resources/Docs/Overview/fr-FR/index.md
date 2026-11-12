# Vue d’ensemble de VeroScript

[Code source sur GitHub](https://github.com/VanstySanic/VeroScript)

Cette vue d’ensemble relie le langage, les outils, les SDK hôtes et les guides de maintenance selon la tâche. Les références des modules définissent les API, la sémantique et les limites ; les relations entre modules et les parcours de lecture sont maintenus ici.

## Déploiement multiplateforme

| Déploiement | Configurations cibles | Guide |
| --- | --- | --- |
| Runtime et SDK de bureau | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [Installation et distribution](../../../../VeroScriptInstaller/Resources/Docs/User/fr-FR/index.md) |
| Intégration mobile | Android, iOS (adaptateurs expérimentaux) | [SDK mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md) |
| Navigateurs et hôtes JavaScript | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md) |

Choisissez une distribution correspondant à votre système et à votre architecture. La disponibilité des paquets et leur état de validation dépendent du produit et de la version ; consultez leurs notes de plateforme pour les dépendances, moteurs disponibles et conditions de déploiement.

## Choisir un parcours

| Objectif | Commencer ici | Poursuivre avec |
| --- | --- | --- |
| Écrire et exécuter des scripts | [Introduction](../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md) → [Bases du langage](../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md), [Bibliothèque standard](../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/index.md), [Index des exemples](../../../../VeroScript/Resources/Docs/User/fr-FR/examples.md) |
| Installer, mettre à jour ou distribuer | [Installation et distribution](../../../../VeroScriptInstaller/Resources/Docs/User/fr-FR/index.md) | [Paquets](../../../../VeroScriptCli/Resources/Docs/User/fr-FR/packages.md), [Référence CLI](../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) |
| Intégrer des scripts dans un hôte | Choisir un SDK ci-dessous | [Frontières Interop](../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md), [Outils et intégration](../../User/fr-FR/toolchain/index.md) |
| Éditer, diagnostiquer et déboguer | [Référence CLI](../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/lsp.md), [Débogage](../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md), [Localisation des diagnostics](../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md) |
| Développer ou maintenir l’implémentation | [Parcours développeur](#parcours-développeur) | [Tous les guides développeur](../../Developer/fr-FR/index.md) |

Vérifiez les composants installés et leurs versions avant de choisir le moteur d’exécution, les liaisons hôtes ou la configuration des paquets. Les commandes et exemples SDK exigent les composants correspondants ; la documentation ne remplace pas les validations de compilation et d’exécution.

## Choisir un SDK hôte

| Hôte | Référence utilisateur | Entrée de maintenance |
| --- | --- | --- |
| C++ natif | [SDK C++](../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/index.md) | [Modules C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/fr-FR/index.md) |
| ABI C ou autre langage | [SDK Interop](../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md) | [ABI et durée de vie](../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/abi-and-lifetime.md) |
| CPython | [SDK Python](../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/index.md) | [Modules Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/fr-FR/index.md) |
| .NET / C# | [SDK C#](../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/index.md) | [Modules C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/fr-FR/index.md) |
| Navigateur ou JavaScript | [SDK WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md) | [Module WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/fr-FR/index.md) |

Consultez le contrat du SDK choisi pour les valeurs, handles, callbacks, threads et libérations. Chaque module définit les sous-ensembles de source acceptés, les restrictions AOT et les environnements cibles ; le nom d’un langage ne suffit pas à établir la compatibilité.

## Parcours développeur

1. Lisez [Architecture et pipeline du cœur](../../../../VeroScript/Resources/Docs/Developer/fr-FR/architecture-and-pipeline.md) et [Exécution et état](../../../../VeroScript/Resources/Docs/Developer/fr-FR/execution-and-state.md) pour comprendre les frontières de compilation, d’exécution et d’état ; poursuivez avec [JIT et concurrence](../../../../VeroScript/Resources/Docs/Developer/fr-FR/jit-and-concurrency.md) pour les moteurs et la concurrence.
2. Suivez les frontières hôtes dans [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/index.md), [Sessions C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/fr-FR/session.md) et les guides des adaptateurs concernés.
3. Suivez les frontières des outils dans [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/fr-FR/index.md), [Protocoles des outils](../../../../VeroScriptTooling/Resources/Docs/Developer/fr-FR/architecture-and-protocols.md).
4. Avant publication, consultez [Compilation et ressources](../../../../VeroScript/Resources/Docs/Developer/fr-FR/build-and-assets.md), [Compilation et empaquetage des installateurs](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/build-and-bundle.md) et [Procédure de validation de publication](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/release-validation.md).

Les résultats antérieurs concernent uniquement les sources, artefacts et environnements enregistrés. Suivez la [procédure de validation de publication](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/release-validation.md) pour les compilations des produits, les contrôles d’intégrité et les résultats de validation à l’exécution ; conservez les preuves dans des répertoires de sortie ignorés.

## Documentation et publication

Consultez l’[entrée utilisateur](../../User/fr-FR/index.md) pour la carte complète des modules. Les responsabilités documentaires, les six langues, les commandes de validation et les paquets All/User sont décrits dans le [guide de maintenance documentaire](../../Developer/fr-FR/index.md).

Les liens et ancres des documents publiés fonctionnent hors ligne ; les références au code source et les sites externes nécessitent une connexion. User contient les usages et tutoriels ; All ajoute les guides de maintenance et les règles. Aucune de ces collections ne contient History.

[Documentation utilisateur](../../User/fr-FR/index.md) · [Documentation développeur](../../Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
