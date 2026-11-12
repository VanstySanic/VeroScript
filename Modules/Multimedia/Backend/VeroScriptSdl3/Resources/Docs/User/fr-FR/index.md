# VeroScriptSdl3 — Fondations

L’hôte de scripts doit sélectionner explicitement un moteur VM ; enregistrer les liaisons Platform, Graphics ou Audio ne le sélectionne pas. Lancez l’exemple source avec `VeroScriptVisualExample --vm-backend <bibliothèque-VM-ou-répertoire> program.vs`. `--check program.vs` vérifie les scripts ordinaires sans VM. Si une VM est fournie, la même fabrique est transmise à la compilation et à l’exécution, y compris aux processeurs de compilation de scripts. Les SDK de services natifs restent indépendants des implémentations VM.

Le fournisseur SDL3 utilise SDL 3.4.18 et l’identifiant `sdl3`. Il fournit `platform/sdl3`, ainsi que `graphics/sdl3` et `audio/sdl3` si ces capacités sont incluses dans le paquet installé. Platform, Graphics et Audio partagent le runtime SDL.

L’enregistrement seul n’initialise pas SDL. Créez et utilisez les services sur le thread principal/propriétaire de l’application. Le rendu hors écran nécessite encore un pilote vidéo/graphique utilisable. Les ressources conservées maintiennent le fournisseur en vie ; terminez leur retrait sur le thread propriétaire avant l’arrêt. La disponibilité dépend du SDK installé, du système et du périphérique.

## Utiliser le SDK natif installé

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

Voir [VeroScriptGraphics — Ressources et dessin 2D](../../../../../../VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md#graphics-service-contract).

Voir [VeroScriptGraphics — Ressources et dessin 2D — Utiliser le SDK natif installé](../../../../../../VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md#vertex-color-mode).

## Périphériques d’entrée

[Platform: Plusieurs périphériques d’entrée](../../../../../../VeroScriptPlatform/Resources/Docs/User/fr-FR/index.md)

Voir [VeroScriptPlatform — Fondations — Plusieurs périphériques d’entrée](../../../../../../VeroScriptPlatform/Resources/Docs/User/fr-FR/index.md#input-device-snapshots).

Déconnexion, perte de focus, cycle de vie et destruction de fenêtre effacent l’état concerné, avec relâchements/annulations synthétiques avant l’événement déclencheur. `ClearInputState(windowId)` efface une fenêtre, ou toutes pour zéro, retire les entrées normalisées en attente et produit `InputStateReset`. Un reset soumis par l’hôte conserve l’ordre des événements suivants. Un reset global synthétique peut signaler une perte de notifications : effacer captures/pressions locales et réénumérer. Les services externes ont des états indépendants alimentés par l’hôte. SDL réconcilie les périphériques tactiles lors du traitement/énumération toutes les 250 ms ; clavier/souris suivent les événements SDL ordonnés. Les stylets dédiés ne sont pas implémentés. Versions inchangées ; reconstruire ensemble hôtes et fournisseurs natifs.

Voir [VeroScriptPlatform — Fondations — Plusieurs périphériques d’entrée](../../../../../../VeroScriptPlatform/Resources/Docs/User/fr-FR/index.md#input-device-snapshots).

SDL déduplique les transitions à partir d’un état global des codes physiques. Des claviers physiques maintenant la même touche peuvent produire une répétition apparente et une libération manquante. Les identifiants ou le mode raw ne garantissent donc pas un état physique complet par clavier. Les instantanés suivent les événements reçus ; utiliser un hôte externe fournissant toutes les transitions pour cette garantie.

## Fenêtres hôtes, curseurs et capture

Les scripts disposent de Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture() et IsBorrowed(). Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows indiquent les interfaces disponibles ; le pilote peut refuser une opération. SDL exige que la fenêtre possède les focus souris et clavier. Perte de focus, masquage, réduction, arrière-plan, arrêt et réinitialisation libèrent la capture. Le curseur configuré suit le focus souris ou la capture. Un glissement capturé le conserve hors fenêtre ; sinon, la sortie rétablit le curseur visible par défaut. Le programme hôte coordonne la propriété globale du curseur et de la capture SDL. En C++, IService.AttachWindow reçoit FBorrowedWindowOptions : HandleKind="sdl3.window", adresse SDL_Window valide et Owner partagé non nul. Les scripts ne reçoivent aucune adresse. Close désinscrit le wrapper sans détruire la fenêtre hôte ; sa destruction native invalide le wrapper et efface les entrées observées. Fermez les surfaces avant la destruction de la fenêtre native. Owner reste conservé jusqu’à la destruction du dossier de retrait ; il ne peut empêcher une destruction explicite anticipée par l’hôte.

Le curseur système exige un curseur natif par défaut. Le SDL fourni annonce la capture explicite uniquement pour windows, cocoa et x11. Les autres pilotes conservent la capture logique GUI sans demander de capture SDL non prise en charge.

<a id="host-window-retirement"></a>

Le backend SDL libère sa réservation GPU pendant l’événement de destruction native, même si le retrait de la surface attend le thread propriétaire ; Close reste idempotent. SDL_EVENT_WINDOW_DESTROYED doit rester activé et non filtré. Un hôte qui supprime cet événement doit fermer les surfaces et wrappers avant la fenêtre native.

## API graphiques et requêtes asynchrones

<a id="sdl-graphics-drivers"></a>

Choisissez un pilote disponible dans le fournisseur installé : `direct3d11` ou `direct3d11-warp` sous Windows, `opengl` (3.3 core), `opengles` (ES 3.0), ou `webgpu` facultatif. SDL_GPU fournit Direct3D 12, Vulkan ou Metal sur les cibles prises en charge. Interrogez le service et traitez un pilote absent comme une erreur.

Créer une fenêtre compatible avec FWindowOptions.GraphicsApi ou Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi). Une fenêtre empruntée doit déjà être compatible.

Pour Android et le pilote `opengles` optionnel iOS/iPadOS/tvOS, créez exactement une fenêtre Platform compatible avec `GraphicsApi="opengles"` avant le périphérique. Les contextes EGL Android peuvent conserver cette fenêtre indépendamment : la récupération prépare un remplacement tout en gardant l’ancien périphérique vivant ; une seule surface possède la présentation à la fois. UIKit conserve un contexte par fenêtre. Les surfaces doivent appartenir à la fenêtre conservée. Fermez surfaces et ressources avant périphériques/fenêtres ; fermer la fenêtre invalide ses périphériques. Le pilote SDL_GPU par défaut suit un autre chemin d’hébergement.

Voir [VeroScriptGraphics — Ressources et dessin 2D — API graphiques et requêtes asynchrones](../../../../../../VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md#asynchronous-requests).

Voir [VeroScriptGraphics — Ressources et dessin 2D — API graphiques et requêtes asynchrones](../../../../../../VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md#resource-retirement).

WebGPU nécessite le runtime natif ou le port navigateur correspondant et leurs notices originales. Le navigateur exige la création asynchrone et des retours à sa boucle ; évaluez séparément le support natif et navigateur selon le SDK et la cible.

CreateDevice et CreateDeviceAsync partagent la sélection par défaut. Une compilation native limitée à WebGPU utilise une requête asynchrone même sans nom de pilote. DX11 Device.Poll détecte la perte et invalide les ressources ; la récupération exige toujours leur reconstruction. WebGPU accepte une annulation avant/après la fin et répétée ; GetPixels en échec conserve sa sortie.

## Manettes, joysticks et vibration

Voir [VeroScriptPlatform — Fondations — Manettes, joysticks et vibration](../../../../../../VeroScriptPlatform/Resources/Docs/User/fr-FR/index.md#controllers-and-feedback).

## Audio

Si l’audio est inclus, le fournisseur SDL fournit `audio/sdl3` (`veroscript.audio`, 0.1.1). `CreateAudio` initialise uniquement l’audio dans le même runtime SDL. `VS::Audio` fournit des flux de lecture/enregistrement, des instantanés de périphériques, des changements de topologie regroupables, des files PCM bornées et le contrôle du gain/de la pause. SDL convertit fréquence et canaux ; le module Audio décode le RIFF/WAVE PCM/flottant non compressé sans périphérique.

Les flux démarrent en pause et sont indépendants. Un débordement d’enregistrement vide les données non lues et conserve son indicateur ; le thread audio n’exécute aucun rappel script. Fermez les flux et terminez leur retrait sur le thread propriétaire. Le SDK Linux déployé doit inclure un pilote réel, par exemple ALSA/PulseAudio. Permissions du microphone, gestes utilisateur du navigateur et disponibilité matérielle restent à vérifier sur la cible.

<a id="audio-event-pumping"></a>

Appelez régulièrement `PollEvent()` ou `GetDevices()` sur le thread propriétaire. L’adaptateur SDL3 traite les rappels du thread principal et les notifications natives sans retirer les événements SDL de l’hôte, y compris dans une application audio sans fenêtre. L’état de connexion repose sur les périphériques actuellement énumérés : un format conservé par un ancien flux ne prouve pas que le périphérique est connecté.

<a id="uikit-device-recovery"></a>

Sur iOS/iPadOS/tvOS, le périphérique `opengles` annonce `bDeviceRecovery == false` (`SupportsDeviceRecovery()` en script). Sa fenêtre ne permet qu’un contexte EAGL conservé : ne préparez pas de remplacement à côté de l’ancien périphérique avec RecoverDevice/RecoverDeviceAsync. Fermez et libérez surfaces, ressources et périphérique anciens, videz le retrait différé sur le thread propriétaire, puis créez un nouveau périphérique et rechargez les données. Les autres pilotes ont leurs propres capacités ; les règles EGL Android ne prouvent pas la récupération UIKit.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
