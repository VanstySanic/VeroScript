# SDK C++

Le SDK C++ est l’interface d’intégration native de VeroScript. Il regroupe
l’API d’exécution embarquée et les outils de source orientés C++. L’ABI C
utilisée par Python, C#, Rust et d’autres adaptateurs est décrite dans le
[SDK Interop](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md).

## Choisir un module

| Module | Utilisation |
| --- | --- |
| [Intégration](embedding.md) | Vérifier, charger, exécuter, appeler, recharger et configurer VeroScript depuis C++. |
| [Bindings hôte](bindings.md) | Décrire et enregistrer les fonctions natives, types hôte, membres, opérateurs et types hôte génériques. |
| [AOT](aot.md) | Générer du C++17 à partir de VeroScript pour une construction native. |
| [Frontend C++](frontend.md) | Analyser le sous-ensemble C++17 pris en charge dans l’AST public partagé. |
| [BindGen](bindgen.md) | Générer un catalogue de bindings et des références d’API à partir de déclarations C++. |
| [Transpiler](transpiler.md) | Convertir le sous-ensemble C++17 pris en charge en source VeroScript et en cartes source. |

Le frontend est partagé par BindGen et Transpiler. Il accepte volontairement
un sous-ensemble borné et échoue avec des diagnostics lorsqu’une construction
ne peut pas conserver sa sémantique. Aucun de ces outils n’est un compilateur
C++ général.

## Ordre d’intégration

<a id="explicit-vm-selection"></a>

Avant l’exécution, sélectionnez une VM compatible via `FEngineConfig::VmBackendFactory`/`VmBackendPath` ou l’API de Session. Lier le SDK C++ seul ne sélectionne pas d’interpréteur. Les processeurs de compilation script nécessitent aussi une VM. Consultez [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/fr-FR/index.md) pour les cibles SDK et la configuration.

1. Choisissez `VeroScriptEngine` pour un usage ponctuel ou
   `VeroScriptSession` pour un module et un état persistants.
2. Enregistrez les fonctions et types hôte avant de charger le module.
3. Exécutez `CheckSource`/`CheckFile` avant `RunSource`/`RunFile` lorsque les
   erreurs de validation doivent être présentées séparément.
4. Exportez et validez le catalogue lorsque l’interface est consommée par un
   IDE, un LSP, un paquet ou un vérificateur statique.
5. Synchronisez l’état possédé par l’hôte : les callbacks et les workers peuvent
   s’exécuter en parallèle.

Les globals de script et les champs statiques de classe sont isolés par thread
natif. `channel<T>` est le mécanisme explicite de partage synchronisé. Les
callbacks natifs, intercepteurs d’instances, observateurs de mutation et
symboles hôte AOT peuvent être appelés depuis des workers concurrents.

## Mémoire gérée

API hôte : `VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`. La référence [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md) décrit la propriété, les quotas, l'expiration des vues et les emprunts.

## Langue des diagnostics des outils source

Le frontend C++ et les outils C++ BindGen/Transpiler émettent actuellement leur aide, leurs erreurs d’arguments et leurs diagnostics de déclaration en anglais. Les CLI BindGen/Transpiler n’acceptent pas `--lang` ; le choix de langue du runtime ne traduit pas ces messages. Les codes et positions source restent utilisables par les applications.

## Environnement de modules et backends

Si l’environnement fourni possède des résolveurs d’exécution, Engine/Session peut hériter de ses choix automatiques. Les fabriques et fichiers/dossiers explicites gardent la priorité ; l’assemblage du `VeroScriptVm` brut reste explicite. Voir [Sélection des backends](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
