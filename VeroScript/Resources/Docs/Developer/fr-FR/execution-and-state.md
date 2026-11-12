# Exécution, état et paquets du cœur

[Backends VM remplaçables](vm-backends.md)

## État VM

Voir [Backends VM remplaçables](vm-backends.md#vm-provider-assembly).

## Métadonnées bytecode et flot de contrôle

Le bytecode porte les métadonnées nécessaires au comportement source et paquet : indicateurs de paramètre référence, noms/types de captures de fermeture, état de fonction declaration-only ou abstract, tables d’interface et enum, propriété du fichier debug, régions d’exception, plages `finally`, plages d’initialiseur global et propriété de composant de paquet. Les opcodes de référence, casts vérifiés ou optionnels, `throw` et nettoyage de région protégée sont des opérations sémantiques, non des optimisations VM interchangeables. Sérialisation, hot reload, réflexion et outils conservent les métadonnées alignées avec les tables exécutables.

## Workers et synchronisation

Les globales de script et champs statiques de classe sont isolés par worker natif. Le registre de tâches et ses ID sont aussi locaux au worker. `channel<T>` est la capacité explicite et synchronisée inter-worker ; callbacks hôte et état natif accessible exigent une synchronisation fournie par l’hôte. Un worker ne traite pas l’ID de tâche d’un autre worker comme local.

## Paquets et rechargement

Le chargement de paquet valide les métadonnées du module et la forme d’entrée application/bibliothèque. Le rechargement de session est opt-in et prépare un remplacement avant commit. Les reload fichier, module compilé et paquet ont des entrées distinctes. Un reload échoué conserve le module fonctionnel précédent et expose un diagnostic structuré.

## Débogage et diagnostics

Positions debug, stack frames, variables, breakpoints, évaluation et raisons d’arrêt sont des contrats de données publics consommés par Tooling et DAP. Les ID de diagnostic, plages source et arguments de substitution sont des données machine stables ; les messages localisés relèvent de la présentation.

## Gouvernance d’exécution

`FExecutionLimits` contient limite d’instructions et timeout wall-clock ; zéro signifie illimité pour ce champ. `FExecutionControl` est un objet de contrôle partagé et thread-safe. L’hôte peut appeler `RequestCancel` pendant que `Run` est actif ; la VM observe la requête aux points coopératifs. L’annulation ne force jamais l’arrêt d’un callback natif ou managé déjà en cours, donc l’état callback garde ses exigences ordinaires de synchronisation.

`FScriptCapabilities` est la politique des effets de bord standard library : fichier, environnement, console et tâche peuvent être restreints. La configuration par défaut est permissive ; lorsqu’un hôte impose des restrictions, le chemin les applique avant toute entrée JIT. C’est une politique de sécurité, pas un simple indice d’optimisation. `FEngineConfig` transporte limites, contrôle et capacités avec les overlays source et le `EExecutionBackend` demandé.

Limites actives, contrôle d’annulation et capacités restreintes utilisent au besoin le chemin interpréteur afin de garantir leur application. Le débogage, les plages d’initialisation conservées et le nettoyage borné utilisent le backend effectif `Vm` ; le backend demandé reste une valeur de configuration et ne doit pas être signalé comme backend effectif dans les diagnostics.

## Nettoyage réentrant et transferts d’exception

La libération d’une valeur peut appeler un destructeur de script qui réentre dans l’exécution et déplace le stockage des cadres. Conservez les indices de cadre à travers ces appels et récupérez ensuite les références. Les lectures d’opérandes du bytecode restent au-dessus de `StackBase` du cadre courant ; le nettoyage entre cadres libère explicitement les opérandes sauvegardés.

Un retour ou une exception en attente appartient à la continuation `finally` englobante. Les boucles et exceptions capturées localement dans `finally` préservent ce transfert ; une sortie de région libère les valeurs remplacées selon le modèle de propriété normal. Une exception de nettoyage non capturée peut remplacer l’exception de script propagée, tandis qu’une exception capturée dans un destructeur doit préserver l’exception extérieure. Un échec et la réutilisation de la VM doivent libérer les objets d’exception en attente. Vérifiez ces cas dans `VeroScriptExceptionTests`, y compris avec debugger et limite d’instructions ; l’exécution gouvernée doit compter et vérifier `throw` séparément.

## Services numériques

`VeroScriptNumerics.h` fournit les valeurs immuables `FBigInteger`/`FRational` et les opérations transactionnelles noexcept `Numerics::Try*`. `FNumericError` contient des métadonnées fixes. Copie, déplacement, échange et destruction n’allouent rien ; une source déplacée devient zéro. Les valeurs peuvent être partagées entre threads ; les contextes et rappels doivent être synchronisés par l’hôte.

`FNumericOperationContext` associe `FNumericLimits` à un rappel de travail emprunté et synchrone. Zéro sélectionne une limite finie : 65 536 bits (maximum absolu 1 048 576), entrées/sorties de 1 MiB, scratch de 16 MiB et 16 777 216 unités par opération. Le scratch mesure les demandes de stockage temporaire et les sorties en attente, pas le tas VM ni le RSS. Une erreur préserve les sorties.

Configurez avant exécution avec `VeroScriptVm::SetNumericLimits`, `FEngineConfig::NumericLimits` ou `VeroScriptSession::SetNumericLimits`. Les workers héritent des limites ; instructions, délai et annulation couvrent aussi les initialisations globales. `VeroScriptNumericBindings.h` fournit les échanges FValue vérifiés. C Interop expose `VS_INTEROP_CAP_NUMERICS`, `vs_interop_numeric_parse/to_text/convert`, la vérification du type et les limites de session ; les chaînes gardent leur longueur explicite. Le mapping BigInteger/Fraction propre à chaque langage n’est pas automatique. AOT autonome refuse ces opérations.

Les hooks utilisent `Packages::FLimits::HookNumericLimits` avant initialisation. Aucun changement au format des paquets, au protocole registre ou aux nombres JSON.

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

Voir [SDK d'interopérabilité — variant](../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#exported-value-lifetime).

La VM valide `FVariantTypeDescriptor` avant le boxing. `Variant::Box` et `Variant::Snapshot` réutilisent la copie habituelle sans exposer de charge utile modifiable. `VisitValueGraph` parcourt boîtes, objets, fermetures et conteneurs ; aucune finalisation de script ne tourne sur un thread de finaliseur. Réserver les emplacements de résultat avant les rappels ou copies, puis valider sans allocation. Le contexte extérieur nettoie les valeurs retirées après exécution. Une fermeture demandée depuis un rappel attend la fin de l’appel actif.

La VM fournit toute la sémantique. Mixed/asmjit revient à la VM pour les instructions non prises en charge. Le bytecode portable et les paquets conservent les déclarations et instructions. Les AOT autonomes C++, C# et Python refusent explicitement les dépendances variant avant toute sortie. Le débogueur expose $present, $type et $value en lecture seule ; type<variant>().kind() renvoie "variant".
Les points sûrs protègent les arguments empruntés et le récepteur pendant l’exécution. À la sortie, les valeurs retournées, les objets construits et les résultats des arguments modifiables restent en vie jusqu’à leur rétention par l’appelant ; les emprunts d’entrée seule se terminent avec l’appel. Un échec du nettoyage des valeurs retirées transforme un appel réussi en échec d’exécution ; une erreur d’exécution existante reste prioritaire. À la fermeture, les échecs de nettoyage sont signalés dans le flux de diagnostic de l’hôte.

## Racines des rappels natifs asynchrones

Un appel native actif conserve les fonctions et captures avec `IScriptCallInvoker::RetainForGeneration`. Conserver le `FValueRoot`, jamais les objets empruntés invoker, VM ou Args. Le retrait est possible sur un autre thread ; la lecture et le nettoyage du langage restent sur le thread propriétaire. Les exports ordinaires et ces racines protègent le même graphe.

Les points sûrs native nettoient ces racines ; `DrainGeneration` préserve les racines des emprunts ordinaires. Les destructeurs partagent le budget, l’annulation et l’échéance absolue de l’exécution. Une Session persistante conserve sa génération ; la fin d’un appel VM autonome, le remplacement, le rechargement et la fermeture l’invalident. Les exports ordinaires bloquent toujours le remplacement. Un échec de compilation conserve l’ancienne génération ; après le début de migration, le rollback ne rétablit pas les racines invalidées.

## Caches de préparation et services JIT

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint` autorise une lecture anticipée du cache uniquement si l'entrée analysée ne nécessite ni processeur, ni rejeu de paquet, ni validation des liaisons hôtes. Combiner cette empreinte avec les empreintes complètes des sources et de la résolution du chargeur, ainsi qu'avec l'identité du compilateur. Sinon, conserver la préparation et la validation des dépendances normales. Les métadonnées source et les limites effectives d'évaluation constante participent à cette identité.

`ICompiledFunctionServices::ExecuteBlock` réduit le coût des passages entre le code natif et Core pour une séquence bornée. Chaque instruction conserve ses vérifications de budget, de délai, d'annulation et d'autorisations ; un branchement, un retour ou un échec termine la séquence. La politique Mixed contrôlée par défaut reste inchangée.

Les caches JIT empruntés, non conservés et des tâches isolées préparent d'abord l'entrée ; les fonctions appelées restent compilables. Un cache conservé ne réutilise une préparation terminée que pour les mêmes exigences d'exécution. Les chemins non pris en charge ou non rentables utilisent la VM.

Ces changements de services C++ imposent de reconstruire Core, les backends et les consommateurs SDK à partir de sources correspondantes.

L’entrée du module complet conserve le budget de compilation anticipée configuré.

`FJitCompiledFunction::bPreferVmForExecution` permet au backend de publier une préférence VM indépendante des arguments. Core applique cette préférence en cache avant d’activer les services du backend ; les statistiques de repli, les vérifications VM normales et la sélection indépendante des fonctions appelées sont conservées.

## Nettoyage borné après interruption

Voir [Intégration hôte — Nettoyage borné après interruption](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md#bounded-interruption-cleanup).

## Propriété du cache de compatibilité des types

`VeroScriptVm::IsActiveObjectTypeAssignableTo` mémorise la compatibilité des noms canoniques exacts de classes/interfaces par module et `ProgramGeneration`. Chaque contexte possède son cache, vidé au changement de module/génération ou à la limite de 4096 entrées. Un programme immuable possédé autorise la réutilisation hors appel actif. Un programme emprunté ne mémorise que pendant l’exécution et est vidé entre appels dans `BeginExecutionGovernance` ; alias et noms non canoniques gardent la résolution de métadonnées faisant autorité. Préservez propriété et invalidation lors des optimisations JIT, copies ou requêtes hôtes.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/execution-and-state.md) · [fr-FR](execution-and-state.md) · [ja-JP](../ja-JP/execution-and-state.md) · [ko-KR](../ko-KR/execution-and-state.md) · [zh-CN](../zh-CN/execution-and-state.md) · [zh-TW](../zh-TW/execution-and-state.md)
