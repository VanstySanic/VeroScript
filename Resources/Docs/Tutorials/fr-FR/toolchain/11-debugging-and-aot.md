# 11 : Débogage et génération AOT

Ce didacticiel couvre l'inspection d'un script avec le débogueur et le choix d'une cible AOT adaptée au déploiement.

## Étape 1 : préparer un script observable

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

Enregistrez-le sous `debug_demo.vs`, puis vérifiez-le normalement :

```powershell
veroscript --check debug_demo.vs
```

## Étape 2 : démarrer une session de débogage DAP

Le mode d'entrée/sortie standard convient à un éditeur ou à un autre client de débogage pour lancer et gérer :

```powershell
veroscript --debug debug_demo.vs
```

Pour un client qui se connecte via TCP :

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

Configurez les points d'arrêt, les étapes et l'inspection des variables dans le client. Le débogage utilise le chemin de débogage de la VM. N'utilisez donc pas le comportement au moment du débogage comme mesure des performances pour une exécution ordinaire.

Placez les points d'arrêt source sur du code actif. Dans un client DAP,
envoyez-les avec `setBreakpoints`. Un point défini avant le lancement reste en
attente ; une fois le programme chargé, le client reçoit un événement
`breakpoint` avec `reason: "changed"` et le même ID. Utilisez toujours l'état
vérifié et la ligne renvoyée. Une ligne retirée par `#if`, hors de la source ou
sans emplacement exécutable reste non vérifiée.

## Étape 3 : générer la source AOT

Lorsqu'un script n'a pas besoin de chargement dynamique, choisissez la cible correspondant au déploiement. C++17 est la cible par défaut :

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp` n'est pas un exécutable lié. Le projet hôte a toujours besoin d'un compilateur C++17 compatible et de la prise en charge du runtime VeroScript. Inspectez le fichier généré et construisez-le dans le projet cible plutôt que d'essayer de l'exécuter directement.

Pour un programme Python autonome, sélectionnez Python explicitement :

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

Pour une application C# ou NativeAOT, produisez un bundle :

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

Consultez [AOT Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/aot.md) et [AOT C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/aot.md)
pour les sous-ensembles et exclusions. Une construction non prise en charge
produit un diagnostic plutôt qu'une sortie qui changerait la sémantique.

## Choisir un mode

- Itération rapide, rechargement à chaud et débogage interactif : VM ou backend mixte par défaut.
- Points d'arrêt et inspection des variables : `--debug`.
- Intégration native : utilisez AOT C++ et vérifiez les liaisons d'hôte et la concurrence dans le projet cible.
- Source Python autonome : utilisez `--target=python` et testez avec la version Python cible.
- Source .NET ou NativeAOT : utilisez `--target=csharp` et construisez le bundle généré.

## Exercices

1. Définissez un point d'arrêt dans `calculate` et inspectez `input` et `result`.
2. Modifiez l'entrée et comparez la sortie normale et la sortie de débogage.
3. Générez la source AOT C++ pour un script utilisant un tableau et confirmez que le projet cible peut la compiler.
4. Générez la sortie Python ou C# du même script et comparez les diagnostics de limites.

Voir [Débogage](../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md), [AOT C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/aot.md), [AOT Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/aot.md) et [AOT C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/aot.md). Suivant : [Liaisons d'hôte et intégration de script](../advanced/12-host-bindings.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/11-debugging-and-aot.md) · [fr-FR](11-debugging-and-aot.md) · [ja-JP](../../ja-JP/toolchain/11-debugging-and-aot.md) · [ko-KR](../../ko-KR/toolchain/11-debugging-and-aot.md) · [zh-CN](../../zh-CN/toolchain/11-debugging-and-aot.md) · [zh-TW](../../zh-TW/toolchain/11-debugging-and-aot.md)
