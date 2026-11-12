# Documentation technique de VeroScript CLI

`VeroScriptCli` est une fine couche de processus. Il fournit les points d’entrée `veroscript`, `veroscript-lsp` et `veroscript-dap` sans réimplémenter le compilateur, la VM, l’API d’embedding C++ ou la sémantique des services de langage.

## Responsabilités du processus

Le CLI gère l’analyse des arguments, la répartition des commandes, les flux du processus, l’orchestration REPL et watch, la sélection source/fichier, la recherche des ressources d’environnement et SDK, la politique de code de sortie, ainsi que les mains des processus LSP/DAP. Les modules core, session C++, AOT et Tooling restent la source du comportement du langage.

Les options de commande, noms de sous-commandes, variables d’environnement, codes de sortie, ID de diagnostic et formats de sortie sont des surfaces de compatibilité. Ils ne doivent ni être traduits ni être modifiés uniquement pour améliorer la prose.

## Ressources d’exécution

Les processus autonomes doivent trouver la bibliothèque standard et les catalogues de localisation correspondant à leur version. La résolution des ressources doit rester relocalisable et ne doit pas utiliser silencieusement des copies périmées du répertoire de build. Une ressource absente ou incompatible est un échec de démarrage/configuration, et non un repli vers une langue vide.

## Points d’entrée de protocole

Les mains LSP et DAP délèguent le comportement de protocole à `VeroScriptTooling`. Le CLI doit préserver l’encadrement des flux, l’ordre des requêtes, l’annulation, la séparation stderr/stdout et le comportement de sortie attendus par ces services.

## Détails des commandes et ressources

Le main ordinaire répartit les opérations compile/check/run et les opérations orientées package ; les processus REPL et watch ajoutent l’orchestration interactive des sources et changements de fichiers. `VeroScriptLspMain` et `VeroScriptDapMain` sont des points d’entrée de processus réservés au protocole et délèguent le comportement de langage à `VeroScriptTooling`. Le CLI ne doit pas créer une seconde implémentation de l’état Session, debugger ou workspace.

Le résolveur de chemins du CLI doit distinguer une racine de source, une racine SDK, un répertoire `bin`, le répertoire de bibliothèque standard et le répertoire de localisation. Un exécutable découvert et ses ressources doivent venir d’une installation compatible ; le repli par variable d’environnement ou `PATH` est un comportement de découverte, pas l’autorisation de mélanger les versions.

## Pages détaillées

- [Contrat du processus et des ressources CLI](process-and-assets.md)

## Source de vérité

Utilisez `Private/`, la cible CMake du CLI, les tests de processus, les API publiques Tooling et les tests smoke de packaging/SDK. Mettez ce document à jour avec toute modification de la répartition des commandes, de la recherche de ressources, du comportement de flux, des variables d’environnement, des codes de sortie ou des frontières de processus LSP/DAP.

## Initialisation de projet

`Private/VeroScriptCliProject.cpp` implémente `project init` par création locale de
fichiers C++17, sans charger l'environnement des paquets ni compiler. La création
exclusive préserve les fichiers existants même lors d'appels concurrents ; le
registre CLI natif réserve la commande `project`. Le test de processus
`VeroScriptProjectInitCliTests` couvre chemins, répétition, conflits, concurrence
et récupération après échec d'écriture ; il n'est enregistré que pour les constructions natives.
Avec les services de construction, il construit et exécute aussi l'application
générée et l'exemple documenté. Les modèles suivent les schémas `0.1.1` existants ;
l'initialisation ne fabrique ni verrou de dépendances ni état d'installation.
La conversion des chemins UTF-8 prend en charge C++17 et C++20 ; la régression
CMake compile l'initialiseur autonome dans les deux modes en traitant les avertissements comme des erreurs.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
