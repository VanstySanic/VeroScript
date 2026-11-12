# Paquets, réflexion et localisation Python

## Types de paquets et résultats de construction

Voir [SDK d'interopérabilité — Paquets et rechargement à chaud](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#package-container-format).

`PackageKind.APPLICATION` crée un paquet exécutable et
`PackageKind.LIBRARY` crée un paquet de bibliothèque. `Session.build_package_file(...)`
renvoie `(data, RunResult)` ; vérifiez `result.success` et les diagnostics avant
de construire `Package(data)`. `Package.from_file(path)` analyse un fichier et
`package_kind` indique son type. L'analyse valide la structure du conteneur,
mais pas l'authenticité de l'éditeur : vérifiez la signature sur
`canonical_signed_bytes` avant de charger des octets non fiables.

Un `Package` analysé est un instantané immuable. Créez un nouvel objet pour lire
d'autres octets ; un nouvel appel à `__init__` lève `veroscript.Error`.

## Octets signés et catalogues

La propriété publique `canonical_signed_bytes` renvoie les octets canoniques
sur lesquels une signature doit être vérifiée. `merge_catalog_directory`
ajoute les catalogues de l'application sans remplacer les catalogues runtime.

`Package.from_file()` lit un paquet et `package_kind` en indique le type. Une session peut utiliser `load_package`, `reload_package` et `build_package_file`. Un paquet est une frontière de chargement et de distribution, pas une sandbox de sécurité : contrôlez les bindings, chemins, ressources et entrées des paquets non fiables.

`reflection()` expose fonctions, types, méthodes, champs, paramètres et métadonnées ; `bindings()` renvoie la vue des bindings hôte.

`set_language`, `get_language` et `available_languages` gèrent la langue courante et les catalogues. `merge_catalog_directory` fusionne un répertoire externe ; validez ses clés et ses paramètres de formatage avant de l'utiliser. La localisation modifie les diagnostics, jamais la sémantique du script.

`ABI_VERSION`, `RUNTIME_VERSION` et `CAPABILITIES` identifient le runtime natif chargé. Vérifiez les capacités avant de dépendre de fonctions optionnelles telles que les packages, la réflexion, les valeurs de conteneur, les diagnostics JIT, le rechargement à chaud ou la gouvernance de l'exécution. Cette gouvernance couvre les limites coopératives d'instructions/de temps, l'annulation et la politique des capacités de la bibliothèque standard. Pour les performances, mesurez séparément démarrage, appels chauds, chargement de paquets, cache, conversions Python et coût du GIL. Les statistiques JIT sont diagnostiques ; `direct_native_calls` compte les appels JIT à portée de génération ayant atteint une autre entrée native sans passer par le pont de valeurs de la VM. La présence du JIT ne garantit pas un gain dans tous les scénarios.

## Références d’objet retournées

`enumerate_object_references(instance)` reçoit un `Value` objet script et doit être appelé sur le thread créateur de la session au repos. Il renvoie un tuple de `ExternalReference`. Chaque `kind` vaut `ExternalReferenceKind.SCRIPT_OBJECT` ou `ExternalReferenceKind.HOST_OBJECT` ; chaque `value` possède un wrapper `Value` cloné. La copie conserve l’identité de l’objet et ne duplique pas son graphe. Le tuple et ses valeurs peuvent être conservés après l’énumération : ils ne sont pas des vues empruntées de callback. Les règles ordinaires des valeurs, sessions, libérations et threads restent applicables.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/packages.md) · [fr-FR](packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
