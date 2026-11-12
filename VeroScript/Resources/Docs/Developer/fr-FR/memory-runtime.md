# Contrat d’implémentation de la mémoire gérée

Core possède les services mémoire dans `Public/VeroScriptMemory.h` et `Private/Memory/`. L’abaissement est dans `Private/Compiler/VeroScriptCompilerMemory.cpp`, l’exécution dans `Private/ExecutionSupport/VeroScriptVmMemory.cpp`. `stdlib/memory.vs` est un point d’entrée de module, pas une seconde implémentation. Les adaptateurs partagent ces services et ne doivent pas créer leur propre registre d’identités ni affaiblir les contrôles de propriété.

## Invariants et vérification

Vérifiez l’origine fiable du handle avant de lire sa charge. L’identité utilise une propriété faible : elle ne prolonge pas la durée de vie et distingue une adresse réutilisée de l’ancien objet. Les régions partagent un propriétaire ; les descripteurs de vue portent décalage, longueur, génération et état de lecture seule. `FMemoryContext` possède un domaine d’accès ; les workers partagent la comptabilité, mais ont des threads propriétaires distincts. Une callable autorisée sur un autre thread ne transfère pas ce domaine.

Toute opération, même de longueur nulle, vérifie domaine, génération, limites et droit d’écriture. La croissance comptabilise les deux allocations avant validation. Les opérations par lots vérifient la gouvernance tous les 64 KiB et facturent ceil(bytes/64) ; les écritures scalaires vérifient avant de toucher les octets. Les prêts empêchent les changements structurels et accès concurrents incompatibles. Les callbacks de libération s’exécutent hors des verrous Core, une seule fois, sans lever d’exception.

## Hôte et quotas

C++ utilise `VeroScriptMemory.h` et un `FMemoryContext` persistant, ou `Session.GetMemoryContext()`. Le C ABI `vs_interop_memory_*` reçoit exactement une Session ou un contexte de rappel actif. `vs_interop_buffer` reste un tampon distinct de résultat de sérialisation.

La responsabilité de libération externe est transférée uniquement en cas de succès. Tous les alias partagent l’invalidation. Les emprunts refusent immédiatement conflits et changements structurels avec Busy. Libérer une seule fois le jeton opaque après le travail natif, depuis tout thread, même après destruction de la Session. Le rappel final s’exécute hors des verrous du cœur et ne doit pas lever d’exception. Sans emprunt, une adresse est une observation immédiate.

MaxBufferBytes limite une capacité/longueur externe ; MaxRetainedBytes compte capacités possédées et octets empruntés, y compris l’ancien et le nouveau bloc durant croissance. Zéro signifie illimité. OwnedBytes, BorrowedBytes et PeakOwnedBytes excluent les métadonnées VM/objets. Run, Clear et rechargement ne réinitialisent pas le compte. Les workers partagent le budget mais ont leurs domaines propres ; les régions ne passent pas par captures, arguments, retours de tâches ou canaux.

C# : `session.Memory`, `SetMemoryLimits`, `using var lease = memory.AcquireLease(bytes, true)`. Python : `session.memory`, `set_memory_limits`, `with memory.acquire_lease(bytes, write=True) as lease` ; libérer les memoryviews avant la fermeture. Les buffers externes restent épinglés jusqu’à la dernière région ou au dernier emprunt. Les rappels interrogent les cibles ref/const-ref originales avec `vs_interop_memory_call_argument_address`, C# `context.GetArgumentAddress` ou Python `current_memory().argument_address`. Une copie passée par valeur donne Unsupported.

## Moteurs et débogage

VM et Mixed sont pris en charge. AsmJit refuse avant entrée les fonctions sensibles aux adresses et les appels membres mémoire, puis utilise le repli VM existant. Les fonctions mémoire intégrées utilisent les services du cœur. Les contrôles d’exécution conservent la politique VM. AOT autonome C++/C#/Python refuse un usage réel, mais accepte un import memory/core inutilisé. Le bytecode et les paquets ne sauvegardent ni pointeurs, ni jetons, ni registre d’identité. Les instantanés d’objets par défaut refusent les régions imbriquées ; sauvegarder explicitement les octets ou utiliser un sérialiseur explicite.

Le débogueur arrêté affiche les métadonnées du cœur. DAP variablesReference reste un identifiant de protocole ; readMemory/writeMemory arbitraires restent désactivés. Les runners Android JNI et iOS partagent le cœur et désactivent les adresses par défaut. Les Sessions C#/Python directes nécessitent une configuration explicite des quotas et capacités par l’hôte. Wasm garde ID, longueurs et quotas en bigint, vérifie l’instance et recrée les vues après croissance du tas. Ses quotas hôtes sont distincts de ceux des Sessions scripts : 16 MiB par région et 32 MiB conservés par défaut.

Conservez `memory.json` dans les six langues Core, avec des ID stables et des clés identiques. Testez stockage possédé/externe, identité, expiration, quotas, prêts occupés, propriété des callbacks, libération inter-thread, destruction Session, rechargement, instantanés, restrictions worker et refus AOT. Exécutez les tests mémoire Core/Cpp/Interop, les contrats des adaptateurs, le déploiement des langues et les contrôles documentaires. Les tests Web couvrent la croissance du heap et l’exclusion de l’entrée interactive ; la validation mobile sur appareil reste distincte.


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory-runtime.md) · [fr-FR](memory-runtime.md) · [ja-JP](../ja-JP/memory-runtime.md) · [ko-KR](../ko-KR/memory-runtime.md) · [zh-CN](../zh-CN/memory-runtime.md) · [zh-TW](../zh-TW/memory-runtime.md)
