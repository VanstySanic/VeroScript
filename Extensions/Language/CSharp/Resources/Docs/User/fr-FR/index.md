# SDK C#

Voir aussi [C# AOT](aot.md) pour la génération de source C#, les bundles
déterministes et la publication NativeAOT.

`VeroScriptCSharp` est l'adaptateur .NET compatible NativeAOT de l'ABI C Interop VeroScript. Il fournit sessions, valeurs, callbacks, bindings hôte, paquets, réflexion, localisation et statistiques JIT. Le package managé cible .NET 8 et .NET 9 ; `VeroScriptInterop` est une bibliothèque native séparée et doit correspondre à l'architecture du processus.

L'ABI Interop actuelle est `0x00000101` (ABI 0.1.1). L'adaptateur managé exige une version ABI exactement identique. Vérifiez `VeroScriptRuntime.AbiVersion` avant de créer une session et déployez le package managé et la bibliothèque native depuis des versions correspondantes.

```CSharp
using System;
using VeroScript;

using var session = new VeroScriptSession(new()
{
	Print = Console.WriteLine,
	Diagnostic = Console.Error.WriteLine,
});
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
});

var loaded = session.LoadSource("""
    int Add(int left, int right)
    {
        return left + right;
    }

    int main()
    {
        print(Add(20, 22));
        return 0;
    }
    """, "main.vs");
if (!loaded.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "load failed");
}

var run = session.Run();
if (!run.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "execution failed");
}
```

`VeroScriptSessionOptions.Print` reçoit les lignes de sortie du script et `VeroScriptSessionOptions.Diagnostic` les lignes de diagnostic. Ces callbacks sont facultatifs et restent référencés jusqu’à la libération de la session.

Voir [Sessions et runtime](session.md), [Valeurs et propriété](values.md), [Liaisons hôte](host-bindings.md) et [Paquets, réflexion et localisation](packages.md). Cette section décrit la frontière C# ; la référence du langage décrit la syntaxe VeroScript.

Voir également [C# AOT](aot.md) pour la génération déterministe de source et de bundles C#.

Voir aussi [Frontend C#](frontend.md) et [C# BindGen](bindgen.md) pour l'analyse
des déclarations, les catalogues et les ponts AOT.

Voir aussi le [transpileur C#](transpiler.md) pour la conversion sémantique du
sous-ensemble C# pris en charge vers une source VeroScript.

- [Android et iOS / MAUI](mobile.md)

## Mémoire gérée

Le runtime doit annoncer `VeroScriptCapabilities.Memory` ; un runtime compatible plus ancien lève `NotSupportedException` avant les appels natifs ou l’épinglage. Utilisez `context.Memory` dans les callbacks et `session.Memory` au repos. Un gestionnaire `Memory<byte>` personnalisé doit prendre en charge `Pin` et permettre `Unpin` sur le thread qui libère la dernière référence ; un échec d’enregistrement libère l’épinglage.

Les opérations mémoire d’un callback et `GetArgumentAddress` doivent s’exécuter sur son thread managé ; un accès depuis un autre thread signale `WrongThread`. Un bail peut être libéré sur un autre thread une fois son utilisation terminée.

API hôte : `session.Memory` / `SetMemoryLimits`. La référence [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md) décrit la propriété, les quotas, l'expiration des vues et les emprunts.

- [Mémoire gérée depuis l’hôte](memory.md)

## Langue des diagnostics des outils source

Le frontend C# et les outils C# BindGen/Transpiler émettent actuellement leur aide et leurs diagnostics en anglais ; les diagnostics Roslyn sont recueillis avec une culture invariante. Les CLI BindGen/Transpiler n’acceptent pas `--lang`. `--language-version` choisit la version syntaxique C#, pas la langue d’affichage. La langue des diagnostics natifs choisie par `VeroScriptRuntime.CurrentLanguage` est indépendante.

## variant

Voir [SDK d'interopérabilité — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#exported-value-lifetime).

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

Chargez une application avec `VeroScriptSession.LoadApplication(absolutePath, optionsJson)` et vérifiez le résultat avant d’appeler `Run()`. Les chemins des options hôte doivent être absolus. Le [contrat de chargement Application](../../../../../../../VeroScriptApplication/Resources/Docs/User/fr-FR/index.md) décrit la configuration de l’environnement, des signatures et de la confiance.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
