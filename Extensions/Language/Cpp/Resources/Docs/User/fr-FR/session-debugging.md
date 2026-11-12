# Déboguer une Session existante

Ce pont ne sélectionne aucun backend VM. Avant l’exécution, configurez explicitement un backend possédant `VmDebugCapability`, comme décrit dans [Intégration hôte](embedding.md) ; connecter un débogueur ne fournit pas d’interpréteur.

Installez `ISessionDebugBridge` de `VeroScriptSessionDebug.h` via `SetDebugBridge` avant l’exécution. `BeginExecution` peut renvoyer un debugger partagé pour l’appel courant : il sélectionne le débogage VM sans rejouer un appel JIT déjà en cours. `Run`, construction d’objet, appels de méthode/fonction, callables conservés et valeurs par défaut de classe sont couverts. `EndExecution` correspond à chaque begin, y compris échec et déroulement d’exception. Les workers ne sont pas des contextes supplémentaires de ce pont.

Les callbacks et inspection, évaluation, mutation et reprise de la VM arrêtée appartiennent au thread de contrôle de la Session. Le callback d’arrêt traite les requêtes sur ce thread. Seuls `RequestPause()` et l’annulation sont inter-threads : utilisez le `FExecutionControl::RequestCancel()` partagé, ou `Session::RequestCancel()` en garantissant la durée de vie de la Session. Les vues empruntées du programme, liaisons, sources et nom d’entrée expirent au retour du callback. Copiez les métadonnées utiles ; les overlays sont la configuration courante, pas une source figée. Publiez séparément les révisions source/liaison.

<a id="debug-disconnect-lifetime"></a>

La déconnexion libère la connexion arrêtée selon la politique hôte ; elle n’annule ni ne détruit à elle seule la Session. `Detached` intervient au retrait/remplacement et avant destruction. Drainez les callbacks sans attendre du travail destiné au même thread. Pour fermer : libérez l’exécution arrêtée, demandez l’annulation si souhaitée, attendez la fin de l’exécution et du transport, puis détruisez la Session. Réinitialisez un jeton d’annulation verrouillé avant réutilisation. Les callbacks natifs bloquants doivent coopérer.

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/session-debugging.md) · [fr-FR](session-debugging.md) · [ja-JP](../ja-JP/session-debugging.md) · [ko-KR](../ko-KR/session-debugging.md) · [zh-CN](../zh-CN/session-debugging.md) · [zh-TW](../zh-TW/session-debugging.md)
