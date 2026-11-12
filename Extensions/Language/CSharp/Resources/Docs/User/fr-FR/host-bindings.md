# Liaisons hôte C#

## Interception et mutations

Installez l'intercepteur avec `SetInstanceCallInterceptor` et le gestionnaire
de mutations avec `SetMutationHandler`. `CallMethodBypassingInterceptor` permet
d'appeler une opération sans passer par l'intercepteur.

`VeroScriptNativeType` décrit les types primitifs, hôte et paramétrés. Les fonctions déclarent leurs types de retour, paramètres et `VeroScriptParameterFlags`; les références et références constantes doivent correspondre au callback réel.

Les API d'enregistrement exposent types hôte, membres, types génériques et alias. `Construct`, `CallMethod`, `GetField` et `SetField` ne voient que les membres enregistrés. Un intercepteur renvoie `VeroScriptInterceptResult` pour continuer, remplacer ou refuser un appel ; il ne doit pas contourner la politique d'accès.

Une fabrique de type générique utilise `VeroScriptGenericHostTypeContext` et ses
méthodes `ArgumentCount`, `GetArgument`, `SetType`, `AddMember`, `SetProtocol`
et `SetError`. Le contexte d'interception expose `Receiver`, `ArgumentCount`,
`GetTarget`, `GetArgument`, `Proceed` et `SetError`; les valeurs empruntées ne
doivent pas sortir du callback.

Les sérialiseurs et `SerializeObject`/`DeserializeObject` doivent vérifier type, version et provenance des données. `SetMutationHandler` reçoit `VeroScriptMutationKind`; les contextes utilisent `GetField`/`SetBytes` pour la sérialisation et `EncodedVersion`/`GetBytes`/`AddField` pour la désérialisation. Évitez la réentrance de session et consommez les valeurs empruntées avant le retour. Une notification ne peut pas annuler une mutation déjà terminée.

Les exceptions managées ne doivent pas traverser l'ABI native. Utilisez les statuts ou `VeroScriptException` et libérez toute valeur possédée.

## Fonctions et callbacks

`RegisterFunction` déclare explicitement le type de retour et les types des paramètres :

```CSharp
session.RegisterFunction(
	"host_double",
	VeroScriptNativeType.Int32,
	[VeroScriptNativeType.Int32],
	(context, result) => result.Set(context.GetArgument(0).GetInt32() * 2));
```

`GetArgument` fournit un argument en lecture seule ; utilisez `GetMutableArgument` uniquement pour un paramètre déclaré comme référence modifiable. Le callback écrit son résultat dans la vue empruntée fournie et peut appeler `context.SetError(...)`. `ArgumentCount`, `ProgramGeneration` et `GetHostThis` décrivent le contexte d'appel. Les exceptions managées sont converties en échec de callback.

Pour les métadonnées détaillées, utilisez `VeroScriptHostFunctionDefinition` : documentation, noms et descriptions des paramètres, annotations, paramètres de type et indicateurs d'objet hôte générique. `ApplyClassDefaults` applique les valeurs par défaut enregistrées après la création ou le chargement d'un objet ; `EnumerateObjectReferences` visite les références script et hôte. Les valeurs de visiteur sont empruntées et expirent au retour du callback. `RetainCallable` crée une référence possédée à une fonction ou fermeture script.

Outre `RegisterFunction`, les API incluent `RegisterHostFunction`, `RegisterAnnotation`, `RegisterHostType`, `RegisterHostMember`, `RegisterHostProtocol`, `RegisterTypeAlias`, `RegisterGenericHostTypeTemplate` et `InstantiateGenericHostType`. `VeroScriptNativeParameter` associe un type à `None`, `Ref` ou `ConstRef` ; choisissez `Ref` uniquement si le callback écrit dans le stockage du script. `VeroScriptHostTypeDefinition` et `VeroScriptHostMemberDefinition` décrivent les types enum/struct/class, constructeurs, méthodes, propriétés, champs, constantes et opérateurs exposés.

## Casse des noms d'annotation

`RegisterAnnotation` utilise `VeroScriptAnnotationDefinition.CaseSensitive = true`
par défaut. La valeur `false` ignore la casse des lettres ASCII du nom concerné.
Les noms d'arguments et les clés de métadonnées restent exacts ; la réflexion
conserve l'orthographe source. Les inscriptions exactes et insensibles à la casse
qui se chevauchent sont rejetées sans modifier le catalogue.
`ExportBindingsJson()` conserve `caseSensitive`. L'inscription fournit aussi la
documentation au compilateur, à la réflexion et aux outils ; les processeurs
s'installent séparément. Déployez l'adaptateur avec le SDK natif actuel
correspondant, dont le descripteur d'annotation inclut ce nouveau champ.

## Signatures, appels imbriqués et persistance

`VeroScriptNativeType` distingue notamment `Void`, `Bool`, `String`, `HostObject`, `Enum` et `Function`. Les conteneurs et fonctions utilisent `TypeArguments`. Une signature native telle que `fn(int)->int` correspond au type source `function(int)->int` ; ne traduisez pas ces identifiants.

Dans un callback `VeroScriptCallContext`, `InvokeFunction` et `InvokeCallable` invoquent des cibles script sans repasser par une opération de session réentrante ; libérez leurs valeurs de retour possédées. Les arguments et le résultat emprunté restent limités au rappel courant. Les protocoles de clonage, égalité, hachage et sous-classe d’un type générique sont installés via la fabrique et conservés par la session.

`VeroScriptInterceptContext` peut renvoyer `Unhandled`, utiliser `Proceed`, fournir un résultat de remplacement ou renvoyer `Failed`. `RegisterExternalSerializer` enregistre la persistance externe. Le contexte de sérialisation fournit `TypeName`, `MutationVersion`, `FieldCount`, `GetField`, `SetBytes` ; celui de désérialisation fournit `EncodedVersion`, `GetBytes`, `AddField`, `SetError`. Vérifiez le type, la version et la provenance des octets ; cette API n’authentifie pas un paquet ou une bibliothèque native.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/host-bindings.md) · [fr-FR](host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
