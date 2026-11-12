# SDK Python

Voir aussi [Python AOT](aot.md) pour la génération d'un programme Python autonome,
les bundles source et les limites de cette cible.

`VeroScriptPython` est l'adaptateur CPython de l'ABI Interop VeroScript. Il permet de charger et d'exécuter des scripts, d'appeler des fonctions, d'exposer des callbacks et des classes typés, d'inspecter les métadonnées et de manipuler des paquets et des valeurs.

Le SDK couvre les backends VM, mixed et JIT, les diagnostics structurés, les conteneurs typés, les liaisons hôte, les overlays de source, le rechargement à chaud, les statistiques JIT, la réflexion, la sérialisation, les événements de mutation et la localisation.

## Installation

```text
python -m pip install veroscript
```

L’extension exige une correspondance exacte avec l’ABI Interop native : `0x00000101` (0.1.1). Une incompatibilité provoque `ImportError` lors de l’import de `veroscript`. Conservez ensemble les bibliothèques natives de la wheel ; ne remplacez pas sa bibliothèque Interop par celle d’une autre version. Après un import réussi, `veroscript.ABI_VERSION` indique l’ABI et `veroscript.RUNTIME_VERSION` la version du runtime.

La wheel contient l'extension Python, la bibliothèque Interop correspondante, les catalogues, les informations de type et la bibliothèque standard. L'extension native dépend de la version mineure de CPython et de l'architecture. `py.typed` et `_veroscript.pyi` sont fournis pour les vérificateurs de types.

Les callbacks reprennent le GIL ; leurs exceptions deviennent `CallbackError` et ne traversent pas le C ABI.

Les wheels publiées prennent en charge CPython standard 3.11–3.15 avec le GIL activé ; les interpréteurs de débogage et sans GIL ne sont pas pris en charge. Choisissez une wheel correspondant à votre système, à l’architecture du processus et à la version mineure de CPython. Les wheels Linux conservent leur compatibilité native : une wheel construite sur une autre distribution Linux n’est pas nécessairement compatible avec la vôtre.

Le module natif d’une archive ou d’un installateur SDK Python correspond uniquement à la version mineure de CPython utilisée pour ce paquet. Pour une autre version mineure prise en charge, installez la wheel correspondante dans l’environnement de cet interpréteur.

## Exécution minimale

```Python
from veroscript import Backend, Session

source = """
int main(string[] args)
{
	print("arguments={}", len(args));
	return 0;
}
"""

with Session(backend=Backend.MIXED, script_args=("one", "two")) as session:
	result = session.load_source(source, "main.vs")
	if not result.success:
		raise RuntimeError(result.diagnostics)
	result = session.run()
```

## Références

- [Sessions et exécution](session.md) : configuration, appels, résultats, JIT, rechargement et threads.
- [Valeurs et conteneurs](values.md) : scalaires, conteneurs, propriété et callables conservés.
- [Liaisons hôte](host-bindings.md) : fonctions, décorateurs, classes et mutations.
- [Paquets, réflexion et localisation](packages.md) : métadonnées, sérialisation et catalogues.
- [Frontend Python](frontend.md) : AST statique pour Python typé.
- [Python BindGen](bindgen.md) : catalogues de liaisons et artefacts de projet.
- [Transpileur Python](transpiler.md) : conversion validée de Python typé vers VeroScript.

La référence du langage reste la source normative de la syntaxe VeroScript ; cette section décrit uniquement la frontière Python.

[Python sur Android et iOS](mobile.md)

## Mémoire gérée

API hôte : `session.memory` / `current_memory()` / `set_memory_limits`. La référence [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md) décrit la propriété, les quotas, l'expiration des vues et les emprunts.

- [Mémoire gérée depuis l’hôte](memory.md)

## variant

Voir [SDK d'interopérabilité — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#exported-value-lifetime).

```python
from veroscript import Session, Value
with Session() as session:
	payload = Value(42, kind="int32")
	box = session.variant("int", payload)
	assert box.variant_has_value()
	assert box.variant_type_name() == "int"
	extracted = box.variant_get()
	assert extracted.value == 42
```

Chargez une application avec `Session.load_application(absolute_path, options=...)` et vérifiez le résultat avant d’appeler `run()`. Les chemins des options hôte doivent être absolus. Le [contrat de chargement Application](../../../../../../../VeroScriptApplication/Resources/Docs/User/fr-FR/index.md) décrit la configuration de l’environnement, des signatures et de la confiance.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
