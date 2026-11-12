# Documentation technique VeroScript Installer

`VeroScriptInstaller` regroupe l’installation, la réparation, la désinstallation, les archives de distribution et leur organisation multiplateforme. Cette page s’adresse aux responsables de maintenance et des outils de publication. Les commandes destinées aux utilisateurs sont décrites dans le [guide d’installation et de distribution](../../User/fr-FR/index.md).

## Responsabilités des modules

Le module comporte quatre couches :

- `Core/` gère les archives, manifestes, métadonnées des paquets, validations, sécurité des chemins, informations de plateforme, nouvelles tentatives et moteur d’installation.
- `App/` gère l’application graphique Avalonia, la progression, les comptes rendus et la localisation de l’interface.
- `Tool/` fournit l’entrée en ligne de commande pour construire et assembler les distributions.
- `Tests/` vérifie les contrats du moteur, de l’interface, des archives et du conditionnement.

Le moteur doit rester utilisable sans interface graphique. Les textes, la progression et le lancement des processus ne doivent pas être nécessaires pour valider un manifeste ou effectuer une opération sûre sur les fichiers.

## Invariants des archives et manifestes

Validez archives, définitions de produit et manifestes de paquet ou d’installation avant extraction ou remplacement. Vérifiez les chemins par rapport à la racine prévue ; traversées, sorties absolues, contenu étranger inattendu et entrées ambiguës doivent être refusés. Une opération partielle ne devient pas un succès du seul fait d’une nouvelle tentative.

L’installation, la réparation et la désinstallation doivent être idempotentes dans les limites du contrat public. Décrivez remplacement atomique, nettoyage, restauration et signalement d’échec à partir du moteur et de ses tests.

## Intégration aux plateformes

Les intégrations des chemins Windows et Unix sont des implémentations distinctes derrière l’interface commune. Les mises à jour de PATH, la configuration du shell, les permissions et la détection de plateforme doivent préserver les règles de chaque système ; ne les réduisez pas à une unique manipulation de chaînes non sûre.

## Enregistrement d’installation et restauration

L’installation prépare chaque composant séparément et écrit un enregistrement contenant racines, fichiers possédés, identité du produit, empreinte du manifeste et modifications runtime de `PATH`. La réparation exige exactement l’identité du paquet d’installation, la version produit, la plateforme, l’architecture et l’empreinte enregistrées. La désinstallation ne supprime que les fichiers et entrées PATH possédés ; les autres fichiers sont conservés.

Les destinations ne doivent ni se chevaucher ni recouvrir le contenu protégé de l’installateur. Vérifiez les liens dans les destinations gérées et leurs ancêtres. Avant de déplacer une installation, contrôlez enregistrement existant, enregistrements voisins, destination et possession. Un échec avant validation finale déclenche une restauration ; un échec de nettoyage après validation conserve le journal pour une reprise sans déclarer l’installation terminée en échec. `LoadRecordAsync` effectue la récupération transactionnelle avant la lecture, y compris au démarrage graphique de réparation/désinstallation.

Les installateurs formels actuels contiennent les composants fonctionnels ; documentation et exemples sont distribués par Resources. D’anciens manifestes et registres peuvent conserver des composants auxiliaires pour réparation/désinstallation, mais l’assistant les exclut des nouvelles sélections et refuse celles qui dépendent de composants indisponibles. Enregistrez les composants réellement installés au lieu de les déduire de l’arborescence actuelle.

## Localisation et ressources de distribution

Les catalogues sources de l’interface appartiennent au module sous `App/Resources/Localization/<culture>/` et sont embarqués en JSON dans l’assembly applicatif. Les noms de culture sont des balises BCP 47 canoniques qualifiées. La sélection et le repli anglais dépendent des catalogues. Clés de ressource, champs de protocole, noms d’énumération, options, chemins, versions et ID de diagnostic conservent leur orthographe.

## Pages détaillées

- [Cycle de vie et sécurité](lifecycle-and-safety.md)
- [Construction et contrat des archives](build-and-bundle.md)
- [Validation de publication](release-validation.md)

## Source de vérité

Appuyez-vous sur `Core/`, `App/`, `Tool/`, les fichiers projet, les tests d’archives/manifestes et les vérifications rapides de conditionnement. Mettez cette page à jour avec les changements d’organisation des archives, de schéma, de sécurité des chemins, de restauration, d’intégration système, de cycle d’installation ou de localisation.

- [Automatisation de l’installateur](automation.md)
- [Organisation des distributions et règles de publication](release-layout.md)

[Propriété et récupération des associations](file-associations.md)

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
