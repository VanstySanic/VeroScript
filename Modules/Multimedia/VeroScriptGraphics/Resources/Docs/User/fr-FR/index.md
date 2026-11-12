# VeroScriptGraphics — Ressources et dessin 2D

<a id="graphics-service-contract"></a>

Graphics expose avec VS::Graphics les périphériques, textures, échantillonneurs, géométries indexées et Renderer2D. Les contrats natifs ne contiennent aucun type SDL/Core. Le module et son contrat de service natif utilisent la même version 0.1.1. Recompilez ensemble hôtes et fournisseurs.

Les requêtes de ressources, de capacités, de dimensions et de résultats de requêtes terminées (`GetDevice` et `GetPixels`) sont des méthodes const, utilisables dans une capture par valeur en lecture seule. Le dessin, les mises à jour, la progression ou l’annulation des requêtes et la fermeture des ressources nécessitent un récepteur modifiable. `GetError` fait également progresser sa requête ; utilisez `mutable` ou une capture par référence modifiable pour ces opérations.

La géométrie est transférée une fois puis réutilisée. Un sommet contient huit double : X,Y,U,V,R,G,B,A ; les indices int64 sont vérifiés. UpdateTexture transfère un array<uint8> RGBA compact. CreateSampler choisit Filter et les AddressMode U/V. DrawList2D conserve SetTexture, SetTint, SetTransform et SetBlendMode lors de AddGeometry. PushClip calcule les intersections et PopClip restaure le rectangle précédent.

Les positions sont des pixels locaux au viewport, Y vers le bas ; les clips sont des pixels absolus. SetTransform reçoit seize double en ordre colonne. Le GUI utilise Platform DisplayScale pour passer aux pixels. Les couleurs des sommets sont linéaires, non prémultipliées par défaut ; Tint reste non prémultiplié. StraightAlpha utilise des textures non prémultipliées, PremultipliedAlpha des textures prémultipliées. Un calque translucide sur du noir transparent contient des pixels prémultipliés : composez-le avec PremultipliedAlpha. Les formats règlent la conversion sRGB.

RenderTexture et RenderSurface prennent target,list,clear,R,G,B,A ; les variantes Viewport ajoutent x,y,width,height. Effacer concerne toute la cible. Initialisez une texture avant de conserver son contenu ; une fenêtre exige un effacement. Une image indisponible peut réussir sans présentation. Fermez les ressources avant le périphérique sur le thread propriétaire ; Clear libère les références. Les ressources fermées, étrangères et le retour de la cible sur elle-même sont refusés. Limites : texture/transfert aligné 256 MiB, géométrie 64 MiB, commandes et clips 65,536. La lecture synchrone utilise l’API C++ ; les scripts utilisent `Device.ReadTextureAsync`, interrogent la requête et lisent les pixels seulement après sa réussite.

```veroscript
VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
VS::Graphics::Device device = graphics.CreateDevice("");
VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
array<double> vertices = {0.0,0.0,0.0,0.0,1.0,0.0,0.0,1.0,
						 32.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,
						 0.0,32.0,0.0,1.0,1.0,0.0,0.0,1.0};
array<int64> indices = {0,1,2};
VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
list.PushClip(0,0,64,64);
list.AddGeometry(geometry);
list.PopClip();
VS::Graphics::Texture target = device.CreateTextureWithFormat(64,64,VS::Graphics::TextureFormat::Rgba8Unorm);
renderer.RenderTexture(target,list,true,0.0,0.0,0.0,0.0);
list.Clear();
geometry.Close();
target.Close();
renderer.Close();
device.Close();
```

Les surfaces SDL exigent la composition SDR_LINEAR pour un mélange linéaire cohérent ; une composition non prise en charge fait échouer leur création.

## Utiliser le SDK natif installé

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode choisit VertexColorMode::Straight (par défaut) ou Premultiplied indépendamment de Blend. Les couleurs déjà prémultipliées le restent pendant l’interpolation ; Tint reste non prémultiplié ; en sortie PremultipliedAlpha, son alpha multiplie RGB une seule fois. AddGeometry mémorise ce choix et Clear rétablit Straight. Les couleurs des sommets restent linéaires ; ce réglage ne décode pas sRGB.

## Composition et récupération manuelle

Composition2D enregistre AddDraw, BeginLayer/EndLayer, PushMask/PopMask, AddBlur, AddColorMatrix, AddShadow et AddBackdropBlur. Device.CreateCompositor2D() réutilise les cibles temporaires ; CreateCompositor2DWithLimits(bytes,depth) règle le budget, par défaut 256 MiB et 32 niveaux. Le constructeur limite à 64 niveaux et 65,536 commandes/dessins. L’opacité de groupe s’applique une fois après composition ; les masques alpha imbriqués, y compris inversés, s’intersectent. Le flou utilise deux passes séparables de 13 échantillons, rayon 0..64 pixels. Les ombres conservent le contenu ; le flou de fond remplace seulement la zone masquée. La matrice contient vingt valeurs par lignes. Les pixels composés sont en RGBA linéaire prémultiplié ; ClearColor est reçu non prémultiplié puis converti. Le chargement et les dessins Opaque doivent fournir des pixels prémultipliés compatibles. Le format gère sRGB. Une erreur de portée ou de budget préserve la cible finale. Trim libère les cibles ; fermez le compositeur avant le périphérique. DrawList2D.SetAlphaMask, SetBlur, SetColorMatrix et ResetEffect exposent les effets par passe ; Additive additionne les canaux prémultipliés. Les méthodes de surface prennent aussi width/height avant la composition ; les variantes Viewport ajoutent x,y,width,height. Une présentation échouée produit false. GetPooledBytes retourne int64. Device.GetGeneration et SupportsDeviceRecovery décrivent la récupération manuelle. Service.RecoverDevice(previous,driver) crée d’abord le remplacement et invalide l’ancienne génération seulement après réussite ; un échec laisse cette génération inchangée. Recréez et rechargez ensuite les ressources, fermez les anciennes surfaces avant de réutiliser leurs fenêtres, puis les anciennes ressources et le périphérique. Close reste possible après Invalidate. Le pilote choisi peut signaler une perte via Device.Poll ; récupération et restauration restent des actions explicites de l’hôte. Cette API ne fournit pas Renderer3D, les shaders utilisateur arbitraires ni l’enregistrement de commandes hôtes.

Les textures temporaires du compositeur utilisent par défaut le stockage RGBA8 sRGB ; échantillonnage et mélange restent linéaires. Cela évite de perdre les couleurs sombres sRGB dans un intermédiaire linéaire 8 bits. C++ permet de choisir explicitement RGBA8 UNORM avec sa quantification linéaire ; les deux constructeurs script utilisent sRGB. ColorMatrix et AlphaMask respectent Clamp/Repeat/Mirror pour les UV négatifs ou supérieurs à un. Blur et Shadow traitent l’extérieur du rectangle source comme transparent ; leur rayon se mesure en pixels source.

## API graphiques et requêtes asynchrones

Voir [VeroScriptSdl3 — Fondations — API graphiques et requêtes asynchrones](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/fr-FR/index.md#sdl-graphics-drivers).

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync renvoient DeviceRequest ; Device.ReadTextureAsync renvoie ReadbackRequest. Poll sur le thread propriétaire : Pending / Succeeded / Failed / Cancelled. GetDevice/GetPixels exigent un succès ; GetError expose le diagnostic du script. Les implémentations par défaut peuvent être synchrones. Dans le navigateur, WebGPU refuse création/lecture/WaitIdle synchrones avec graphics.async_required : rendre la main entre les sondages.

<a id="resource-retirement"></a>

La récupération invalide la génération précédente après succès ; recréer les ressources. Cancel abandonne le résultat sans garantir un arrêt natif. Fermer et libérer ressources, appareils et requêtes puis collecter sur le thread propriétaire jusqu’à Service.HasPendingRetirements() == false. Dans le navigateur, attendre les images suivantes. Conserver le service Platform propriétaire pendant le nettoyage.

<a id="graphics-capabilities"></a>

Les capacités appartiennent au périphérique/pilote sélectionné. C++ utilise `IDevice::GetCapabilities()` ; les scripts interrogent `SupportsGuiBasic`, `SupportsTransforms`, `SupportsClipMasks`, `SupportsFilters` et `SupportsDeviceRecovery`. Vérifiez la récupération avant de demander un remplacement et gérez l’échec ; le nom du fournisseur ne suffit pas. Les restrictions SDL3 sont dans [la récupération UIKit](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/fr-FR/index.md#uikit-device-recovery).

Les huit doubles d’un sommet script doivent tenir dans un float fini ; les indices int64 sont contrôlés avant conversion en uint32 natif. `UpdateTexture` consomme exactement largeur × hauteur × 4 octets RGBA compacts pour la région demandée et conserve leur encodage. `ResetTexture` rétablit le dessin uni. Modifications des listes et rendu utilisent le thread propriétaire sans concurrence. Les exceptions d’allocation peuvent traverser C++ ; l’adaptateur script traduit les échecs de callback en diagnostics. La lecture renvoie les octets RGBA stockés et conserve la sortie fournie en cas d’échec ; lecture native bloquante/WaitIdle restent hors des boucles de trame ordinaires.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
