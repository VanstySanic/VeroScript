# Publications par produit et versions indépendantes

Cette page suit la seule [norme de packaging et publication](../../../../../Resources/Docs/Standards/fr-FR/PACKAGING.md). Ni compatibilité ni conversion des anciens formats : nettoyer les anciens résultats/candidats puis reconstruire, avec les versions produit définies par le registre.

Disposition produit d’abord : `Release/<business-route>/<target>/<version>/`, puis `archives/`, `sdks/`, `packages/`, `installers/`, `symbols/` selon l’usage. Partage sous `any`, SDK WebAssembly sous `wasm32`, natif par vrais OS/architecture. `release-index.json` référence `indexes/<target>.json` ; un produit cible peut référencer plusieurs manifestes natifs/partagés. Champs, responsabilités, charges et nettoyage sont définis par la norme.

Le registre commun et les scripts implémentent chemins classés, contrôles de format et suivi de validation. Une distribution exige une vraie compilation réussie et des audits de contenu/inventaire réussis. Consignez séparément exécution cible et cycle de vie, y compris échecs et contrôles non exécutés. Planification et contrôles statiques seuls ne prouvent ni compilation ni validation runtime.

Nettoyer uniquement les résultats possédés vérifiés et temporaires reproductibles sans utilisateurs actifs ; préserver sources, identifiants, outils et disques utiles. Pas de migration de l’ancienne collection ; mises à jour nouvelles toujours verrouillées, récupérables et vérifiées par fermeture/empreintes. Une modification documentaire seule ne lance aucun packaging.

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

Distinguer les cibles réelles dans `supportedTargets` des cibles consommatrices autorisées dans `consumerTargets`. Un SDK wasm32 peut être référencé par les index natifs. La composition conserve des versions réelles compatibles et correspondantes des paquets.

## Planification et points d’entrée de l’implémentation

Exécutez depuis la racine du dépôt. `configuration` lit versions, chemins, couverture et versions de format du registre ; le plan utilise les mêmes fonctions de routage sans compiler, préparer ni publier d’artefacts. Répétez `--target` et `--business` pour limiter le plan. `indexSelections` peut référencer des manifestes natifs et partagés ; les entrées facultatives et tags de wheels exigent encore les sorties réelles des producteurs.

Les entrées source et commandes de compilation figurent dans [compilation et bundles](build-and-bundle.md). La [validation de publication](release-validation.md) explique la vérification finale et ses limites. Les commandes ci-dessous lisent seulement les métadonnées ; une modification des normes seules se limite aux contrôles documentaires ci-dessus.

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## Inventaire et preuves de validation

`complete` décrit les produits requis dans l’index cible. `qualificationStatus` résume la validation enregistrée ; `qualified` exige aussi un inventaire complet et les contrôles requis satisfaits. Échecs runtime/appareil et contrôles non exécutés ne bloquent pas la distribution de contenus construits et vérifiés. Consultez les résultats avant de déclarer une cible prête ; une preuve absente signifie `not-recorded`.

Chaque entrée produit peut porter `qualification` schemaVersion 1, lié aux chemins/SHA-256 de `manifests[]`. `--records` fournit un objet JSON associant les ID de produits publiés à des objets `checks`. Chaque contrôle possède un `status` réel et un `reason` non vide ; `required` vaut true par défaut et `evidenceSha256` est facultatif. Un `build` réussi et un contrôle `target-runtime` explicitement requis sont obligatoires. Utilisez les vrais résultats, dont `failed` et `not-run` ; une modification du contenu invalide les anciennes preuves. L’outil met à jour l’index cible et l’empreinte racine sous verrou.

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## Contrôle préalable d’une collection existante

Avant publication, les index cibles existants doivent déclarer `requiredBusinesses` exactement selon le registre de produits actuel. Si cet ensemble a changé, choisissez une nouvelle sortie dédiée vide sous `.tmp` ou `dist` et reconstruisez la collection depuis les sources. La sortie `Release` par défaut subit le même contrôle. Les aperçus incluent toutes les cibles desktop et mobiles enregistrées, y compris `win-x86`, sauf sélection plus restreinte avec `--target`.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/release-layout.md) · [fr-FR](release-layout.md) · [ja-JP](../ja-JP/release-layout.md) · [ko-KR](../ko-KR/release-layout.md) · [zh-CN](../zh-CN/release-layout.md) · [zh-TW](../zh-TW/release-layout.md)
