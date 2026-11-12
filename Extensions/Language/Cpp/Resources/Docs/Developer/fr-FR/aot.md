# Émetteur AOT C++

`VeroScriptCppAot` émet du C++ à partir d’un programme VeroScript vérifié par compilation. L’API publique accepte chaînes source, fichiers, programmes, une chaîne C++ générée et `FCppAotSourceBundle`, qui contient fichiers relatifs et manifeste JSON.

## Invariant du Bundle

Le Bundle généré utilise volontairement une seule unité de traduction C++ et des fragments `.inl`. Le fractionner en modules liés indépendamment changerait instanciation de modèles, linkage interne et ordre d’initialisation globale. Cette disposition est donc un contrat de sortie, pas une préférence de formatage.

## Admissibilité et support runtime

L’émetteur valide le module d’entrée et accepte la fonction `main` sous les formes `void main()`, `int main()`, ou `string[] args` correspondantes. Il émet le support de compatibilité seulement pour les valeurs, conteneurs, channels et tâches worker utilisés. Les globales et statiques AOT worker sont isolées par thread natif ; channel est le partage synchronisé explicite.

Les entrées de tâche désignées par une chaîne, les demandes d'annulation coopérative `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()` et les opérations de système de fichiers `std::filesystem::is_file()`, `std::filesystem::is_directory()` et `std::filesystem::remove_dir()` sont prises en charge. Les entrées de tâche par callable et la réflexion exécutable `field_info<T>` / `method_info<T>` sont rejetées avec un diagnostic AOT.

Il rejette tout programme dont le lowering altérerait sémantique numérique, propriété, exception, destruction, module ou concurrence. Un échec de compilation, admissibilité, lowering ou écriture ne retourne jamais de source partielle.

Le type `array<bool>` généré utilise un emplacement booléen adressable par élément pour fournir un véritable `bool&` lors de l’indexation et du passage par référence. Ce stockage n’est pas compacté en bits et consomme davantage de mémoire que `std::vector<bool>` à capacité égale.

## Maintenance

Les en-têtes publics de l’émetteur, implémentation, tests source générée/Bundle et chemin de validation des fichiers générés font foi. Un helper runtime n’est ajouté que si un test démontre le comportement VeroScript correspondant et que le helper reste autonome dans le Bundle généré.

Voir [AOT C++ — Modèles pair utilisateur](../../User/fr-FR/aot.md#cleanup-container-operations).

Alignez la conversion décimale autonome sur `Shared/Private/VeroScriptFloatParsing.h`. `VeroScriptFloatParsingSource.h`, `MakeRuntimeSupport` et `VsFloatParsingDetail` forment la sortie autonome. Lors du déplacement de l’entrée, mettez à jour ensemble chemin de lecture CMake et dépendance de configuration ; les consommateurs générés ne doivent pas exiger d’en-tête privé des sources. Validez largeur cible, locale et plage séparément de Core AOT.

<a id="standalone-file-paths"></a>

`VsPathFromUtf8` généré reste autonome. Sous Windows, les branches char8_t et C++17 u8path construisent un chemin, normalisent les séparateurs natifs et n’ajoutent le préfixe étendu qu’aux chemins absolus d’au moins 248 unités UTF-16 natives. UNC complet utilise sa forme étendue ; un préfixe existant est conservé et les chemins relatifs ne deviennent pas absolus. Rejetez NUL avant conversion. Maintenez génération et régressions ensemble pour Unicode, chemins absolus/UNC longs, relatifs et les deux branches du standard, sans en-tête source privé requis par le consommateur.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/aot.md) · [fr-FR](aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
