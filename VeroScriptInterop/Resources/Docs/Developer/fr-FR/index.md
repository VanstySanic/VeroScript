# Documentation technique VeroScript Interop

`VeroScriptInterop` est l’ABI stable de processus C11 pour les hosts non C++. Ce document s’adresse aux mainteneurs de bindings. Header ABI, implementation, tests et bindings consommateurs générés sont autoritaires ; il ne faut pas inventer une abstraction managée absente du contrat C.

## Surface ABI

Le header public définit les contextes opaques session/value/callable/call/buffer/package/interception/generic-host-type/sérialisation, les vues string/byte, status/enums, session options, source overlays, runtime configuration, JIT policy/statistics, native signatures, host-type descriptions, mutation events et visitor callbacks. La capability optionnelle de cache JIT session ajoute un policy switch append-only, des retained-resource limits et statistiques cumulées tout en gardant ABI 0.1.1 et le policy layout original de 32 bytes.

Les exports couvrent runtime/version/capability query, buffer, locale catalog, création/destruction de session, configuration, chargement source/file, exécution, values, calls, packages, host bindings, interceptors, mutation, reflection et serialization. Noms exportés, layout, calling convention, enum values et status meanings sont ABI, non prose.

## Propriété et vues

Opaque handle possède des règles explicites de création, emprunt, retain et destruction. Une view n’est pas owner. Une string/byte view retournée ne survit pas à son owner ; un buffer est copié avant libération de l’owner. Callback context est valide uniquement pendant l’appel callback documenté. Les bindings doivent rendre ces contraintes visibles, sans supposer un garbage collector ou reference-counting non garanti par l’ABI C.

## Callback et threads

Les callback result passent par le callback status contract ABI. Callback exception, conversion failure et invalid user data sont rapportés sans fuite native. Les host callback peuvent venir de script workers ; l’état host nécessite donc sa propre synchronization. Creator-thread restriction et callback reentrancy restent alignées avec core et chaque language adapter.

## Gestion des versions

`vs_interop_get_abi_version()` rapporte le contrat ABI. Runtime semantic version, capability bits, package metadata et ABI version sont distincts. Ne mettez pas un pre-release numeric marker dans le texte de version utilisateur et ne traitez jamais un ABI number comme version produit. Additive field/function exige une décision de compatibilité ; layout/enum change exige ABI review et consumer updates.

## Localisation

Les fonctions locale sélectionnent et énumèrent les répertoires catalog BCP 47 canoniques. Locale discovery ne suppose pas un nombre fixe de langues. Catalog key, exported function name, status code, ABI value, diagnostic ID et version string ne sont jamais traduits.

## Détails session, value et callback

Les API session distinguent owner-thread operation, idle-session operation et operation permise pendant run. Loading, clearing, reflection, package management, configuration et binding change sont rejetés si leurs preconditions ne sont pas remplies. `vs_interop_session_copy_last_error()` est le fallback des erreurs d’opération ; les diagnostics structurés passent par count, arguments et formatted-diagnostic API.

La surface value inclut scalar, string, enum, host object, array, map, ordered map, set, queue, stack, deque et channel. Les callbacks d’énumération reçoivent des views empruntées. La capacity channel est fixée avant usage ; close réveille les opérations bloquées et send/receive failure renvoie status plus optional owned UTF-8 error buffer. Les nested capability transfer et invalid type conversion sont des engine error, non des coercitions réussies.

Les helpers callback context sont le chemin supporté pour retourner une erreur depuis native host function, generic type factory, instance interceptor, external serialization/deserialization callback. Une callback ne détruit ni ne clear sa owning session en réentrance, sauf si implementation rapporte explicitement un engine error. Callback user data, host-object payload et managed root survivent à toute entrée callback possible.

## Pages détaillées

- [ABI Interop et durée de vie](abi-and-lifetime.md)
- [Valeurs, callbacks et packages Interop](values-and-callbacks.md)

## Source de vérité

Utilisez `Public/VeroScriptInterop.h`, implementation/test privés, configuration package CMake et adapters C#/Python consommateurs. Mettez à jour ce document à chaque changement d’ownership, layout, callback, threading, status, locale, capability ou versioning.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
