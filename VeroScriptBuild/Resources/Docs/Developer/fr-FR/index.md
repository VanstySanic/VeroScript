# Maintenance du service Build

## Planification et exécution

`FBuilder::Plan` fige graphe, identités des sources/artefacts, fournisseurs et modèle de lanceur sans exécuter processeurs, fabriques ou sous-processus. Les chemins de `FBuildRequest` sont absolus ; ceux de configuration sont résolus depuis la racine sélectionnée, sans changer le répertoire du processus. `FServices` conserve catalogues, callbacks et identité explicite de l’environnement/outillage. Le `Plan` d’un fournisseur doit rester sans effet d’exécution.

`FBuilder::Execute` revérifie les entrées figées ; leur modification exige un nouveau plan. Déclarez les entrées supplémentaires via `FStepContext::TrackInput` sans remplacer une empreinte existante. Seuls les champs d’entrée déclarés interprètent les références d’artefacts ; les `options` fournisseur restent du JSON opaque. Les clés de cache incluent configuration, contenu amont, identité fournisseur et empreintes des sorties. Un cache personnalisé exige déterminisme et identité d’outillage explicites ; les processeurs dynamiques et compilations sensibles à la confiance/cryptographie sont réexécutés prudemment.

## Publication et politique de cible

Publiez chaque cible atomiquement sur son volume avec un reçu de possession. Ne remplacez jamais des sorties étrangères ou modifiées. L’échec ultérieur d’une cible n’annule pas les cibles indépendantes publiées. Conservez propriétaires d’artefacts et baux de déploiement pendant leur utilisation. `Deploy` et `LeaseDeployment` consomment des environnements figés installés sans repli vers une installation.

Validez les applications pour la plateforme/ABI sélectionnée, pas pour l’hôte de build ; l’exécution native exige encore une validation sur la cible. Les applications sources conservent leurs valeurs portables et les usages runtime+compile, sans chemins de recherche propres au développement. Composants runtime et modèles de bootstrap sont des entrées vérifiées explicites ; changer un modèle invalide plan/cache. Séparez assemblage du contenu, wrappers natifs et signature de plateforme.

Les processus reçoivent exécutable et tableau d’arguments sans shell. Les scripts restreints n’exposent que les aides d’entrée/sortie déclarées avec leurs limites ; fournisseurs/processus natifs de confiance ne constituent pas un bac à sable OS. Vérifiez les signatures des ressources avant transformation. Le rechiffrement décode l’enveloppe existante une fois ; changer du contenu signé exige une nouvelle politique, les copies inchangées conservent leurs signatures. Un échec de vérification interdit la publication ; ne sérialisez pas de clés secrètes dans configuration ou artefacts.

## Intégration et vérification

Maintenez la cohérence entre `Public/VeroScriptBuild.h`, planification/exécution, `Resources/Schemas/veroscript-build.schema.json`, analyse CLI et adaptateurs Application/paquets. Le build racine active `VeroScriptBuildTests` ; `Resources/Examples/hello` et `Resources/Examples/generated` couvrent les sorties applicatives/bibliothèques/distributions et les générateurs. Contrôlez plans périmés, invalidation du cache, baux, sélection de cible, signatures et publication atomique. Les procédures CLI et exemples sont dans le guide utilisateur ; exécution sur plateforme et signature nécessitent une validation propre consignée.

<!-- docs-navigation -->

---

[Guide utilisateur](../../User/fr-FR/index.md) · [Documentation de développement](../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
