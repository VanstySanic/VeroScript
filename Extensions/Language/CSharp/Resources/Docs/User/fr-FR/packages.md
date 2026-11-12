# Paquets, réflexion et localisation C#

## Métadonnées du paquet

Voir [SDK d'interopérabilité — Paquets et rechargement à chaud](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#package-container-format).

`GetMetadata()` renvoie les métadonnées possédées, `HasCompiledModule` indique
si le module peut encore être transféré et `GetCanonicalSignedBytes()` renvoie
les octets couverts par la signature. Les catalogues se chargent ou se fusionnent
avec `VeroScriptRuntime.LoadCatalogDirectory` et
`VeroScriptRuntime.MergeCatalogDirectory`.

`VeroScriptPackage.Read` analyse les octets d'un paquet. `LoadPackage`, `ReloadPackage` et `BuildPackageFile` gèrent ensuite les paquets ; `VeroScriptPackageKind` et `VeroScriptPackageMetadata` en décrivent la nature et les métadonnées. Un paquet n'est pas une sandbox : limitez bindings, chemins, ressources et temps d'exécution.

`ExportReflectionJson` et `ExportBindingsJson` exportent les fonctions, types, méthodes, champs, paramètres et bindings. `VeroScriptResolvedMethod`, `VeroScriptHostObjectInfo` et `VeroScriptNativeType` servent à l'outillage et à la validation, pas au remplacement des contrôles d'accès.

`VeroScriptRuntime` fournit la langue courante, le JSON des langues disponibles et le chargement ou la fusion de catalogues. Choisissez un tag BCP 47 réellement présent dans la construction et validez les catalogues de confiance. La langue modifie les diagnostics et le formatage, pas la sémantique.

Vérifiez `VeroScriptRuntime.Capabilities` avant d'utiliser JIT, packages, réflexion, reload, sérialisation ou gouvernance de l'exécution. Cette gouvernance couvre les limites coopératives d'instructions/de temps, l'annulation et la politique des capacités de la bibliothèque standard. Pour les performances, mesurez séparément démarrage, appels chauds, cache, conversion de valeurs et callbacks.

`VeroScriptRuntime.AbiVersion` doit correspondre exactement à l'ABI de l'adaptateur `0x00000101` (0.1.1). Déployez le package managé, la bibliothèque native Interop et les catalogues depuis des versions correspondantes.

## Propriété du module et signature

Un `LoadPackage` ou `ReloadPackage` réussi transfère le module compilé à la session. Les métadonnées et octets canoniques restent lisibles, mais la même instance ne peut plus transférer ce module. Analysez de nouveau les octets originaux si un autre transfert est nécessaire. `Read` valide le format du conteneur, pas l’authenticité de son auteur ; vérifiez la signature sur `GetCanonicalSignedBytes()` avant de charger un paquet non fiable. `BuildPackageFile` renvoie les octets du paquet et un `VeroScriptRunResult` ; ses options définissent le type application/bibliothèque, le nom, la version, les métadonnées et la suppression des informations de débogage.

Les valeurs reçues par `EnumerateObjectReferences` sont empruntées pendant le callback ; ne les conservez pas après son retour. Définissez quand un changement de langue à l’échelle du processus est permis afin que les tâches concurrentes ne mélangent pas leurs diagnostics. Les contrôles d’arguments ou de durée de vie propres au SDK peuvent produire des exceptions en anglais ; le choix de langue concerne les diagnostics du runtime.

Voir [Valeurs C# et propriété](values.md#managed-call-lifetime).

## Sélection de la langue des diagnostics

Affectez le tag choisi à `VeroScriptRuntime.CurrentLanguage`. Découvrez les tags réellement disponibles avec `VeroScriptRuntime.GetAvailableLanguagesJson()` avant de proposer un choix. Cette propriété règle la langue des diagnostics à l’échelle du processus ; coordonnez les changements avec les autres sessions.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/packages.md) · [fr-FR](packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
