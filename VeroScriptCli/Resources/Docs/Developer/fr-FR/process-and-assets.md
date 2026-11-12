# Contrat du processus et des ressources CLI

## Points d’entrée

`VeroScriptCli` est une fine couche de processus pour `veroscript`, `veroscript-lsp` et `veroscript-dap`. Le main ordinaire répartit les opérations compile/check/run et package. Le code REPL et watch ajoute l’orchestration interactive des sources et changements de fichiers. Les mains LSP/DAP délèguent le comportement de langage à Tooling.

Le CLI ne doit pas dupliquer la sémantique du compilateur, de la VM, du debugger ou du workspace. Les noms de sous-commandes, options, variables d’environnement, codes de sortie, ID de diagnostic et formats de sortie sont des jetons de compatibilité et ne sont pas localisés.

## Résolution des ressources

La résolution distingue les racines de source, racines SDK, répertoires `bin`, répertoires de bibliothèque standard et répertoires de localisation. Un exécutable et ses ressources d’exécution doivent provenir d’une installation compatible. Le repli par variable d’environnement ou `PATH` est un comportement de découverte, pas l’autorisation de mélanger des installations.

## Comportement de processus

Stdout, stderr, le traitement des lignes de diagnostic, les flux d’entrée, l’annulation du processus et la propagation du code de sortie doivent rester stables pour les consommateurs IDE, SDK et shell. L’encadrement LSP/DAP appartient aux implémentations de transport Tooling ; le CLI ne possède que le démarrage et l’arrêt du processus.

## Frontière Unicode du processus

L’entrée Windows `wmain` convertit une seule fois les arguments UTF-16 en UTF-8 avant `VeroScript::Cli::Run`. Les chaînes propriétaires et le tableau de pointeurs doivent rester valides pendant tout l’appel. Les opérations sur les fichiers utilisent les utilitaires de chemins UTF-8. `VeroScriptUnicodeCliTests` couvre les chemins sources et imports non ASCII, les arguments de scripts et l’exécution de paquets en modes VM, mixed et JIT préféré. L’affichage du terminal et le décodage des pipes relèvent du consommateur.

## Contrat d’enregistrement et de dispatch

Core expose `VeroScriptCommandLine.h` ; CLI possède registre, parseur et dispatch. Pendant `Create`, le module demande `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)`, utilise le service compatible `ICommandLineExtensionService` et enregistre synchroniquement un descripteur `FCommandLineModule` possédé. L’enregistrement ferme avant le dispatch et une déclaration invalide fait échouer tout le lot. Un module à rôles combinés doit accepter l’absence de ce service hors CLI. Chaque module doit déclarer au moins une commande ou option ; toute option exige un callback Configure.

ID et noms sont sensibles à la casse et utilisent lettres/chiffres ASCII, tirets et soulignements, au plus 64 caractères, sans tiret initial/final. Les noms Core sont réservés. Limites : 256 modules, 256 commandes et options au total par module, 8192 octets sans caractères de contrôle pour chaque description/usage/libellé de valeur. Une option exige par défaut une valeur et ne se répète pas ; un flag présent fournit une chaîne vide, une option absente aucune entrée. Utilisez `--name=value` pour une valeur ressemblant à une option.

Les callbacks Configure valident/mémorisent la configuration une fois, dans l’ordre des ID de module, avant l’exécution ou le dispatch. Ils ne reçoivent que leurs valeurs et ne doivent pas exécuter de scripts. L’aide ignore configuration et exécution de commande, mais charge/enregistre les modules. Run reçoit arguments UTF-8 intacts, langue, flux et environnement sélectionné. Ne conservez pas le contexte emprunté ni de références fortes en retour vers son environnement propriétaire. Terminez les workers avant le retour de Destroy. L’hôte conserve le code, intercepte les exceptions et valide les codes de sortie 0..255. Les commandes natives bloquantes gèrent leur annulation.

[L’exemple source NativeModule](../../../Examples/NativeModule/README.md) montre la compilation avec le SDK correspondant et l’intégration aux paquets. `VeroScriptCliModulesTests` et `VeroScriptCliRegistryTests` couvrent analyse des arguments, conflits, Unicode, durées de vie, activation/désactivation et frontières des arguments.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/process-and-assets.md) · [fr-FR](process-and-assets.md) · [ja-JP](../ja-JP/process-and-assets.md) · [ko-KR](../ko-KR/process-and-assets.md) · [zh-CN](../zh-CN/process-and-assets.md) · [zh-TW](../zh-TW/process-and-assets.md)
