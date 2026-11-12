# 01 : Construire une chaîne d'outils Python

Prérequis : installez les paquets Python Frontend, BindGen et Transpiler appariés et rendez `veroscript` accessible. Travaillez dans un répertoire modifiable contenant vos sources typées prises en charge `host_api.py`, `app.py`, `package/models.py`, `package/service.py`. Adaptez les chemins ensemble. La liaison runtime demande aussi une Session du SDK Python configurée ; voir le [guide Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/index.md).

Cet exercice fait passer un module Python typé par le frontend, BindGen et le
transpileur.

## 1. Analyser les déclarations

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

Consultez l'AST et repérez le module, les fonctions, les classes et les
emplacements source. L'analyse n'importe pas le module.

## 2. Produire le contrat hôte

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

Comparez une fonction du catalogue avec le callable que l'hôte enregistrera.
Le catalogue ne remplace pas cette implémentation.

## 3. Convertir la source

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs --source-map generated/app.vs.map.json `
  --veroscript veroscript
veroscript --check generated/app.vs
```

Les fonctionnalités Python dont les sémantiques ne sont pas représentables sont
refusées plutôt que modifiées silencieusement.

## 4. Utiliser le mode projet

Lorsque les modules s'importent entre eux, fournissez-les ensemble afin que
les déclarations et les chemins soient résolus dans le même projet :

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py --project-root . --output-dir generated/bindings --validate --veroscript veroscript
python -m veroscript_python_transpiler package/models.py package/service.py --project-root . --output-dir generated/veroscript --veroscript veroscript
```

Conservez le manifeste, les cartes de source et les sorties correspondantes ;
ne mélangez pas des artefacts provenant de jeux d'entrées différents. Au
runtime, importez les modules sous le contrôle de l'application et enregistrez
le catalogue fusionné en une seule opération :

Le fragment suppose une Session configurée `session`, les objets modules `models`/`service` importés explicitement depuis `package.models`/`package.service`, et `project_bindings`, mapping lu dans `generated/bindings/bindings.json` ou résultat de `generate_binding_project`. Les outils sources n’importent pas ces modules pour vous.

```python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

Le helper vérifie la couverture des modules avant de modifier la session et
enregistre les types du projet avant les membres qui les utilisent entre
modules.

## Étape 5 : vérifier le binding et les valeurs

Utilisez `IntEnum` ou `IntFlag` pour une énumération enregistrée à l'exécution ;
un `enum.Enum` ordinaire est refusé. Les noms et valeurs entières doivent
correspondre exactement au catalogue. Une méthode d'instance doit également
rester une méthode d'instance et une entrée `staticMethod` doit rester statique
ou de classe.

`Value(None)` représente désormais le `null` du script et
`Value.priority_queue("int", [3, 1, 2])` crée une file de priorité. Affecter
`None` à une valeur non nullable lève `TypeError`. Ajoutez ces vérifications aux
tests de l'hôte pour détecter toute dérive entre catalogue et module.

## Exercices

1. Ajoutez une enum annotée et vérifiez ses valeurs dans le catalogue.
2. Ajoutez une variable locale non annotée et lisez le diagnostic.
3. Passez deux modules liés en mode projet et comparez les manifestes.
4. Chargez le catalogue produit dans un hôte Python et vérifiez que
   l'implémentation enregistrée correspond à la signature générée.

Pour un artefact Python autonome, continuez avec la [documentation Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/aot.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/python/01-python-source-tools.md) · [fr-FR](01-python-source-tools.md) · [ja-JP](../../ja-JP/python/01-python-source-tools.md) · [ko-KR](../../ko-KR/python/01-python-source-tools.md) · [zh-CN](../../zh-CN/python/01-python-source-tools.md) · [zh-TW](../../zh-TW/python/01-python-source-tools.md)
