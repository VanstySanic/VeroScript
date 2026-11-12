# 01 : Générer des artefacts de liaison C#

Produisez catalogue, référence API, source d’enregistrement et bridge AOT à partir des mêmes déclarations gérées. Le frontend sémantique analyse le code à la construction ; il ne crée pas de session script.

Placez les outils appariés `veroscript-csharp-bindgen` et `veroscript` sur PATH. Utilisez un répertoire de leçon accessible en écriture et un projet hôte .NET compatible avec le SDK choisi. Exécutez les commandes depuis ce répertoire. Génération, compilation de l’hôte et enregistrement des callbacks sont des étapes distinctes.

L’exécution AOT ci-dessous utilise le projet généré `net8.0`. Installez un SDK .NET capable de construire cette cible et un runtime .NET correspondant pour l’exécuter.

## Étape 1 : préparer les entrées

Enregistrez la déclaration hôte dans `HostApi.cs` :

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

Enregistrez l’appelant dans `app.vs` :

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## Étape 2 : générer l’enregistrement actif

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

Les résultats attendus sont `bindings.json`, `Bindings.g.cs` et `Bindings.vs.txt`. Compilez l’enregistrement avec le SDK C# géré et le fichier `HostApi.cs` original ; appelez son entrée d’enregistrement générée avant de charger le script. Le catalogue décrit l’interface sans installer les callbacks. Dans un hôte configuré, cet appelant affiche `42` et renvoie zéro.

Dans l’hôte VM configuré, appelez cette entrée générée avant de charger `app.vs` ; `session` est la `VeroScriptSession` existante de l’hôte :

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## Étape 3 : générer le bridge AOT

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

Compilez `HostApi.cs`, `Bindings.Aot.g.cs` et toutes les unités applicatives générées dans le même projet .NET. Le bridge fait des appels gérés directs sans `VeroScriptInterop`. Conservez les dépendances runtime du projet généré ; la génération seule ne produit pas d’exécutable.

Le fichier généré `generated/app/App.csproj` fixe `EnableDefaultCompileItems=false` et énumère ses unités. Ajoutez ce `ItemGroup` dans son élément `Project` en conservant les éléments existants. Les chemins sont relatifs à `App.csproj` :

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

Exécutez l’application gérée depuis le répertoire de la leçon. La sortie attendue est `42`, avec un code de sortie nul :

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

Ce projet AOT utilise `Bindings.Aot.g.cs`. `Bindings.g.cs` appartient à l’hôte VM de l’étape 2. Régénérer le bundle remplace `App.csproj` ; ajoutez de nouveau ces entrées après régénération.

## Étape 4 : générer un projet

Créez `Models/State.cs` avec la déclaration suivante et conservez `HostApi.cs` de l’étape 1 :

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

Passez les deux fichiers ensemble et gardez catalogue, enregistrement, bridge, références et manifeste de la même génération. Les déclarations partielles/liées sont fusionnées avant sortie. Les fichiers de même nom de base reçoivent un suffixe stable dérivé du chemin.

Pour reconstruire l’application AOT après cette génération de projet, répétez d’abord la commande AOT de l’étape 3 avec le nouveau catalogue fusionné. Ajoutez de nouveau les entrées du projet, dont `<Compile Include="../../Models/State.cs" />`, avant de construire. Catalogue, bridge, déclarations hôtes et application générée doivent correspondre à ce même jeu d’entrées.

## Étape 5 : vérifier le contrat

Comparez noms et signatures du catalogue avec la source hôte et inspectez la référence API. Les déclarations publiques sont sélectionnées par défaut. Définissez/utilisez `[VeroScriptBinding]` avec `--require-attribute` pour sélectionner les types marqués ; `[VeroScriptIgnore]` exclut une déclaration. Les signatures nullable, pointeur ou propres à un adaptateur non prises en charge produisent des diagnostics. Utilisez un wrapper explicite plutôt que de masquer un écart dans les métadonnées générées.

## Exercices

1. Ajoutez une propriété en lecture seule prise en charge et inspectez le contrat.
2. Définissez un attribut de liaison, marquez un type et comparez avec `--require-attribute`.
3. Identifiez l’hôte qui consomme chaque enregistrement et bridge.
4. Ajoutez une signature nullable non prise en charge, lisez le diagnostic puis restaurez l’API.

[Référence BindGen](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/bindgen.md) · [AOT C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/aot.md) · [Conversion de source C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/transpiler.md) · [Valeurs et propriété runtime](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/csharp/01-csharp-source-tools.md) · [fr-FR](01-csharp-source-tools.md) · [ja-JP](../../ja-JP/csharp/01-csharp-source-tools.md) · [ko-KR](../../ko-KR/csharp/01-csharp-source-tools.md) · [zh-CN](../../zh-CN/csharp/01-csharp-source-tools.md) · [zh-TW](../../zh-TW/csharp/01-csharp-source-tools.md)
