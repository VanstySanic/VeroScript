# AOT et packaging Python

## Émetteur autonome

`VeroScriptPythonAot` émet un module Python lisible contenant un petit runtime de compatibilité, sans charger VM VeroScript, bibliothèque Interop ni extension CPython. Le wrapper de contrôle peut trouver un CLI fourni par l’utilisateur depuis chemin exécutable, racine SDK, `bin`, variables d’environnement ou `PATH`.

Les appels SDK avec `FCodegenOptions` transmettent les définitions d'annotations
de `Bindings` au traitement, y compris la politique `CaseSensitive`. Ces
métadonnées seules n'ajoutent aucune dépendance d'exécution ; les fonctions et
types hôtes d'exécution ne sont pas pris en charge.

## Contrat du Bundle

Le mode Bundle écrit `__main__.py`, `__init__.py`, `_veroscript_runtime.py`, `_veroscript_program.py` et `python-aot.manifest.json`, et vérifie le graphe d’import complet comme un programme. Les fichiers sont préparés dans un répertoire temporaire avant le remplacement, permis seulement pour un répertoire ordinaire vide ou avec manifeste VeroScript structurellement valide sans contenu étranger ; les `__pycache__` reconnus sont le seul contenu généré toléré.

Les liens symboliques et jonctions Windows finaux sont refusés. L'ancien répertoire est déplacé pendant le remplacement, ce qui peut rendre le chemin brièvement absent. En cas d'échec, sa restauration est tentée.

## Contrat de distribution

Wheel et distribution source portent extension native, bibliothèque Interop, sources Python, informations de type, bibliothèque standard et catalogues de langue de versions concordantes. `VeroScriptVersion.h`, métadonnées du paquet et version runtime importée doivent être identiques. La validation de release vérifie matrice CPython/plateforme, surface de types installée et reconstruction de distribution source.

Le runtime Python autonome convertit les erreurs de domaine/dépassement en résultats flottants Core sans laisser sortir `ValueError`/`OverflowError` : log de zéro donne moins l’infini, le dépassement de puissance conserve le signe pour l’exposant entier impair, et floor/ceil/trunc conservent le zéro signé. Le texte NaN généré par Python/C# suit la plateforme, dont Darwin. Couvrez ces détails dans les régressions ; les vérifications publiques de finitude sont dans [Math](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/math.md#floating-results).

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/aot-and-packaging.md) · [fr-FR](aot-and-packaging.md) · [ja-JP](../ja-JP/aot-and-packaging.md) · [ko-KR](../ko-KR/aot-and-packaging.md) · [zh-CN](../zh-CN/aot-and-packaging.md) · [zh-TW](../zh-TW/aot-and-packaging.md)
