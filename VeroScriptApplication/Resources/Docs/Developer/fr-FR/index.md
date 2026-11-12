# Maintenance du service Application

## Architecture et limites

`VeroScriptApplication` se situe au-dessus de Core. Core encode les images compilées et les bibliothèques `.vslib` ; Application gère les conteneurs ZIP `.vsapp`, descripteurs, inventaires, préparation et durées de vie. Gardez ZIP, adaptateurs cryptographiques et intégration GUI hors de Core. `VeroScriptApplicationPackages` adapte `VeroScriptPackageRuntime`, sans HTTP, aux environnements figés explicites ; ouvrir une application ne doit ni résoudre ni installer de paquets.

`BuildApplication` renvoie les fichiers scellés ; `WriteArchive` sérialise le ZIP externe. La surcharge acceptant une image brute doit préserver ses octets et métadonnées. `FApplication::Open` valide le contenu stocké. `ReadEntryPackage` inspecte les métadonnées compilées sans charger de fournisseurs ni appeler de fabriques d’environnement. `Prepare` conserve environnement et propriétaires des octets ; `FPreparedApplication::CompileSource` applique les valeurs sources portables possédées. Session doit utiliser ce chemin commun pour charger et recharger.

## Intégrité, possession et politique

Maintenez les mêmes contrôles d’inventaire, chemins, en-têtes ZIP locaux, modes, limites de taille/décompression et empreintes dans les formes répertoire, ZIP et fichier unique. Refusez fichiers supplémentaires, traversées, collisions de casse et fichiers spéciaux. Les caches gardent reçus de possession et baux interprocessus coopératifs ; les répertoires étrangers sont refusés et les consommateurs conservent les baux du contenu natif matérialisé. Le rechargement prépare code et environnement ensemble et restaure les deux en cas d’échec ; un catalogue natif modifié exige une nouvelle durée de vie applicative.

La protection authentifie le contenu avant de fournir le texte clair à Core. La confiance et le seuil de clés indépendantes viennent du consommateur ; un service requis absent entraîne un échec sans repli en clair. Réassembler du contenu signé exige une nouvelle politique explicite. Conservez les propriétaires des modules cryptographiques, ne sérialisez ni clés secrètes ni callbacks, et séparez signature du contenu et signature de plateforme. Les sources conservent leurs valeurs de compilation portables, sans chemins propres au développement.

## Intégration et vérification

Le build racine contrôle ce module optionnel ; `CMakeLists.txt` exporte Application et son adaptateur séparément. Maintenez ensemble identités Core, exports SDK, schémas `Resources/Schemas`, Interop/adaptateurs de langage, CLI et délégation runtime. `VeroScriptApplicationTests` et, en option, `VeroScriptApplicationPackageServicesTests`, activés depuis la racine, couvrent conteneurs/préparation et environnements. Contrôlez corruption, échecs de confiance, durées de vie, restauration, compilation source, déplacement et équivalence répertoire/ZIP/fichier unique. Bootstrap, signature de plateforme et exécution sur appareil nécessitent une validation propre consignée ; les contrôles documentaires statiques ne l’établissent pas.

`BuildApplication` accepte aussi une image d’application Core déjà préparée en conservant ses octets et métadonnées hôte. `FApplicationServices::PreparePackage` configure les services de l’entrée compilée après l’environnement et avant le chargement Session ; l’inspection seule ne l’appelle pas. Le rappel facultatif `ProvidersReady` de `ConfigurePackageApplicationServices` transmet les fournisseurs enregistrés, leur environnement conservé et un sélecteur de backend possédant son instantané aux adaptateurs. La priorité est celle des options hôte, des services de l’application puis des valeurs du projet, y compris celles d’un déploiement intégré et les désactivations explicites. GUI utilise ces hooks pour le conteneur `.vsapp` standard sans ajouter de dépendance GUI ou ZIP à Core. `PreparePackage` prépare uniquement les liaisons et ressources ; il ne doit pas exécuter le programme d’entrée.

<!-- docs-navigation -->

---

[Guide utilisateur](../../User/fr-FR/index.md) · [Documentation de développement](../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
