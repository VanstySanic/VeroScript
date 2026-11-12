# VeroScriptSdl3

<a id="provider-boundary"></a>

Les contrats publics Platform/Graphics/Audio restent indépendants de SDL et Core. L’implémentation SDL, le runtime, les options de pilotes et les notices appartiennent à VeroScriptSdl3. Vérifiez contrats compatibles, SDK installés/déplacés, durées de vie des modules et retrait sur le thread propriétaire. Les contrôles source ne remplacent pas la validation réelle des périphériques et plateformes.

## Compilation et intégration

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## Maintenance et vérification

`VEROSCRIPT_SDL3_GRAPHICS` sélectionne l’enregistrement graphique. Les pilotes sont contrôlés séparément par `VEROSCRIPT_SDL3_GPU`, `VEROSCRIPT_SDL3_D3D11`, `VEROSCRIPT_SDL3_OPENGL` et `VEROSCRIPT_SDL3_WEBGPU` ; WebGPU est désactivé par défaut. Contrôleurs et audio utilisent `VEROSCRIPT_SDL3_CONTROLLERS` et `VEROSCRIPT_SDL3_AUDIO`. WebGPU natif utilise wgpu-native v29.0.1.1 via `VEROSCRIPT_WEBGPU_ROOT` ; le navigateur utilise le port Emdawn figé. Séparez résultats réels et support de construction.

<a id="sdl-audio-build"></a>

La construction audio racine utilise `VEROSCRIPT_BUILD_AUDIO=ON` ; SDL exige aussi `VEROSCRIPT_BUILD_SDL3=ON` et `VEROSCRIPT_SDL3_AUDIO=ON`. L’entrée autonome des fondations prend également Audio en charge. Validez les pilotes Linux réels séparément des périphériques factices.

`VEROSCRIPT_SDL3_CONTROLLERS` vaut ON par défaut ; OFF désactive seulement le support natif. Chaque périphérique est ouvert une fois, et sa libération finale est exécutée sur le thread propriétaire même après abandon ailleurs. La politique d’entrée en arrière-plan de l’hôte reste inchangée. Gyroscopes, surfaces tactiles des manettes, boules de commande et retour de force avancé sont exclus. L’audio est activable séparément avec `VEROSCRIPT_SDL3_AUDIO` ; caméra, SDL_Renderer et Haptic séparé restent désactivés. Versions inchangées : reconstruire ensemble hôte, fournisseurs, adaptateurs et SDK.

`VeroScriptSdl3ControllerTests` vérifie énumération, branchement à chaud, configuration, états indépendants, vibration, limites et destruction avec des périphériques virtuels SDL. Les tests mémoire et scripts couvrent le contrat public. Les manettes physiques et pilotes Apple/mobile/navigateur demandent encore une validation matérielle.

`VeroScriptSdl3OpenGl.cpp` utilise la fenêtre hôte pour `SDL_PLATFORM_ANDROID`, `SDL_PLATFORM_IOS` et `SDL_PLATFORM_TVOS` : OpenGL ES et exactement une fenêtre Platform compatible, sans fenêtre auxiliaire cachée. Android compte séparément contextes EGL conservés et droits de présentation des surfaces, préservant l’ancien périphérique avant validation du remplacement/annulation. UIKit garde le contexte exclusif. Initialisez Android via SDLActivity/`SDL_main` et son thread natif d’événements ; la disponibilité du thread UI Java ne constitue pas cette initialisation. Les autres plateformes exigent toujours le thread principal. Respectez les threads propriétaires pour fenêtres, périphériques, événements et retrait. Validez SDK/appareils séparément.

[README](../../../../README.md) · [User](../../User/fr-FR/index.md)

Les tests synthétiques contournent cette déduplication SDL.

`VEROSCRIPT_SDL3_GRAPHICS` est calculé à partir des pilotes activés ; configurez les options de pilotes plutôt que cette valeur dérivée.

SDL_GPU réutilise les paires de tampons de sommets et d’indices retirées : au plus 128 paires / 8 Mio, également bornées par MaxGeometryBytes. Le tampon de transfert est réutilisé ; le mappage et les transferts vers la destination utilisent le cycling pour préserver les données des commandes GPU en attente. Fermer le moteur libère le stockage inactif. Les géométries restantes conservent leurs règles de propriété et de retrait sur le thread propriétaire.

<a id="uikit-recovery-capability"></a>

Alignez `Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities` sur la propriété des contextes conservés : sous SDL_PLATFORM_IOS/SDL_PLATFORM_TVOS, seul opengles désactive bDeviceRecovery. Ne déduisez pas la récupération de l’ID fournisseur ou des sept autres capacités actives. UIKit ne crée pas de remplacement avec l’ancien propriétaire EAGL présent ; les contextes retenus indépendamment sur Android ont un autre contrat. Rapports de capacités et documentation publique doivent évoluer ensemble.

<a id="native-provider-maintenance"></a>

Le fournisseur possède les descripteurs SDL et la requête statique `veroscript_sdl3_query` ; le build d’extension native expose `veroscript_extension_query`. N’enregistrez que les services/pilotes construits, en vérifiant séparément chaque contrat et capacité réelle. Gardez shaders, dépendances des pilotes et leases avec SDL3, hors des contrats indépendants. Validez découverte statique/dynamique, entrées, graphismes, audio et retrait sur thread propriétaire avec des consommateurs correspondants ; les pilotes synthétiques/factices ne certifient pas les périphériques physiques.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
