# ABI Interop et durée de vie

`VeroScriptInterop` est une ABI de processus compatible C11. Le header public définit des handles opaques pour sessions, valeurs, callables, packages, buffers, contextes d’appel, interception, types hôte génériques et sérialisation externe.

## Sélection du backend VM

`VS_INTEROP_CAP_VM_BACKEND_SELECTION` annonce `vs_interop_session_set_vm_backend` et `vs_interop_session_get_vm_backend_info`. Le produit Interop standard assemble explicitement la VM bytecode statique ; une compilation personnalisée peut utiliser `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF` puis sélectionner une VM externe compatible avec Core. Cette sélection est indépendante de la politique VM/Mixed/JIT. Elle se fait sur le thread créateur, session inactive, avant exécution ; un chemin vide la supprime. Un échec conserve la sélection. Core la fige dès que l’exécution est engagée, y compris en JIT ; `vs_interop_session_clear` ne la déverrouille pas.

Initialiser `struct_size` en sortie. Une requête échouée préserve la sortie. `VS_INTEROP_VM_SELECTED` et `VS_INTEROP_VM_FROZEN` décrivent le domaine ; `VS_INTEROP_VM_CAP_*` décrit les capacités. Les vues texte restent valides jusqu’au remplacement ou à la destruction de la session ; une sélection statique a un `library_path` vide. ApplyConfig préserve la sélection et les extensions de compilation script héritent de la VM de la session.

## Version et statut

La version ABI, version sémantique runtime, capability bits, backend d’exécution, run mode, package kind, value kind et status codes sont des champs distincts. Les fonctions de requête ABI les rapportent séparément : un binding ne doit ni afficher ni comparer une valeur ABI comme si c’était la version produit.

La valeur actuelle de `VS_INTEROP_ABI_VERSION` est `0x00000101` (0.1.1), encodée par `(major << 16) | (minor << 8) | patch` : 16 bits pour la version majeure, puis 8 bits pour la mineure et 8 pour le correctif. Les adaptateurs C# et CPython exigent une égalité exacte ; les bits de capacité et les champs ajoutés en fin de structure selon `struct_size` ne dispensent pas de cette vérification. Examinez les changements d’ABI indépendamment de la version du paquet de chaque produit.

Chaque opération retourne un ABI status lorsque c’est applicable. Le last-error buffer sert aux échecs d’opération ; les diagnostics structurés sont lus par les API de diagnostic de session et formatés uniquement à la frontière de présentation.

## Propriété et durée de vie

Les handles opaques sont des owners explicites ou des références empruntées selon la fonction. Les vues string/byte ne possèdent rien. Les buffers renvoyés par ABI sont détruits par leur fonction de buffer correspondante ou copiés avant libération de leur owner. Les contextes callback et valeurs d’énumération empruntées ne sont valides que pour l’appel documenté.

Le header C est compilé et linké depuis C comme C++ : calling convention, struct layout, enum value, nullability et symbol name appartiennent donc à l’ABI. Même une fonction additive exige synchronisation du header, implementation, tests et adapters.

## Extension de cache JIT de session

`VS_INTEROP_CAP_SESSION_JIT_CACHE` protège les exports de limite/statistique cache. L’extension ajoute `reuse_session_code` à `vs_interop_jit_policy` ; un caller compilé pour le layout original de 32 bytes reste valide et reçoit la réutilisation activée par défaut. Les callers actuels envoient la structure complète et peuvent mettre ce byte à zéro. Cela conserve ABI 0.1.1 tout en offrant un opt-out explicite.

La réutilisation concerne les Run, EvalOnce et appels ordinaires admissibles. Elle conserve l’état compilé, pas les globals VM ni les objets script. Les sessions persistantes peuvent aussi réutiliser le code avec un backend compatible et une révision stable des bindings. Modifier un intercepteur ou un rappel de mutation invalide le code conservé. Le backend VM effectif contourne ce cache. Une exécution contrôlée peut conserver le cache compatible et son identité de gouvernance avec un quota de compilation cumulé. Un budget de tentatives/entrées nul est illimité pour ce compteur ; les plafonds mémoire restent applicables. `vs_interop_session_set_jit_cache_limits` invalide le contexte ; les valeurs par défaut sont 8 Mio/32 Mio et zéro interdit la conservation correspondante. Mixed contrôlé utilise la VM par défaut ; activez `enable_controlled_mixed` après mesure. La sélection explicite JIT est indépendante. Vérifiez `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` et `controlled_native_executions` ; les entrées non prises en charge ou préférées sur VM gardent les contrôles via la VM.

`vs_interop_session_get_jit_cache_stats` rapporte compile attempts cumulés, bytes retenus stabilisés et resource-limit latch du contexte courant. Il se distingue du delta last-run de `vs_interop_session_get_last_jit_stats` et n’est pas une limite process peak-memory. Policy, limits, backend, module et changements pertinents d’observabilité hôte peuvent invalider le contexte et réinitialiser ses statistiques.

## Nouveaux contrats de contexte et channel

Les opérations channel utilisent le contrat du value handle et ne transforment pas une valeur en callback object emprunté. Les opérations blocking, timed et non-blocking ont symboles/résultats distincts ; `close` est le point de synchronisation des waiters. Les hosts ne doivent pas déduire un résultat timeout/closed d’une valeur `VeroScriptStatus` sans rapport.

Les pointeurs de contexte generic host-type, call, interception et external serialization sont empruntés pour toute leur callback. Accessor, mutator, error setter et fonction de continuation/invocation ne sont valides que lorsque cette callback est active. Aucun context pointer, argument/field/byte view ne peut être retenu après retour. Une exception callback ou un échec hôte doit devenir une erreur de contexte avant retour.

Les callable handles disposent d’opérations explicites retain, validity, invoke et destroy. L’invocation owner-thread-only et la capture weak-`this` sont indépendantes et ne rendent pas une session thread-safe. Les serializer bytes et reference visitor values externes doivent être copiés s’ils sont nécessaires après callback. Ces règles s’appliquent aux adapters C, C++ et managés.


<a id="localization-abi"></a>

## ABI de localisation

`vs_interop_locale_translate` alloue un tampon appartenant au module selon l’initialisation normale et la langue du processus. Les octets inconnus, même NUL, sont conservés ; une entrée invalide efface la sortie. Libérez le tampon sur chaque chemin. Alignez l’en-tête C et les exports `.def`, puis vérifiez la validation native et les substitutions dans les six langues. Un échec d’allocation peut conserver un repli anglais.

## Vérifications de publication

Les publications Interop autonomes et complètes compilent et exécutent le test
d’en-tête C11 avec le SDK préparé, en liant chaque fonction publique. Un export
ou une entrée de bibliothèque d’importation manquant bloque le paquet. L’exemple
active ce contrôle avec `VEROSCRIPT_SDK_ABI_TEST_SOURCE`. Les tests de bibliothèque
partagée Linux injectent aussi un échec d’allocation dans le chemin d’erreur de
réflexion sans module chargé : ils exigent `OUT_OF_MEMORY` sans exception
traversant l’ABI C. Le cas ordinaire sans module retourne `ENGINE_ERROR`.

Les tests de frontière couvrent aussi l’échec d’allocation du premier appel de version
(vue vide, nouvel essai réussi), l’échec du remplacement serializer (anciens octets
conservés), le comptage multiset sans allocation et le statut initial du contrôle
mémoire. Les tests étendus déclenchent les callbacks natifs, interception et mutation
depuis les destructeurs de script lors de la destruction finale de la session.
Les données de callback doivent rester vivantes jusqu’à la fin de cette destruction.

Un échec d’allocation de la vue UTF-16 retourne `OUT_OF_MEMORY` sans modifier les
paramètres de sortie ; le nouvel essai préserve toutes les unités de code et les
lectures suivantes réutilisent la vue en cache.

Les callbacks console étendent le préfixe des options de session sans changer la
version de l’ABI C. Seuls les champs finaux complets sont lus ; les anciennes
options restent valides. Query/control décrivent les opérations du terminal et
write reçoit les octets UTF-8 exacts sans saut de ligne ajouté. Les règles de durée
de vie et de threads worker des callbacks existants s’appliquent.

Les régressions de macros utilisent l'ABI C publique pour construire, analyser,
importer et exécuter une bibliothèque réexportant des macros objet, fonction et
variadiques, avec ou sans suppression des informations de débogage. Elles vérifient
les exports conditionnels fixés à la construction, l'expansion chez l'appelant,
l'isolation des macros privées, les positions physiques des erreurs d'importation
et le rejet de `VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` par le lecteur actuel `VSBC`.

Le test du SDK natif installé vérifie le registre gelé, retain/release indépendants,
la conservation par ApplyConfig, le thread créateur et la réentrée de configuration
depuis les processeurs et callbacks de libération. Il génère et appelle une méthode,
vérifie quatre gravités de diagnostic et l'erreur principale, puis désactive la
génération en effaçant le service. Le créateur natif exige le SDK Core C++ correspondant ;
les cinq nouveaux exports C préservent l'ABI 0.1.1 et les dispositions existantes.

Le test couvre aussi la copie et la destruction des objets fonction du service
pendant ApplyConfig : la réentrance de configuration est refusée, un échec
d'allocation renvoie `OUT_OF_MEMORY` sans perdre le service installé, et une nouvelle tentative
réussie efface les erreurs précédentes.

Les régressions refusent aussi les appels de fonction ordinaires, l'exécution et
le remplacement du code source depuis ces callbacks de copie/destruction, en
préservant le programme chargé. Les descripteurs sont validés avant la copie des
objets fonction, afin de préserver l'erreur de validation initiale. L'invocation
par contexte de callback natif reste disponible pour l'exécution imbriquée prise en charge.

Les nouvelles régressions initialisent le résultat avec d'anciens indicateurs de
succès et de sortie, puis vérifient un résultat d'échec neuf et un diagnostic de
session, même avec une sortie NULL. Un callable expiré échoue sans modifier la
valeur de retour. Lors de la destruction synchrone, le handle reste alloué jusqu'à
la fin des callbacks mais la session est marquée fermée : l'accès aux API et une
nouvelle destruction sont refusés sans accès à la mémoire libérée. Le handle est
invalide après le retour de destroy. Les données des callbacks doivent survivre
jusqu'au retour d'un appel actif si celui-ci diffère la finalisation.

`vs_interop_session_retain_callback_user_data` transfère les données d'un protocole
hôte uniquement en cas de succès. La libération a lieu une seule fois après la dernière
référence propriétaire du registre ou d'une valeur, éventuellement après la session
sur un autre thread. Les protocoles ordinaires et génériques suivants partagent ce
propriétaire. Les tests couvrent les valeurs copiées conservées, la libération sur un
autre thread et la reprise d'enregistrement après un échec d'allocation.

## Traitement de compilation général et par scripts

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING` identifie le service natif de `VeroScriptInteropCompileProcessing.h`. `CreateCompileProcessingService` exige un `FCompileProcessorRegistry` figé ; retain/release fournissent des handles opaques indépendants. `vs_interop_session_set_compile_processing_service` installe la configuration, NULL l'efface et ApplyConfig la conserve. Les SDK Core/Interop natifs doivent correspondre ; conserver le code et les données jusqu'à la dernière libération. La session doit être inactive sur son thread créateur ; la réentrée depuis un processeur ou la copie/libération du service est rejetée.

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS` identifie `vs_interop_session_set_script_compile_options`. Fournir le `struct_size` actuel complet. Racines, entrées d'enregistrement facultatives, configuration et répertoires autorisés sont copiés. Les chemins/noms d'entrée refusent NUL ; clés dupliquées, couples pointeur/compte invalides, octets réservés et booléens invalides ne remplacent pas la configuration acceptée. Limites : 64 racines et 4096 paramètres/répertoires. L'entrée vide sélectionne `register_extension`.

NULL restaure les valeurs Core : aucune racine configurée, imports source autorisés, 1 000 000 instructions et 2 000 ms. Les limites explicites nulles sont illimitées. Interdire les imports source permet encore les racines configurées. Overlays, chemins d'import et bibliothèque standard suivent la configuration normale ; les imports standard nécessitent un répertoire correspondant contenant `compiler.vs`. Les extensions utilisent une VM isolée, héritent de l'annulation et sont conservées par ApplyConfig. Les bindings métier ne sont pas exposés implicitement. Les paquets générés n'exécutent pas les extensions productrices ; le JIT exécute le code métier généré.

Les tests C vérifient l'état entre phases, les copies, génération/diagnostics, rejet transactionnel, budgets, annulation et reprise, imports et paquets. Le consommateur natif installé vérifie durée de vie, conservation et réentrée durant traitement/copie/libération.

## Environnements de modules résolus

`VS_INTEROP_CAP_MODULE_ENVIRONMENT` indique la passerelle. Avec les SDK natifs Core/Interop correspondants, créez un `FModuleEnvironment` immuable puis enveloppez-le avec `CreateModuleEnvironment` dans `VeroScriptInteropModuleEnvironment.h`. C ne transmet qu’un handle opaque : `vs_interop_module_environment_retain` crée un autre handle possédé, à libérer une fois. La session configurée conserve indépendamment l’instantané, ses ressources, les propriétaires du code natif et les contrats de dépendance.

`vs_interop_session_apply_config_with_environment(session, config, environment)` applique une configuration complète et sélectionne l’instantané ; NULL le retire. `vs_interop_session_apply_config` conserve la sélection actuelle. Les deux exigent le thread créateur et une session inactive. Appelez Clear sur le programme chargé avant de changer d’environnement. Un conflit avec les liaisons locales renvoie `ENGINE_ERROR` avant toute modification de configuration ou d’annulation. Un catalogue appartenant à l’environnement refuse les modifications locales avec `ENGINE_ERROR` : préparez un catalogue immuable combiné côté natif. Un environnement sans catalogue autorise l’enregistrement C ordinaire.

Chargement, exécution, paquets et JIT externe suivent le contrat Core pour les modules mémoire/fichiers, les ressources de compilation, la localisation et les dépendances externes exactes. La passerelle consomme un environnement déjà résolu ; elle n’installe pas de paquets et ne recherche pas d’extensions natives. Les rappels publiés restent immuables et les binaires des SDK natifs doivent correspondre. Les propriétaires peuvent être libérés pendant la configuration ou la destruction finale ; leurs destructeurs ne doivent pas lever d’exception et la réentrance de configuration/exécution est refusée. Après un échec d’allocation ou interne pendant l’application Core, réappliquez une configuration complète avant de vous fier aux paramètres.

Un refus de résolution du backend renvoie `ENGINE_ERROR` avant la validation de la configuration et de l’état d’annulation. Réessayez avec un environnement valide. Un réglage de rechargement à chaud refusé renvoie également `ENGINE_ERROR`. Si le retour arrière borné reste dans l’état `RESET_REQUIRED`, détruisez et recréez la session avant toute nouvelle exécution.

<a id="interop-regression-targets"></a>

Les contrôles CMake comprennent `VeroScriptInteropTests`, `VeroScriptInteropExtendedTests`, `VeroScriptInteropTypeTests`, `VeroScriptInteropMemoryTests` et le C11 `VeroScriptInteropCHeaderTest`. `VeroScriptInteropNativeSdkTests`, si activé, couvre les consommateurs natifs installés. Alignez les exports réels sur le manifeste ABI et validez l’exemple C11 obtenu séparément contre un SDK extrait. Vérifiez durée de vie des services compilateur, mauvais thread, callbacks et valeurs conservées avec le Core/VM correspondant. Notez résultats et exclusions réels ; la seule présence des sources de test n’est pas une acceptation.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/abi-and-lifetime.md) · [fr-FR](abi-and-lifetime.md) · [ja-JP](../ja-JP/abi-and-lifetime.md) · [ko-KR](../ko-KR/abi-and-lifetime.md) · [zh-CN](../zh-CN/abi-and-lifetime.md) · [zh-TW](../zh-TW/abi-and-lifetime.md)
