# 10 : Construire des paquets et distribuer une application

Lorsqu'un script devient un programme livrable, conditionnez-le sous forme d'application ou de bibliothèque. `.vsapp` est destiné à être exécuté, tandis que `.vslib` est destiné à être importé par un autre script.

## Étape 1 : créer un module de bibliothèque

Créez `package_library.vs` :

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

Seules les déclarations exportées font partie de l'API de la bibliothèque. Construisez et inspectez la bibliothèque :

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## Étape 2 : créer le point d'entrée de l'application

Créez `package_app.vs` :

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

Créez et exécutez l'application :

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

Le paquet d'application contient son point d'entrée et ses dépendances, de sorte que le paquet de bibliothèque source n'a pas besoin de se trouver à côté de lui au moment de l'exécution. `package_app.vs` importe le module source par défaut ; `-D VERO_USE_BINARY_PACKAGE` le fait basculer vers le `DemoLibrary.vslib` généré, qui doit donc être disponible lors de la compilation de l'application.

## Étape 3 : revoir la limite publique

Une fonction non exportée peut être utilisée dans sa propre bibliothèque source, mais les scripts en aval ne peuvent pas l'appeler. Avant de publier un paquet, vérifiez :

1. Seuls les types et fonctions prévus sont exportés.
2. Les types de paramètres, les types de retour et les valeurs d'énumération sont stables.
3. La version du paquet est compatible avec le runtime et les liaisons cibles.
4. `package inspect` a été exécuté sur les fichiers générés.

Les exemples distribués contiennent un flux complet dans [`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md), [`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs) et [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs).

## Exercices

1. Ajoutez une fonction `subtract` exportée.
2. Supprimez `export` et observez l'erreur de compilation de l'application.
3. Définissez une version de paquet et enregistrez la commande de build exacte utilisée pour la publication.

Voir [Paquets et distribution](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/packages.md). Suivant : [Débogage et génération AOT](11-debugging-and-aot.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/10-packages-and-distribution.md) · [fr-FR](10-packages-and-distribution.md) · [ja-JP](../../ja-JP/toolchain/10-packages-and-distribution.md) · [ko-KR](../../ko-KR/toolchain/10-packages-and-distribution.md) · [zh-CN](../../zh-CN/toolchain/10-packages-and-distribution.md) · [zh-TW](../../zh-TW/toolchain/10-packages-and-distribution.md)
