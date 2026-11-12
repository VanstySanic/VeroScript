# Python BindGen

`VeroScriptPythonBindGen` transforme les déclarations du sous-ensemble Python
typé en catalogue de liaisons v1. Il lit le code statiquement : il n'importe
pas les modules et n'exécute pas les décorateurs. Le catalogue décrit une
interface hôte ; il ne constitue pas l'implémentation d'exécution.

La surface comprend les fonctions publiques, classes, dataclasses,
`IntEnum` et `IntFlag`, champs annotés, constructeurs, méthodes d'instance,
statiques ou de classe, et propriétés. Les signatures dynamiques ou non
typées, variadiques, avec valeurs par défaut, les globaux de module,
l'héritage non supporté et les types non représentables sont refusés.

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host `
  --catalog host.bindings.json `
  --api-reference host.vs.txt `
  --validate --veroscript veroscript
```

`--timeout <seconds>` limite chaque validation externe et doit être une valeur
finie et strictement positive.

Le mode projet produit un `bindings.json` fusionné, une référence par entrée et
`bindgen.manifest.json` :

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . --output-dir generated/bindings `
  --validate --veroscript veroscript
```

`--output-dir` sélectionne le mode projet, même pour une seule entrée ; plusieurs entrées exigent cette option. `--project-root` et `--manifest` sont réservés à ce mode ; `--manifest` remplace le chemin de sortie du manifeste. N’y combinez pas `--catalog`, `--api-reference` ou `--module-name`. Les sorties sont préparées ensemble avec restauration au mieux en cas d’échec ; conservez le manifeste avec le catalogue et les références correspondants.

L'API expose `generate_bindings`, `generate_bindings_from_source`,
`generate_bindings_from_file`, `generate_binding_project`, `validate_catalog`,
`bind_module` et `bind_modules`. Ces deux dernières fonctions utilisent
uniquement les modules que l'application a importés explicitement ; BindGen
n'importe ni n'exécute le code analysé. `bind_modules` vérifie la couverture
complète du projet avant de modifier la session.

Lors de la liaison, l'énumération fournie doit dériver de `IntEnum` ou
`IntFlag`, et ses noms et valeurs doivent correspondre exactement au catalogue.
Les déclarations de membres sont également vérifiées : une méthode d'instance
doit rester une méthode d'instance, et une entrée `staticMethod` doit rester
statique ou de classe. Les divergences échouent avant toute modification de la
session.

Lorsque plusieurs modules déclarent le même nom de type, utilisez une annotation
qualifiée comme `models.Item`. Les noms simples ambigus sont refusés, même s'ils
correspondent à un alias scalaire comme `Int8`. Les types du module courant
priment sur les alias simples du projet et les alias scalaires.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/bindgen.md) · [fr-FR](bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
