# Transpileur Python

`VeroScriptPythonTranspiler` convertit un sous-ensemble Python 3.11 strict et
statiquement typé en source VeroScript. Il partage l'analyse de déclarations du
frontend Python et reste indépendant du runtime Python et du backend AOT
VeroScript vers Python.

Le sous-ensemble accepte les fonctions typées, classes ordinaires,
énumérations entières, champs et variables annotés, constructeurs, méthodes,
littéraux scalaires et conteneurs, appels, affectations, conditions, boucles
`while` et expressions ordinaires. Les entiers de précision arbitraire, les
imports dynamiques, async, compréhensions, générateurs, descripteurs,
propriétés, héritage multiple, réflexion et variables locales non annotées sont
refusés.

`@dataclass` est refusé : les constructeurs, l'égalité et les autres comportements
générés ne sont pas implémentés. Les arguments nommés de classe, dont `metaclass`,
sont également refusés, car leur comportement à l'exécution n'est pas implémenté.
Utilisez des classes ordinaires avec des
constructeurs et méthodes explicites dans le sous-ensemble pris en charge.
La conversion intégrée `str(...)` est refusée, car la sémantique de formatage
Python n'est pas implémentée. Les classes et fonctions utilisateur nommées `str`
restent appelables si elles sont déclarées ou explicitement importées dans le module courant.

Les valeurs par défaut des champs dans le corps d'une classe acceptent les littéraux
scalaires immuables, les expressions constantes pures prises en charge et les membres
d’énumérations entières connues. Les conteneurs partagés, objets, références arbitraires
de noms et appels de fonctions sont refusés.
Initialisez les valeurs d'instance dans un `__init__` explicite ; pour un conteneur,
créez d'abord une variable locale typée, puis affectez-la au champ d'instance.

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs --source-map input.vs.map.json `
  --veroscript veroscript
```

`--timeout <seconds>` limite chaque validation du compilateur externe et doit
être une valeur finie et strictement positive. L'API expose le même réglage
avec `timeout_seconds` dans les deux types d'options.

La sortie est validée par le compilateur VeroScript par défaut. Le mode projet
résout les imports relatifs fournis, conserve la disposition des paquets,
écrit les cartes source et `transpile.manifest.json`, puis valide le graphe
complet. Rien n'est remplacé si l'analyse, la conversion ou la validation
échoue. L'API fournit `transpile`, `transpile_source`, `transpile_file` et
`transpile_files`.

Les imports de projet qui renomment un module ou une déclaration avec `as` sont rejetés ;
utilisez les noms d’origine. Les imports de métadonnées comme `import typing as t` restent pris en charge.

## Validation et mode projet

La validation externe est activée par défaut ; utilisez `--no-validate` seulement si une étape externe prend explicitement en charge la validation du code généré. `--timeout` vaut `120.0` secondes par défaut et limite chaque appel externe, pas la durée totale du projet. Les sources, source maps et manifeste sont préparés avant leur remplacement ; un échec d’écriture déclenche une tentative de restauration. Les imports non résolus ou ambigus sont rejetés.

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

`len(str)` compte les points de code Unicode, y compris les caractères supplémentaires et les marques combinantes, et non les octets UTF-8. Les longueurs des littéraux ne nécessitent aucune dépendance à l’exécution. Les autres chaînes déclarées utilisent un auxiliaire Core linéaire avec un tampon UTF-8 et importent la bibliothèque standard `string.vs` ; ce chemin coûte O(n) en temps et en stockage temporaire, contrairement à la longueur de chaîne Python en O(1), et exige le runtime Core et est actuellement rejeté par Python AOT autonome. Un fichier généré ou existant `string.vs` ne doit pas masquer cette dépendance. Les longueurs des conteneurs conservent leurs opérations Core directes. Si le type chaîne ou conteneur d’une expression ne peut pas être établi, utilisez d’abord une variable locale annotée.

Les appels aux liaisons Python nommées `print`, `len`, `abs`, `min` ou `max` sont rejetés lorsque Core les intercepterait comme fonctions intégrées ; les méthodes ordinaires et une fonction ou classe `str` déclarée restent appelables. Les champs seulement annotés doivent être initialisés explicitement avant lecture. Les attributs manquants et l’analyse complète de l’affectation certaine de Python ne font pas partie de ce sous-ensemble.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/transpiler.md) · [fr-FR](transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
