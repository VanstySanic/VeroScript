# Maintenance de la VM de bytecode

Core possède opérations du langage, valeurs, frames logiques, gouvernance, nettoyage et dispatch. Ce module possède l’interpréteur complet dans `Private/VeroScriptBytecodeVmExecution.cpp`, l’exécution des membres et les diagnostics. Bibliothèque statique et module dynamique lient le contrat d’en-têtes `VeroScriptExecutionContract`, sans second binaire Core. `BytecodeVm::CreateFactory` reçoit les services du véritable Core propriétaire.

`FVmInvocation` reçoit les frames déjà préparées. Préservez domaine, contexte, génération du programme, jeton d’appel et frontière de frame ; ne préparez pas les arguments et n’entrez pas deux fois. Positionnez `bEntered` avant la première opération sémantique. `NotEntered` n’est valide qu’avant toute entrée. Continuations, instrumentation, gouvernance et nettoyage incomplet/reset gardent leurs contrats Core.

Chaque contexte actif a une instance ; la réentrée synchrone utilise son instance active. Le backend officiel permet le réattachement au repos dans le même domaine/thread. Détachez les anciens programmes et conservez fabrique/module pendant la vie des instances. La fabrique qui alloue détruit ses instances ; le descripteur fournit la destruction de fabrique dans son module allocateur.

L’entrée dynamique est `veroscript_vm_query`. Vérifiez `CoreVersion`, taille du descripteur, `BuildVmModuleAbi()`, ID/version et capacités avant utilisation. L’encodage produit est major:8/minor:8/patch:16, distinct du C ABI. L’identité générée suit les sources. En-têtes, compilateur/STL/CRT, plateforme et layouts doivent correspondre, même à version `0.1.1` identique.

`VS_VEROSCRIPT_VM_VERSION` est le paramètre de cache CMake numérique `major.minor.patch` du produit VM indépendant ; sa valeur par défaut est `0.1.1`. Il fournit `VEROSCRIPT_VM_VERSION`, les versions des descripteurs de fabrique/module et du paquet CMake installé. Les outils complets transmettent la version du produit `Vm` enregistrée et refusent cache ou artefacts périmés. Cette version reste distincte de `CoreVersion`, de l’ABI de service et des empreintes d’implémentation/en-têtes ; changer le numéro ne rend pas compatibles des binaires incompatibles.

Le projet CMake autonome consomme un `VeroScriptExecutionContract` installé ; la racine fournit aussi Core aux tests. `VeroScriptVmBackendStaticTests` et l’optionnel `VeroScriptVmBackendDynamicTests` couvrent les deux formes. `Resources/Tests/Contract.cpp` et `Package.py` concernent la consommation du contrat/paquet. Vérifiez capacités, repli avant entrée, réentrée, gouvernance, nettoyage/reset, propriétaires et consommateurs installés. iOS/Web utilisent l’assemblage statique ; les résultats desktop ne valident pas les appareils/autres plateformes.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/fr-FR/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
