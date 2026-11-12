# Backend JIT optionnel

## Sélection et compatibilité

AsmJit est un backend JIT facultatif, distribué séparément de Runtime. Choisissez la stratégie d’exécution indépendamment du backend. La [référence des moteurs CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md#cli-backend-policy) définit l’héritage du projet, `auto`, `none`, les ID/chemins explicites et le repli. Ce guide traite du paquet AsmJit, de la compatibilité native et du chargement manuel.

Avec un JIT compatible installé et activé dans `MyProject`, ces commandes utilisent respectivement le choix hérité et la sélection automatique explicite :

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

Pour un déploiement manuel depuis une archive portable : Téléchargez le paquet fonctionnel depuis `Release/Modules/Execution/Jit/<target>/<version>/archives/` et extrayez-le séparément du Runtime. Aucun installateur autonome n’est fourni ; sélectionnez explicitement la bibliothèque ou le répertoire extrait. Un numéro de version identique ne garantit pas la compatibilité : Core, architecture, compilateur, bibliothèque standard et configuration runtime doivent correspondre à l’hôte. Un backend portable peut optimiser sans émettre de code machine natif.

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## Chemins et remplacement

Ces règles du chargeur fichier/répertoire concernent les chemins explicites, distincts de la sélection par identifiant ou automatique de l’environnement de modules. Un fichier fixe un module précis. Un répertoire est parcouru à sa racine et jusqu’à deux niveaux enfants, dans l’ordre lexical UTF-8 ; le premier candidat compatible utilisable gagne. Les noms commencent par `VeroScriptJit` sous Windows et `libVeroScriptJit` sous Linux/macOS. La limite est 4096 candidats. Les entrées enfants illisibles sont ignorées. Les chemins relatifs utilisent le répertoire courant. Aucune variable d’environnement, registre ou recherche implicite ne sélectionne le backend. Changez-le uniquement au repos ; un échec conserve la sélection précédente. Un chemin vide dans les API d’intégration le désactive.

## API d’intégration

C++ utilise `SetJitBackendPath` ; C utilise `vs_interop_session_set_jit_backend` et `vs_interop_session_get_jit_backend_info` ; C# utilise `SetJitBackend` et `GetJitBackendInfo` ; Python utilise `set_jit_backend` et `get_jit_backend_info`. Choisissez séparément la stratégie. Le module reste chargé tant que fabriques, instances, workers ou artefacts exécutables conservés en ont besoin.

Le nom officiel du paquet et le préfixe des archives sont `vs-asmjit` (par exemple `vs-asmjit-0.1.1-win-x64.zip`). L’identifiant du backend utilisé pour la sélection par nom reste `asmjit`.

## Exécution native contrôlée et AOT statique

<a id="controlled-native-execution"></a>

Core garde les limites, l’annulation, les permissions, les cadres et les valeurs. Les fonctions incompatibles passent à VM avant leur entrée ; aucune erreur ne rejoue une fonction. Le JIT Mixed contrôlé exige `FJitPolicy::bEnableControlledMixed` (C ABI `enable_controlled_mixed`, C# `EnableControlledMixed`, Python `enable_controlled_mixed`) : cette première version peut être plus lente que VM. Jit explicite reste indépendant. `ControlledNativeExecutions` / `AotExecutions` comptent les entrées natives réelles. La validation SDK et appareils Android/iOS reste à effectuer ; les tests de bureau ne suffisent pas.

Voir [Intégration mobile expérimentale — Exécution native contrôlée et AOT statique](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md#mobile-jit-selection).

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
