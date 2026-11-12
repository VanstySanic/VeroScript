# Architecture centrale et pipeline de compilation

[Backends VM remplaçables](vm-backends.md)

L’identifiant du bytecode est fixe : `VSBC`. La version du format suit celle du produit, actuellement `0.1.1`. Régénérez les artefacts des anciennes versions de développement.

Le cœur possède l’implémentation du langage : lexer, parser, AST, préprocesseur, compilateur, bytecode, VM, génération de code, JIT, modules, paquets, débogage, liaisons hôte, réflexion, sérialisation, concurrence, mutation et localisation.

## Pipeline

La compilation commence par la résolution des sources, overlays, imports et paquets. L’analyse lexicale et le prétraitement conservent les positions source ; viennent ensuite l’AST public, les vérifications sémantiques/de type/de génériques/de visibilité, le bytecode avec informations de débogage, puis le module ou paquet exécuté par la VM ou le JIT. Chaque étape produit des diagnostics structurés avec code, message, position et token/déclaration associés. Une ligne d’erreur écrite dans un flux ne remplace pas l’objet résultat.

## Modules, imports et macros

`VeroScriptModuleLoader` résout le module principal, les imports ordinaires et implicites, les modules stdlib ainsi que les graphes de modules de paquet/modèle avant de fusionner le programme. Les chemins d’import et racines stdlib sont normalisés et dédupliqués. Les overlays source remplacent les lectures disque par chemin normalisé, afin qu’un éditeur ou hôte Web compile un graphe mémoire sans écrire de fichiers.

Les définitions `-D` du préprocesseur et macros prédéfinies structurées sont normalisées avant d’influencer l’empreinte source. Les noms de type hôte sont fournis avant l’analyse, afin qu’une déclaration telle que `Counter c = Counter(1);` soit classée avec le catalogue de bindings hôte. Les imports circulaires et l’accès à l’ensemble du projet restent des réglages explicites du loader, et non un état global caché du compilateur.

Lorsqu’une racine stdlib ou une ressource de module est introduite, le loader peut fusionner l’arbre voisin `localization/`. La fusion est indexée par ID de catalogue canonique et conserve le repli `en-US`; un paquet de ressources doit donc déployer ensemble ses répertoires stdlib et localisation.

## Frontières sémantiques

`Public/` contient les en-têtes pris en charge. `Private/` contient compilateur, parser, VM, JIT et structures internes. Les tests d’en-tête public garantissent que les en-têtes installés ne dépendent pas de fichiers privés absents. Les outils et adaptateurs externes utilisent les contrats publics. Une cible interne qui a réellement besoin de l’AST du compilateur ou du runtime reçoit explicitement un chemin privé via CMake et reste une dépendance friend revue, jamais un consommateur du SDK.

## Déclarations statiques, génériques et captures

Les champs statiques sont enregistrés comme globales possédées par le module et initialisés durant l’initialisation du module. Un accès statique peut utiliser un receiver de type ou un receiver objet évalué ; dans le second cas, le receiver est toujours évalué pour ses effets avant lecture de la globale de support. Les instanciations de type générique substituent le type de champ et enregistrent indépendamment leur stockage statique.

Le compilateur enregistre dans l’AST les captures lambda et bornes génériques, puis porte leurs positions source dans les diagnostics bytecode. Restrictions de type statique, validité de capture, bornes d’argument de type et règles d’héritage sont des sémantiques du compilateur. Un frontend, émetteur AOT ou outil doit utiliser l’identité de déclaration enregistrée, jamais reconstruire ces règles depuis un nom affiché.

## Contrôle d’analyse par portée et métadonnées des dépendances

`FAnalysisControlScope` possède les callbacks d’annulation et de mesure pour l’analyse synchrone sur le thread appelant. Les portées imbriquées restaurent le contrôle précédent ; les objets capturés doivent rester valides pendant la portée et chaque `FAnalysisStage` doit se terminer avant elle. `CheckAnalysisCancellation()` lève `FAnalysisCancelled` lorsque le prédicat renvoie true, indépendamment des diagnostics de syntaxe. Cela n’annule pas l’exécution de la VM. Un lexer annulé peut recommencer sans conserver un flux partiel.

`FAnalysisStage` rapporte les millisecondes écoulées sur le même thread ; son nom doit rester valide jusqu’à sa destruction. Les exceptions des observateurs sont ignorées pour ne pas remplacer une exception d’annulation ou d’analyse pendant le déroulement de pile. Les temps imbriqués se recouvrent et ne doivent pas être additionnés comme des travaux indépendants.

`VeroScriptModuleLoader::GetLoadedFileStamps()` expose la date de modification et la taille relevées avant les lectures physiques de sources ou de packages. Les entrées en mémoire et les overlays n’ont pas d’empreinte disque. La référence reste valide jusqu’au prochain chargement ou à la destruction du chargeur. Un cache doit rejeter `bValid == false` et revérifier ces métadonnées avant de réutiliser un AST, y compris pour les modifications pendant l’analyse. Ce ne sont pas des hachages des octets ; les versions, options et overlays exigent leur propre invalidation.

## Longueurs et indices

Les longueurs et indices linéaires utilisent `int64`. Le stockage natif reste en `size_t` ; les conversions vérifient les limites du script et de la plateforme avant toute réduction. Les conteneurs C# conservent les limites du CLR. Le marqueur de disposition du bytecode est `0x43425356` (`VSBC`). Les anciens caches utilisant des longueurs sur 32 bits doivent être recompilés depuis les sources. Reconstruisez les hôtes C++ et les sorties AOT avec les en-têtes et la bibliothèque standard mis à jour.

## Sélection du backend

<a id="core-vm-ownership"></a>

Core possède les programmes, valeurs, cadres logiques, opérations du langage, contrôles et dispatch. La boucle complète appartient à VeroScriptBytecodeVm dans Modules/Execution/VeroScriptBytecodeVm. Son ID est vs-bytecode ; son paquet est vs-bytecode-vm. Core ne sélectionne implicitement ni VM ni JIT. Runtime assemble explicitement la bibliothèque dynamique officielle dans Vm/ ; les produits statiques Interop, Web et mobiles injectent également leur VM explicitement. JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## Macros publiques

Voir [Préprocesseur — Macros publiques](../../User/fr-FR/language-basics/preprocessor.md#exported-macro-contract).

VSBC 0.1.1 ajoute aussi une table validée de macros publiques. Les caches et bibliothèques `.vslib` la conservent ; `.vsapp` écrit une table vide. Régénérez également les anciens paquets binaires. Le prétraitement suspend chaque import émis jusqu’à disponibilité de l’interface ; la préparation utilise une pile bornée sur le tas.

## Traitement des annotations à la compilation

Le registre natif figé est transmis par `FCompileOptions.AnnotationProcessing`, C++ Engine/Session, Tooling ou `FCodegenOptions`. `@Name` équivaut à `@Name()` ; les drapeaux ordonnés, les arguments scalaires nommés et les groupes imbriqués sont conservés. Les annotations inconnues restent des métadonnées. Une seule étape lie les définitions utilisateur avant de produire les corps et méthodes auxiliaires ; les vérifications ordinaires, la répartition virtuelle et la substitution générique suivent. Les définitions dans un autre fichier exigent le chargement des modules de déclaration et d’implémentation. Les paquets conservent les membres générés et les modèles ouverts sans nécessiter le processeur producteur. Tooling expose des `GeneratedDocuments` en lecture seule ; les outils intégrés affichent ces documents virtuels. Les outils autonomes ne chargent pas automatiquement les processeurs natifs, enregistrés par l’hôte. C# et Python attachent un handle de service natif via Interop. VSBC 0.1.1 conserve les annotations structurées et les opérations de génération ; régénérez les anciens caches et paquets binaires. L’exemple natif se trouve dans `VeroScript/Resources/Examples/AnnotationProcessing`.

Le test CLI couvre deux bibliothèques, leurs spécialisations et une application sans informations de débogage.

## Surcharges hôte

Voir [Liaisons hôtes et catalogues de liaisons — Surcharges hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md#host-overload-resolution).

## Services communs des modèles

`VeroScriptTemplate.h` fournit le schéma ordonné Type/Value, les constantes typées exactes, l’évaluation pure bornée, le codage des arguments et les instantanés de déclarations/instances. Le compilateur possède les instances sans modifier l’AST d’entrée. Les valeurs par défaut sont liées au contexte de déclaration. L’identité inclut les arguments englobants et propres ; VM, JIT et Core AOT utilisent le bytecode concret.

`ModuleMeta::EnumerateTemplateDeclarations` renvoie des instantanés autonomes sans exécuter les corps. `TemplateInstance` sur les fonctions et types distingue les arguments propres et englobants. Les types utilisent leur nom canonique ; les valeurs utilisent un codage exact `@value:` à décoder avec `DecodeTemplateArgument` après retrait du préfixe, sans passage par double. L’instantané vit avec son propriétaire ; les vues de traitement ne sont valides que pendant le rappel.

`TemplateParameters` natif et `declaration_info.template_parameters` du script partagent les catégories, défauts et indicateurs englobants. Les bibliothèques conservent les méthodes ouvertes des types exportés ordinaires et leurs dépendances privées. Charger une application fermée ne lance pas de processeur. Les identités internes de disposition/construction changent, mais le produit et VSBC restent en 0.1.1. Recompilez les consommateurs d’AST avec le même Core. Les exporteurs source indépendants diagnostiquent les modèles non pris en charge avant la sortie.

`FTemplateCallTypeQuery` dans `VeroScriptTemplateQuery.h` interroge les signatures du programme préparé immuable avec la déduction, les défauts et la sélection des surcharges du Core. Aucun corps, processeur ou appel natif ne s’exécute. L’appelant filtre visibilité et accès. Le nom retourné sert à la recherche, pas à l’affichage ni à une identité sérialisée. Programme et liaisons doivent survivre à la requête ; chaque instance appartient à un seul thread d’analyse et propage l’annulation.

## Limites des optimisations à l’exécution

Les plans bornés d’appels entiers exigent une exécution sans débogage, rechargement à chaud ni gouvernance et respectent la profondeur d’appel. Arguments et captures par valeur doivent être int. Un essai de champs entiers qui échoue ne valide aucune écriture et rend la main à VM, préservant les effets partiels des erreurs. Les observateurs de mutations désactivent ces plans de champs. Les plans restent liés au module et à sa génération ; les caches des modules empruntés sont vidés aux limites d’exécution. BigInt calcule son hash à la demande et le met en cache atomiquement dans sa charge immuable ; les copies conservent des enveloppes distinctes et les protocoles personnalisés.

Les bases hôtes de FScriptObject utilisent GetHostBase()/SetHostBase(). Les objets ordinaires n’allouent aucun état auxiliaire hôte/de construction par copie ; les copies détachent cet état avant modification. Recompiler les consommateurs C++ SDK et les backends JIT avec les en-têtes et l’identité Core correspondants ; l’ABI C reste identique.

L’identité de type des objets utilise `FScriptObject::GetTypeName()/SetTypeName()`. Un cache de noms borné partage les noms immuables, qui survivent à la VM et au module. La référence empruntée expire quand cet objet change de nom de type ; ce changement invalide son indice de disposition. Les schémas de signature restent valides pendant la réentrée et sont vérifiés par module, génération et signature réelle ; les contrôles des arguments, références et retours restent inchangés.

## Valeurs de champs et préparation des dépendances statiques

`RegisterStaticMethodDeclarations` publie les signatures avant les dispositions des champs ordinaires. Les structs/classes/interfaces génériques fermés enregistrent le stockage statique, publient descripteurs et méthodes, puis `CompileOwnFieldDefaults` lie les valeurs par instance. Préservez espace de noms, accès, substitutions de type/valeur et métadonnées ; retrouvez les descripteurs par indice après les callbacks pouvant agrandir les tables. La résolution du type propre utilise l’identité lexicale sans perdre arguments englobants explicites, racine ni valeurs locales.

`FStaticInitializationDeferral` empêche le code d’initialisation du module d’entrer dans un tampon par instance. `bStaticFieldInitializationStarted` sépare disposition précoce et phase statique ; `PendingGenericStaticFieldInits` garde les types en attente. `FlushPendingGenericStaticFieldInitializers(RequiredType)` retire seulement le type demandé et conserve l’ordre des autres. Le code d’initialisation est émis une seule fois par globale de stockage, sans être inclus dans chaque valeur de champ ni réémis pour chaque lecture statique à l’exécution.

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp` prépare les dépendances depuis le bytecode abaissé et détaché de l’initialiseur. `PrepareStaticInitializationDependencies` suit appels/constructeurs choisis, valeurs de champs, copies et destructions locales ; provenance des callables, captures et écritures par référence suivent les révisions d’état. Fermetures stockées, allocations retenues, champs nullables et conteneurs vides ne doivent pas ajouter des dépendances d’exécution/destruction par leur seul type. Les callbacks inconnus ne forment pas un graphe exhaustif et l’analyse n’exécute pas les effets natifs.

Compilez l’expression consommatrice dans un tampon temporaire, préparez ses dépendances puis ajoutez-la avec les positions de débogage et plages d’initialisation correspondantes. Gardez les initialisations génériques requises hors des branches de court-circuit et tampons de champs. L’analyse vérifie l’annulation, copie les descripteurs avant préparation récursive et utilise des gardes actives/de révision sans exécuter le code métier. Préservez l’ordre normal des initialisations indépendantes, sans promettre une résolution arbitraire des cycles.

Contrôlez méthodes statiques déclarées plus loin, types propres imbriqués, alias, accès const/ref, constructeurs choisis, écritures de callables/références, vies des copies/destructeurs et types indépendants. Vérifiez séparément VM Core, JIT choisi et Core AOT ; l’export autonome suit un autre contrat. Les scénarios sont dans `Private/Tests/VeroScriptStaticMemberTests.cpp`, la suite différentielle des backends et la suite C++ Core AOT. Leur présence ne prouve pas leur réussite.

## Implémentation de la conversion décimale

`Shared/Private/VeroScriptFloatParsing.h` partage `FromCharsDecimal` entre parseur et services de texte scalaire. Détectez le support flottant de `std::from_chars` ; le repli convertit directement à la largeur cible, borne le préfixe significatif avec un chiffre témoin, utilise la locale numérique C et l’arrondi au plus proche, puis restaure l’état appelant. Les subnormaux non nuls sont valides ; un texte non nul arrondi à zéro échoue hors plage sans affecter la sortie. `ParserDetail::ParseFloatLiteral` conserve séparément l’exception des littéraux `f` encore représentables en double. Distinguez cas de milieu, zéro négatif, longue mantisse, locale et sortie préservée ; leur présence ne valide pas une plateforme.

Le JSON des catalogues de liaisons Core et le JSON LSP/DAP de Tooling utilisent aussi cette conversion. `vs_use_shared_private_headers` fournit les chemins privés ; cet en-tête partagé n’est pas une API SDK publique.

`Shared/Private/VeroScriptJsonString.h` fournit l’échappement des chaînes JSON aux catalogues de liaisons, à Interop et aux outils de langage natifs sans dépendre du runtime Core. L’opération d’ajout écrit le contenu sans guillemets externes ni validation UTF-8 ; les appelants conservent leur style d’échappement des caractères de contrôle et fournissent une entrée distincte du tampon de sortie. Cette entrée de compilation privée doit accompagner les distributions source et figurer dans l’empreinte d’implémentation de chaque consommateur.

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h` fournit `ToCharsDecimal` et `ToCharsGeneral` à la sérialisation des macros de paquets, à l’i18n applicative, au JSON Tooling, à Json et aux contrôles numériques GUI. Utilisez la surcharge flottante `std::to_chars` lorsqu’elle existe ; le chemin de compatibilité utilise la locale numérique C, conserve la largeur float/double pour choisir une représentation avec aller-retour exact et restaure errno et le mode d’arrondi. Ce support privé n’est pas une API SDK publique. Incluez-le dans les empreintes Core/AOT cible, des bindings Json, de compilation GUI et dans les distributions source ; vérifiez le repli séparément de charconv.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/architecture-and-pipeline.md) · [fr-FR](architecture-and-pipeline.md) · [ja-JP](../ja-JP/architecture-and-pipeline.md) · [ko-KR](../ko-KR/architecture-and-pipeline.md) · [zh-CN](../zh-CN/architecture-and-pipeline.md) · [zh-TW](../zh-TW/architecture-and-pipeline.md)
