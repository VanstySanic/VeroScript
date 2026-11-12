# 12 : Liaisons d'hôte et intégration de scripts

Un cas d'utilisation important de VeroScript consiste à l'intégrer dans un hôte C++. L'hôte crée un moteur ou une session et enregistre les fonctions et les types ; le script appelle la surface publique stable. Ce didacticiel utilise d'abord un catalogue de liaisons pour rendre cette limite concrète, puis décrit l'ordre d'intégration.

## Étape 1 : inspecter un catalogue de liaison

Un catalogue décrit l’API hôte en JSON. Copiez [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json) depuis ces ressources documentaires ou la distribution Resources séparée dans un répertoire modifiable, sous le nom `bindings.sample.json`. Les archives SDK ne contiennent pas d’exemples. Exécutez les commandes depuis ce répertoire :

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

Un catalogue comprend des champs de niveau supérieur tels que `version`, `functions`, `genericTypeTemplates`, `types`, `typeAliases` et `annotations`. Il décrit une interface ; il ne remplace pas le code d'enregistrement C++ et ne fournit pas d'implémentation d'exécution en soi.

Traitez le catalogue comme la liste d'interfaces visible par le compilateur : une fonction nécessite au moins `name`, `returnType` et `paramTypes` ; Les objets hôtes et les conteneurs ont également besoin des métadonnées de type d'hôte, d'élément ou de clé/valeur correspondantes. Les paramètres de référence utilisent `paramFlags` parallèlement à `paramTypes` et les annotations de paramètres utilisent `paramAnnotations` parallèle. Ces champs doivent correspondre à la signature d'enregistrement C++.

Par exemple, l'entrée de fonction `host_add` du catalogue d'exemple utilise deux paramètres `int` et annote le premier paramètre :

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

Ne traitez pas les tableaux de métadonnées du catalogue comme des implémentations d’exécution ; ils décrivent uniquement l'interface et la surface de l'outillage. Consultez la [Référence des liaisons d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md) pour chaque champ, les noms de types pris en charge, les modèles génériques, les alias et les limites JSON.

## Étape 2 : appeler les capacités de l'hôte à partir d'un script

Une fois que l'hôte a enregistré une fonction, un script peut l'appeler comme une déclaration ordinaire. Par exemple, supposons que l'hôte ait enregistré `HostLog` :

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

Pour un type avec des membres statiques, le script utilise normalement le nom du type :

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

Ces noms démontrent uniquement la forme de l'appel ; les noms réels doivent provenir de l’enregistrement et du catalogue de l’hôte.

## Étape 3 : intégrer dans l'ordre

Dans l'hôte C++, utilisez cette séquence :

Avant l’exécution, sélectionnez une VM compatible via `FEngineConfig::VmBackendFactory`/`VmBackendPath` ou l’API de Session. Lier le SDK C++ seul ne sélectionne pas d’interpréteur. Les processeurs de compilation script nécessitent aussi une VM. Consultez [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/fr-FR/index.md) pour les cibles SDK et la configuration.

1. Créez un `VeroScriptEngine` ou un `VeroScriptSession` de longue durée.
2. Enregistrez les fonctions natives, les types d'hôtes, les membres et les opérateurs.
3. Ajoutez de la documentation et des métadonnées de paramètres pour les API que les outils doivent exposer.
4. Exportez ou chargez le catalogue de liaisons et exécutez `bindings validate`.
5. Appelez d'abord `CheckSource`/`CheckFile`, puis `RunSource`/`RunFile` une fois la vérification réussie.
6. Conservez les diagnostics pour les échecs de vérification et d'exécution au lieu de renvoyer uniquement un booléen.

Les éditeurs, les jeux et les outils hôtes ayant une longue durée de vie de script utilisent normalement `VeroScriptSession` pour les modules, les liaisons, l'état d'exécution et le rechargement à chaud. L'exécution unique peut utiliser les points d'entrée source et fichier sur `VeroScriptEngine`.

## Étape 4 : garder l'interface stable

Traitez les liaisons comme une API versionnée. L'ajout d'une fonction est normalement une extension compatible ; la suppression de membres, la modification des types de paramètres, la modification des valeurs d'énumération ou la modification de la sémantique de propriété peuvent interrompre les scripts. Avant d'expédier une version hôte, vérifiez :

- Le catalogue de reliure valide.
- Les scripts passent `--check`.
- Le tableau d'inscription correspond au catalogue.
- Les durées de vie des objets, des structures et des handles sont documentées.

Si un membre hôte statique possède un paramètre explicite `HostObject`, testez
aussi la limite nullable avec `nullptr`. Le callback reçoit un handle d'objet
hôte vide et doit le vérifier avant de déréférencer la charge. Un récepteur
d'instance null reste invalide.

## Exercices

1. Concevez un nom stable, une liste de paramètres et un type de retour pour une fonction hôte.
2. Ajoutez une courte documentation et des descriptions de paramètres à la liaison.
3. Modifiez une entrée de catalogue et exécutez à la fois la validation du catalogue et la vérification du script.

Voir [Intégration d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md) et [Liaisons d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md) pour connaître les limites complètes.


Suivant : [Réflexion d'exécution](13-reflection.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/advanced/12-host-bindings.md) · [fr-FR](12-host-bindings.md) · [ja-JP](../../ja-JP/advanced/12-host-bindings.md) · [ko-KR](../../ko-KR/advanced/12-host-bindings.md) · [zh-CN](../../zh-CN/advanced/12-host-bindings.md) · [zh-TW](../../zh-TW/advanced/12-host-bindings.md)
