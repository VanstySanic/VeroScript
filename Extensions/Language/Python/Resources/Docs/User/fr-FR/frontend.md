# Frontend Python

`VeroScriptPythonFrontend` est le frontend statique partagé par le générateur
de liaisons Python et le transpileur Python vers VeroScript. Il analyse un
sous-ensemble Python typé sans importer ni exécuter le module et produit un
modèle de déclarations appartenant à VeroScript.

Il conserve les emplacements source, les modules et imports, fonctions typées,
classes, dataclasses, énumérations entières, champs annotés, paramètres,
décorateurs, documentation et annotations de types normalisées. L'exécution
au niveau du module, les types dynamiques ou non résolus et les constructions
non représentables produisent un diagnostic.

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

Le mode projet analyse plusieurs fichiers comme un ensemble :

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

Il prépare un AST par entrée et `frontend.manifest.json` avant leur mise en place ;
en cas d’échec, la restauration des sorties précédentes est tentée.
Les fichiers dupliqués, les noms de modules dupliqués et les entrées hors de
`--project-root` sont refusés avant le remplacement d'une sortie. L'API expose
`parse_source`, `parse_file` et `parse_files`. Utilisez ce frontend pour la
structure, BindGen pour un catalogue hôte et le transpileur pour produire du
VeroScript.

## Repli des diagnostics

Utilisez `--lang` pour choisir la langue des diagnostics ; il prévaut sur `VERO_LANG` et la langue système. Les messages traduits absents ou inutilisables sont remplacés par l’anglais. Codes de diagnostic et positions source restent identiques dans toutes les langues.

`--lang` s’applique aux messages fournis par les catalogues VeroScript. Il ne traduit ni l’aide et les erreurs d’arguments d’`argparse`, ni les textes transmis par Python, le système ou un compilateur externe ; ceux-ci conservent leur propre comportement linguistique.

## Options et sorties du projet

En mode fichier unique, `--module-name` impose le nom logique du module ; `--version` affiche la version de l’outil. Le mode projet conserve la disposition des paquets et rejette les entrées répétées, les noms de module dupliqués et les fichiers extérieurs à `--project-root`. `--output-dir` ne se combine ni avec `-o` ni avec `--module-name`. Les sorties sont encodées en UTF-8 et préparées avant leur remplacement.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/frontend.md) · [fr-FR](frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
