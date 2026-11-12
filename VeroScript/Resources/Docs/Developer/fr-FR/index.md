# Documentation technique du cœur VeroScript

[Backends VM remplaçables](vm-backends.md)

Ce répertoire décrit l’implémentation du langage et du cœur natif, pour mainteneurs et auteurs de modules. Ce n’est ni un tutoriel de langage ni un manuel d’utilisateur runtime. Les en-têtes publics et les tests définissent le contrat pris en charge ; ces pages enregistrent l’architecture et les règles qui restent visibles lors d’une évolution interne.

## Pipeline

Le cœur possède frontend, AST, diagnostics, loader de modules, préprocesseur, compilateur sémantique, bytecode, VM, génération de code, JIT, paquets, réflexion, bindings hôte, sérialisation externe, concurrence, débogage, hot reload et sélection de locale. La pipeline normale résout sources/overlays/imports/paquets, tokenize et prétraite en préservant les positions, parse déclarations/expressions/statements/types/métadonnées vers l’AST public, vérifie sémantique/types/overloads/génériques/propriété/visibilité, émet bytecode et debug metadata, lie module/paquet, puis exécute par VM ou backend JIT choisi. Un échec conserve diagnostics structurés et positions ; il ne produit jamais silencieusement un autre programme.

## Invariants runtime

`VeroScriptVm`, `FValue`, handles objet/conteneur, valeurs fonction, valeurs hôte et références forment un unique modèle de propriété. Destruction, références, événements mutation, sérialisation externe et callables retenus préservent identité et durée de vie. Un callback de mutation de conteneur observe une mutation achevée et son exception ne fait pas rollback.

Les globales de script et statiques de classe sont isolées par worker natif. Les channels sont la capacité explicite de synchronisation inter-worker ; registres et ID de tâches sont worker-local. Les restrictions de creator thread pour sessions et callables retenus font partie du contrat embedding.

## Backends d’exécution

Voir [Architecture centrale et pipeline de compilation — Sélection du backend](architecture-and-pipeline.md#core-vm-ownership).

## Frontières publiques et privées

`Public/` est la surface native include prise en charge ; `Private/` contient compilateur, parser, VM, JIT et détails internes. Les consommateurs externes et adaptateurs de langage utilisent les en-têtes publics. Quelques cibles d’implémentation embedding/tooling/AOT internes à l’arbre source reçoivent explicitement les chemins privés via CMake ; ce sont des dépendances friend revues, ni installées ni prises en charge comme API, et leur liste ne doit pas s’étendre sans revue du cœur.

Le SDK C++ natif est un contrat de bibliothèque statique lié à une build précise : le consommateur est recompilé avec les mêmes en-têtes, bibliothèque, architecture, runtime et build identity. La compatibilité binaire du layout C++ entre builds n’est pas garantie ; seule la surface C11 `VeroScriptInterop` constitue l’ABI de processus stable. Les changements de layout C++ public, valeur enum, format bytecode, record de binding hôte, identifiant diagnostic ou métadonnée de paquet exigent toujours une revue de compatibilité.

## Localisation et ressources

Le cœur charge catalogues runtime et tooling depuis la racine partagée de localisation avec noms de répertoire BCP 47 canoniques. La découverte de locale dépend des répertoires et Catalogs : le nombre de locales n’est pas une constante de compilation. Identifiants source, ID de diagnostic, clés JSON, API, chemins, versions et exemples sont des tokens techniques et ne sont pas localisés.

## Session, paquets et bindings hôte

La surface session distingue chargement source/fichier/paquet, compilation sans commit, commit de module compilé, run, clear et reload. Le reload fichier conserve son chemin ; les overlays source n’en créent pas. Le chargement de paquet valide la forme application/bibliothèque. Le hot reload est opt-in et préserve uniquement l’état explicitement reloadable ; un échec restaure le rollback pending au lieu de remplacer le module fonctionnel par un résultat partiel. `VeroScriptModuleLoader` reçoit explicitement paths d’import, imports implicites, racines stdlib, overlays, définitions `-D`, macros prédéfinies, types hôte et politiques d’import circulaire/accès projet ; leur normalisation contribue à l’empreinte source. L’ajout d’une racine stdlib peut fusionner un arbre voisin `localization/`.

Les bindings hôte passent par le catalogue : fonctions natives, types hôte, modèles/instanciations génériques, membres, opérateurs, callbacks clone, types héritables, enums scoped et factories de sous-classe sont validés avant usage. La réflexion énumère types, champs, méthodes d’instance, fonctions libres, globales, enums et interfaces chargés sans exposer les structures privées du compilateur. Les métadonnées bytecode retiennent aussi interface, enum scoped, déclaration, flags de référence, schémas capture, régions exception, plages d’initialiseur, propriété de paquet, déclarations exportées et modules modèle embarqués ; l’optimisation codegen ne les supprime jamais lorsqu’elles sont observables.

## Pages détaillées

- [Architecture centrale et pipeline de compilation](architecture-and-pipeline.md)
- [Exécution, état et paquets du cœur](execution-and-state.md)
- [JIT et concurrence du cœur](jit-and-concurrency.md)
- [Build central et ressources d’exécution](build-and-assets.md)

## Gouvernance d’exécution et observabilité JIT

La VM fournit gouvernance coopérative par `FExecutionLimits`, `FExecutionControl` et `FScriptCapabilities`. Les limites instruction/wall-clock emploient zéro pour illimité. `RequestCancel` marque le contrôle partagé sans pouvoir interrompre un callback natif ou managé déjà en cours.

La politique de capacité contrôle les effets de bord fichier, environnement, console et tâche. Une restriction active est imposée par la VM et non contournée par JIT. Le backend demandé est `Vm`, `Mixed` ou `Jit`; le débogueur, l’initialisation avec état conservé, le nettoyage borné et la gouvernance nécessitant l’interpréteur choisissent la VM. La politique JIT et `FJitStats` sont des contrats d’observabilité : ils distinguent compilation, cache, dispatch, repli/échec, budget, type d’entrée et fast path. Une compilation non prise en charge est une décision de repli normale, pas un diagnostic de langage sauf échec de l’exécution.

## Mises à jour du modèle compilateur

Le modèle parser/compilateur conserve les modificateurs de type static et membres de données static comme globales de support possédées par le module. L’accès statique se résout à travers hiérarchie et interfaces du type déclarant ; les instanciations génériques reçoivent leur stockage static substitué propre. Les restrictions de type static sont des règles sémantiques du compilateur, pas une convention d’émetteur.

L’AST retient aussi métadonnées de capture lambda, paramètres/bornes génériques, alias, annotations de déclaration et plages source nécessaires aux diagnostics/outils. Tout lowering consommant l’AST préserve identités et positions ; une forme capture/générique non prise en charge échoue explicitement au lieu d’être approximée.

## Source de vérité

Utiliser `Public/`, l’arbre responsable `Private/`, le target CMake central, les tests runtime/compiler/VM/JIT ciblés et les ressources stdlib/localisation déployées. Mettre à jour ce document avec chaque modification du comportement sémantique, politique d’exécution, propriété, ABI publique, format bytecode/paquet, diagnostic ou chargement des ressources runtime.

- [Ressources de localisation et déploiement](localization-and-resources.md)

- [Contrat d’implémentation de la mémoire gérée](memory-runtime.md)

- [Traitement de compilation et métadonnées source](compile-processing.md)
- [Extensions de compilation en script](script-compile-extensions.md)

- [Contrat d’exécution i18n](i18n-runtime.md)

- [Environnements de modules et extensions natives](module-environments.md)

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
