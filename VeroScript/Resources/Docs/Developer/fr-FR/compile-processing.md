# Traitement de compilation et métadonnées source

Core fournit des processeurs natifs via `VeroScriptCompileProcessing.h`. Ils transforment les sources, génèrent types et membres, remplacent déclarations ou corps et ajoutent annotations et commentaires. Le code produit passe les contrôles ordinaires de visibilité, types et génériques. Les règles métier appartiennent à l’extension.

## Enregistrement et durée de vie

Enregistrez des `FCompileProcessorDesc` dans `FCompileProcessorRegistry`, appelez Freeze(), puis renseignez `FCompileOptions::CompileProcessing.Registry`. Le descripteur précise identité, version, configuration, ordre, phases, abonnements, opérations autorisées et fabrique. Chaque compilation crée ses propres instances, conservées entre ses phases. Les contextes et vues AST empruntés expirent à la fin du rappel ; la fabrique doit conserver les propriétaires natifs nécessaires.

Un processeur est appelé une fois par événement même si plusieurs abonnements correspondent. L’ordre est Order puis ProcessorId ; il ne rend pas légales des modifications incompatibles. Les annotations inconnues restent des métadonnées. `FAnnotationProcessorRegistry` utilise les mêmes transactions : ajout en Members, corps par défaut en Bodies après liaison des implémentations utilisateur.

## Phases

| Phase | Entrée et opérations |
| --- | --- |
| RawSource | Texte original ; EditSource peut traduire syntaxe et imports |
| ActiveSource | Texte prétraité ; conservation des imports et limites du prétraitement |
| Types | Déclarations ; ajout ou remplacement de types, interfaces, enums et alias |
| Members | Types enregistrés ; membres, fonctions et variables globales |
| Bodies | Implémentations liées ; CompleteBody, ReplaceBody et SetMetadata |

Types précède la liaison des alias et bases. Members précède la liaison des définitions entre fichiers et l’analyse différée des corps. Bodies ne modifie ni disposition ni signature. Les déclarations générées ne déclenchent pas récursivement les processeurs ; les phases suivantes peuvent les consulter.

AddDeclaration utilise FCompileScope (module, namespace, OwnerId éventuel et Snapshot courant), sans TargetId. Les remplacements conservent nature, nom et nombre de paramètres génériques. CompleteBody remplit un corps absent ; méthodes virtuelles pures et déclarations deleted sont exclues, sauf la règle existante du destructeur virtuel pur. Cibles périmées et opérations concurrentes incompatibles sont rejetées avant application.

FCompileMetadata distingue collection omise et collection explicitement vide. Les remplacements préservent la documentation non affectée et retirent les anciens descendants. EditSource utilise des plages d’octets UTF-8 et des correspondances d’origine. ActiveSource ne peut introduire de directives ni modifier la séquence d’imports résolue.

## Métadonnées, cache et paquets

Les métadonnées sérialisées partagent les chemins et identifiants répétés par des références de chaîne propres à chaque instantané. Les valeurs, identités, positions et correspondances de source sont conservées ; la limite encodée reste de 64 MiB, avec une limite distincte de 512 MiB pour les chaînes développées. Le lecteur accepte aussi l’ancienne forme à chaînes littérales. Les empreintes de préparation, le bytecode et les paquets utilisent le même codec ; reconstruisez Core et les backends natifs correspondants lorsque l’identité d’implémentation change.

`VeroScriptSourceMetadata.h` conserve des instantanés immuables : documents, déclarations, commentaires et arbres d’arguments d’annotation, avec doublons, texte et positions. RegisterAnnotation peut fournir une documentation sans comportement. Les objets de réflexion exposent ces informations sans accorder de droits d’exécution supplémentaires.

CommentRetention choisit Full, Documentation ou NoComments. NoComments conserve annotations et descriptions enregistrées ; Unavailable indique l’absence d’information source. Les anciens instantanés restent lisibles après rechargement.

PreparedProgram conserve déclarations, opérations, origines et empreintes. MatchesHost, MatchesProcessing et MatchesDependencies contrôlent la réutilisation ; la clé hôte inclut aussi sources, overlays, macros, imports et options. Un processeur non réutilisable interdit le cache entre tâches. Un AST seul ne reconstitue pas le traitement source ni l’état des processeurs.

Les paquets génériques ouverts conservent modèles transformés, opérations et métadonnées du producteur. Le consommateur rejoue Types, Members et Bodies sans charger les processeurs du producteur. Le code fermé utilise le bytecode ; la substitution générique reste ordinaire. Régénérez les anciens artefacts de développement lorsque le contrat 0.1.1 change.

L’annulation est coopérative : CheckCancelled pour les calculs longs, ReadDependency pour enregistrer ressources présentes et absentes. Les volumes sont limités ; les rappels natifs bloquants exigent une coopération hôte. Interop, outils et AOT consomment le même résultat préparé ; JIT exécute le code métier compilé.

Exemples : [processeurs natifs](../../../Examples/CompileProcessing/main.cpp), [extensions script](script-compile-extensions.md).

## Correspondance des noms

Les noms sont sensibles à la casse par défaut. Avec `annotation_definition.case_sensitive = false`, `@GuiWidget`, `@guiWidget` et `@GUIWIDGET` désignent une seule définition. Les abonnements, `has_annotation`, `find_annotations` et `annotation_info.matches_name(name)` suivent cette règle. `name()` conserve la graphie source ; `definition().name()` donne le nom enregistré et `definition().case_sensitive()` la règle. Les arguments et clés restent sensibles à la casse. `find_annotations` et `matches_name` exigent `@` ; il reste facultatif pour `has_annotation`. Les annotations non enregistrées restent sensibles à la casse.

`FAnnotationDefinition::CaseSensitive`, `FHostBindingRegistry::RegisterAnnotation(..., false)`, JSON `caseSensitive`, C Interop `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` (`name_flags`), C# `CaseSensitive`, Python `case_sensitive`. ASCII; indépendant de la locale. Rejetez les inscriptions actives qui se recouvrent ; les empreintes, le bytecode, les bibliothèques et les génériques conservent cette règle.

## Responsabilités d’intégration

`FCompileResult::PreparedProgram` est le résultat préparé partagé. Session C++ expose `SetCompileProcessing`, les hôtes de génération passent `FCodegenOptions::CompileProcessing`, et les options d’analyse et de protocole Tooling transportent cette configuration. Les hôtes Interop natifs peuvent créer un service opaque avec `CreateCompileProcessingService` dans `VeroScriptInteropCompileProcessing.h`. Ces points d’intégration existent déjà.

Fournissez explicitement registres et propriétaires natifs. Une CLI ordinaire ne découvre pas automatiquement des fabriques natives arbitraires ; les extensions de script et les modules natifs explicitement choisis suivent leurs chemins de chargement configurés. Ne réexécutez pas les processeurs d’un programme déjà préparé et ne recréez pas ses déclarations en analysant les commentaires. JIT exécute le code métier résultant. L’acceptation des plateformes et applications reste distincte.


## Déclarations locales et génération dans les blocs

Les vues de déclaration exposent `ScopeId`, `ParentScopeId`, `bLocal`, `bNested` et leur propriétaire sémantique. `FCompileProcessingContext::Blocks` est une vue empruntée en lecture seule, valable pendant le rappel. En phase Types, utilisez `FCompileScope::ScopeId` et `InsertionBeforeId` pour ajouter un type local ou un alias ; utilisez l’ancre finale pour un bloc vide ou un ajout en fin de bloc. Les champs module/owner conservent leur sens.

Les processeurs script utilisent `context.blocks()` et `context.block_scope(scope_id, before_id)`. Les blocs et ancres proposent des getters (`scope_id()`, `statements()`, `id()`, `range()`) ; les plages source restent des valeurs à champs. Ne fabriquez pas de scope ou de décalage pour contourner la validation du snapshot.

L’ordre des opérations sur une même ancre est déterministe. La visibilité commence au point d’insertion. Remplacer un parent entre en conflit avec une opération sur ses anciennes ancres. Le remplacement d’un corps reconstruit ses déclarations locales. Les descendants générés sont vérifiés sans nouvelle passe de processeurs ; l’instanciation générique substitue le résultat préparé. VSBC et bibliothèques conservent identités, opérations, catalogues et appartenance. Reconstruisez les artefacts de développement après un changement de disposition 0.1.1.

## Lots d’ajout de déclarations et références aux propriétaires

Dans `Private/Processing/VeroScriptDeclarationProcessing.cpp`, le chemin Members `DeclarationAppendsOnly` exige uniquement des opérations `AddDeclaration`, avec `TargetId` et `ScopeId` vides. Des fonctions auxiliaires de namespace sans `OwnerId` peuvent alterner avec des membres de propriétaires class/struct/interface déjà présents dans le snapshot validé. Les contrôles de phase, snapshot, modules modifiables et conflits restent applicables ; un propriétaire nouvellement généré ne devient pas une cible de l’ancien snapshot.

`ReuseOwners` emprunte ce snapshot tant que les adresses de stockage de `P.Classes`, `P.Structs` et `P.Interfaces` ne changent pas. Le corps d’un auxiliaire généré peut ajouter des types locaux et agrandir ces conteneurs ; après un changement de stockage, reconstruisez les références avec `Collect(P)`. Ne conservez pas de pointeurs de déclaration/AST au-delà de cette croissance. Vérifiez auxiliaires et membres intercalés, propriétaires génériques, appels virtuels/interfaces et déclarations locales ; `FInterleavedAppendGenerator` dans `Private/Tests/VeroScriptCompileProcessingTests.cpp` fournit des scénarios source, sans établir un résultat de test.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Language: [en-US](../en-US/compile-processing.md) · [fr-FR](compile-processing.md) · [ja-JP](../ja-JP/compile-processing.md) · [ko-KR](../ko-KR/compile-processing.md) · [zh-CN](../zh-CN/compile-processing.md) · [zh-TW](../zh-TW/compile-processing.md)
