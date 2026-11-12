# iOS — Intégration mobile expérimentale

Le XCFramework statique expérimental cible iOS 15+, arm64 appareil et arm64/x86_64 simulateur. Importez `VeroScriptIOS`, liez Foundation/libc++ ; ne l’embarquez pas comme framework dynamique. Conservez la hiérarchie `assets/stdlib` dans le bundle et vérifiez les URL. `VSMConfiguration` copie options et `sourceResources` ; recréez l’engine pour une mise à jour. Labels absolus cohérents avec les imports, cache accessible en écriture propre à l’app, jamais le bundle. Exécutez sur une file worker ; ARC garde l’engine vivant pendant execute/cancel. Chaque appel crée un état neuf ; annulez puis attendez avant libération. `outputUTF8`/`diagnosticsUTF8` sont des `NSData` conservant NUL ; une troncature peut couper un caractère. Ce SDK reste expérimental. Vérifiez l’intégration de votre application sur les simulateurs et appareils physiques visés avant déploiement.

[Intégration mobile expérimentale](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md)

## Localisation mobile explicite

[Localisation mobile explicite](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md)

## Quotas du stockage d’octets géré

`VSMConfiguration.maxBufferBytes` limite un tampon d’octets géré ; `VSMConfiguration.maxRetainedBytes` limite le stockage sous-jacent conservé. Les valeurs par défaut sont respectivement 16 MiB et 32 MiB ; zéro désactive la limite correspondante. Configurez-les avant de créer `VSMEngine`, qui copie sa configuration.

## Exécution native contrôlée et AOT statique

Voir [Intégration mobile expérimentale — Exécution native contrôlée et AOT statique](../../../../../VeroScriptMobile/Resources/Docs/User/fr-FR/index.md#mobile-jit-selection).

Voir [Intégration mobile expérimentale — Exécution native contrôlée et AOT statique](../../../../../VeroScriptMobile/Resources/Docs/User/fr-FR/index.md#mobile-core-aot).

<a id="ios-native-selection"></a>

Sur iOS, créez le programme avec `VSMAotProgram.programWithFactory:error:` et configurez `VSMConfiguration.aotPrograms` / `aotProgramId` ; exécutez le paquet généré avec `executePackage`. Les catalogues conservent les programmes ; le code d’entrée reste lié statiquement et les ID ne contiennent pas d’adresses de fonctions. `VSMAotProgramFactory` reçoit des pointeurs de sortie non nuls dont les valeurs programme/erreur peuvent être nulles. La factory renvoie `vs_interop_status` ; la méthode Objective-C renvoie un programme nullable et peut fournir une NSError via le paramètre d’erreur optionnel. Vérifiez le programme renvoyé avant utilisation ; s’il vaut nil, consultez la NSError si un paramètre de sortie d’erreur a été fourni.

## Ressources du bundle et exécution

<a id="ios-application-resources"></a>

Liez le `VeroScriptIOS.xcframework` statique avec Foundation/libc++ ; son module expose aussi `VeroScriptInterop.h` aux consommateurs de l’ABI C. Copiez la bibliothèque standard sans aplatir les répertoires. Dans l’exemple, `stdlibURL` est l’URL vérifiée du répertoire déployé et `cacheURL` un répertoire existant, accessible en écriture et appartenant à l’application. Appelez depuis un worker hôte. Le moteur copie la configuration, dont `sourceResources` ; les labels virtuels absolus doivent correspondre aux imports. Recréez le moteur après une mise à jour des ressources.

```objc
@import VeroScriptIOS;

// Call on a host worker queue after validating both URLs.
static VSMExecutionResult* RunExample(NSURL* stdlibURL, NSURL* cacheURL, NSError** error)
{
	VSMConfiguration* config = [VSMConfiguration new];
	config.stdlibDirectory = stdlibURL.path;
	config.cacheDirectory = cacheURL.path;
	VSMEngine* engine = [[VSMEngine alloc] initWithConfiguration:config error:error];
	if (!engine)
	{
		return nil;
	}
	NSData* source = [@"void main(){print(42);}" dataUsingEncoding:NSUTF8StringEncoding];
	return [engine executeSource:source fileLabel:@"/veroscript/scripts/main.vs" error:error];
}
```

Pour cette source, le succès donne `success == YES`, le code de sortie 0 et les octets `0x34 0x32 0x0A` dans `outputUTF8`. Un résultat nil indique un échec API : consultez la NSError demandée. Un résultat non nil avec `success == NO` contient les diagnostics/erreurs du script. Conservez NSData pour préserver NUL ou une fin UTF-8 tronquée. Ces résultats attendus ne remplacent pas la validation de l’application.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
