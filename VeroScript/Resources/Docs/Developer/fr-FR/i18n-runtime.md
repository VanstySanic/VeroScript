# Contrat d’exécution i18n

Le cœur gère les valeurs, la configuration atomique, les instantanés immuables et les diagnostics des appels natifs. La langue des diagnostics reste indépendante. ICU, les formats de fichiers et le rafraîchissement GUI relèvent des extensions.

## API publique

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h) expose `FI18nArgument`, `FI18nArguments`, `FI18nConfiguration`, `FI18nText`, `FI18nContext`, `FI18nSnapshot` et `FI18nVersion`.
- `CreateContext(config, services, out, error)` crée le contexte ; `CaptureSnapshot()`, `TryLookup`, `TryResolve` et `TryFormat` interrogent son état. `ToScriptValue` / `FromScriptValue` vérifient l’identité des enveloppes Core.
- `FI18nServices::Publish` publie atomiquement les deux vues immuables. `PublishResources` / `PublishFormatter` conservent la dernière autre vue ; `Acquire` renvoie un instantané possédé. Créez une nouvelle vue quand ressources ou configuration changent. Republier la même vue sans propriétaire conserve l’ancrage de module existant ; un shared_ptr possédant un bloc de contrôle avec un pointeur alias nul constitue aussi un ancrage valide.
- `ConfigureAndSetServices` remplace atomiquement configuration et référence de services, sans modifier transactionnellement un service partagé avec d’autres contextes. Les méthodes `Set*` fusionnent avec les derniers autres champs.
- Le résolveur reçoit une langue normalisée exacte par appel et renvoie `Found`, `NotFound` ou `Failed`. Une chaîne trouvée vide est valide. Seul Core applique la chaîne de repli explicite.
- Le formateur est immuable et utilisable en concurrence via `SupportsFormat` et `Format`. La requête conserve arguments typés, langue demandée, langue trouvée, locale de formatage effective et options typées. `literal` / `vero.named` sont réservés à Core.
- `FI18nWork` emprunte le contrôle de l’opération courante et sa limite de sortie, zéro signifiant illimité. Vérifiez l’annulation pendant les traitements longs et le budget avant d’allouer la sortie. Core contrôle avant/après les callbacks mais ne peut interrompre de force un appel natif bloquant.
- Valeurs et instantanés possèdent leurs données. Ne conservez ni contexte de callback, ni VM, ni vues de requête empruntées, ni `FI18nWork`. Les modifications d’une valeur native ordinaire exigent une synchronisation externe ; les méthodes des contextes/services sont sûres en concurrence. Valeurs personnalisées et fournisseurs doivent être immuables et permettre les lectures concurrentes.
- Les propriétaires des modules fournisseurs/personnalisés doivent survivre aux callbacks et destructeurs virtuels. Fournissez un propriétaire explicite pour le code chargé dynamiquement ; évitez les cycles forts contexte/service → fournisseur → contexte/service avec des références faibles.
- La récursion `Try*` sur le même contexte est refusée par thread. Un fournisseur peut lire ou actualiser la configuration ; l’opération active conserve son instantané initial et sa valeur texte même si le callback remplace ou réinitialise les variables de l’appelant. Les entrées string_view empruntées doivent rester valides jusqu’au retour et la sortie d’erreur ne doit pas posséder leurs chaînes. Ne rentrez pas dans une VM en cours d’exécution.
- L’égalité des versions comprend identité du contexte et révisions de configuration, ressources et formateur. L’épuisement d’une révision échoue sans bouclage ; ces versions ne sont ni persistantes ni interprocessus.
- Une erreur structurée possède `FI18nError::Status`, `Diagnostic`, domaine/clé/langue/nom. `NotFound` ne porte aucune erreur ; un échec exige un statut différent de Ok et un diagnostic. Un callback réussi ne renvoie pas d’erreur.
- `FNativeCallContext::DiagnosticOutput` est une sortie empruntée pour un `FScriptError` possédé ; VM et JIT conservent ID/arguments et appliquent le site d’appel courant. `I18nServices` donne un accès emprunté aux services de l’appel en cours. Les contextes créés par script capturent la sélection de services possédée, pas la VM.
- Configurez `VeroScriptVm::SetI18nServices` ou `FEngineConfig::I18nServices` / `I18nLimits` dans Engine/Session C++. Les workers héritent sélection et budget mais utilisent leur propre contrôle d’exécution. Les contextes existants conservent leur référence de services après un changement de configuration hôte.

## Compilation et exécution

Le parseur et le compilateur utilisent le catalogue commun `StandardLibrary::GetBindingCatalog`, avec les entrées mémoire canoniques avant i18n. L’opcode relatif à Core garde le nom interne `CallMemory` et indexe désormais ce catalogue commun ; aucun opcode ni variante `FValue` supplémentaire n’est ajouté. `Memory::UsesRuntimeMemory` exclut les entrées i18n. Le chargement de bytecode/paquet vérifie le même catalogue ; les contextes, fournisseurs et charges personnalisées vivants restent des valeurs d’exécution et ne sont pas sérialisés dans une bibliothèque.

AsmJit refuse les appels relatifs à Core non pris en charge avant leur entrée, permettant le repli VM habituel sans rejouer un callback. Les valeurs standard utilisent les protocoles hôte ordinaires de copie et d’égalité ; l’égalité générale n’exige pas de hachage de clé. L’égalité des clés Map/Set exige toujours un protocole de hachage valide.

Les méthodes hôte nommées ordinaires acceptent les surcharges par types de paramètres. Les API script conservent `text::message_key(domain,key)` / `text::message(domain,key,args)` et `format` / `format_with` ; le formatage typé n’appelle pas implicitement le `std::format` positionnel limité aux chaînes.

## Validation

`VeroScriptI18nTests` couvre précision scalaire, données possédées/COW, fournisseurs, versions, publication concurrente, réentrance, annulation/limites de sortie, génériques/conteneurs/Attribute script, héritage des workers, aller-retour bytecode/bibliothèque, VM/Mixed/JIT et ponts de diagnostics. La partie JIT s’exécute avec un backend compatible. `VeroScriptI18nExample` lie Core et un SDK Bytecode VM compatible.

[Exemple SDK i18n](../../../Examples/I18n/README.md) · [API utilisateur](../../User/fr-FR/standard-library/i18n.md).

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [en-US](../en-US/i18n-runtime.md) · [fr-FR](i18n-runtime.md) · [ja-JP](../ja-JP/i18n-runtime.md) · [ko-KR](../ko-KR/i18n-runtime.md) · [zh-CN](../zh-CN/i18n-runtime.md) · [zh-TW](../zh-TW/i18n-runtime.md)
