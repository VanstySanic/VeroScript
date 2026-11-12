# Procédure de validation de publication

Cette page suit la seule [norme de packaging et publication](../../../../../Resources/Docs/Standards/fr-FR/PACKAGING.md). Ni compatibilité ni conversion des anciens formats : nettoyer les anciens résultats/candidats puis reconstruire, avec les versions produit définies par le registre.

## Étapes de validation

Partez de la source/configuration prévue et du profil cible choisi. Les prérequis des producteurs figurent dans [compilation et bundles](build-and-bundle.md). Vérifiez l’inventaire candidat, puis exécutez les consommateurs, applications et cycles de vie d’installateur requis sur la cible. Une compilation croisée ne valide pas l’exécution. Liez les résultats aux empreintes finales après signature et toute opération modifiant les octets.

## Inventaire et comparaison aux sources actuelles

Les exemples suivants vérifient une collection existante depuis la racine du dépôt. Remplacez `win-x64` par la cible consommatrice réelle. Un index partagé `any`/`wasm32` ne suffit pas à établir la complétude d’une publication native ou mobile.

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete` vérifie les produits requis par la cible et refuse les produits partiels ; il n’exécute ni ne certifie les validations runtime/appareil, graphiques, cycle de vie, signature ou notarisation. `--check-source` compare en plus les contenus publics issus des sources couverts par le vérificateur à leurs originaux actuels ; il ne prouve pas que les binaires ont été reconstruits depuis ces sources. Omettez-le seulement pour auditer le contrat enregistré d’une ancienne collection valide au nouveau format. `--version` exige cette version réelle pour chaque référence produit sélectionnée ; ce n’est pas une version commune de collection.

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## Preuves et contrôle final

Consignez `passed`, `failed`, `not-run` ou `not-applicable`, avec raisons, identité source, outils, configuration, cible/appareil et empreintes finales. Séparez édition de liens croisée, émulateur/simulateur, appareils physiques, C# AOT/trimming, applications Python et installation/réparation/désinstallation. Environnements absents et artefacts non signés restent des lacunes explicites. Le wrapper Windows peut imposer Authenticode avec `-RequireSignedInstallers` ; il ne signe pas et ne remplace pas le cycle de vie.

Avant distribution, exigez une vraie compilation réussie et des contrôles de contenu, cible, dépendances, manifestes et empreintes réussis. Exécutez la validation runtime/cycle de vie lorsque l’environnement existe, en enregistrant échecs et contrôles non exécutés ; ces lacunes n’excluent pas les artefacts construits valides de la distribution. Consultez les [preuves et l’inventaire](release-layout.md#release-qualification) pour distinguer `complete` et `qualified`. Après distribution, revérifiez inventaire et empreintes ; une entrée modifiée invalide les preuves affectées. Une modification des normes seules utilise seulement les contrôles documentaires ci-dessous, sans compilation ni packaging produit.

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## Validation statique des téléchargements d’installateur

`veroscript_installer.py` vérifie le nom du téléchargement et l’architecture cible, lit le bundle .NET monofichier et compare ses parties embarquées au SHA-256 et au nombre de parties déclarés dans l’assembly. L’assemblage pour une autre cible compare aussi ce digest au ZIP vérifié pendant la préparation. Sous Linux, le contrôle porte sur l’unique exécutable et ses permissions dans le `.tar.gz`. Sous macOS, il porte sur l’application et le contenu embarqué dans le `.dmg`. L’inspection d’un DMG depuis un autre système requiert 7-Zip via `VEROSCRIPT_7ZIP` ou PATH. Consignez séparément les contrôles d’exécution cible et de cycle d’installation.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/release-validation.md) · [fr-FR](release-validation.md) · [ja-JP](../ja-JP/release-validation.md) · [ko-KR](../ko-KR/release-validation.md) · [zh-CN](../zh-CN/release-validation.md) · [zh-TW](../zh-TW/release-validation.md)
