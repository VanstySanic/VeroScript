# VeroScriptPlatform — Fondations

Platform définit les fenêtres, dimensions logiques／pixels, clavier, souris, tactile, texte UTF-8／IME et presse-papiers. Les contrats natifs ne dépendent ni de Core ni de SDL. Les adaptateurs de script facultatifs exposent `VS::Platform`.

Les requêtes de capacités, de dimensions de fenêtre et d’état des entrées sont des méthodes const, utilisables dans une capture par valeur en lecture seule. La consommation d’événements, la remise à zéro des entrées et les changements de fenêtre nécessitent un récepteur modifiable ; utilisez `mutable` ou une capture par référence modifiable.

Créez les services sur le thread principal. Traitez la file native partagée une seule fois, ou soumettez des événements normalisés en mode externe. Fermez les surfaces graphiques avant les fenêtres. Avant l’arrêt, récupérez les libérations différées sur le thread propriétaire.

## Utiliser le SDK natif installé

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

Les coordonnées de fenêtre suivent la plateforme native. `ScaleX/ScaleY` les convertissent en pixels, tandis que `DisplayScale` indique l’échelle UI recommandée en pixels. Ces facteurs peuvent différer. Les entrées indépendantes de la densité utilisent `guiX = windowX * ScaleX / DisplayScale`, et le dessin utilise `pixelX = guiX * DisplayScale`. Le script interroge `window.GetDisplayScale()`. Actualisez les mesures lorsque les pixels ou l’échelle changent.

## Plusieurs périphériques d’entrée

<a id="input-device-snapshots"></a>

`service.GetInputDevices()` renvoie un instantané avec `int64 Count` et `Get(index)`. Un périphérique expose `Kind`, `DeviceId`, `Name`, `Aggregate`, `Connected`. `service.GetInputState(kind,id)` fournit un instantané immuable : `Device`, `KeyCount`, `PointerCount`, `Modifiers`, `GetKey(index)`, `GetPointer(index)`, `IsKeyDown(Key)`, `IsScanCodeDown(int)`. Les champs des touches sont `Key/ScanCode/WindowId`, ceux des pointeurs `PointerId/WindowId/X/Y/Pressure/Buttons`. Les indices sont vérifiés. Capacités : `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()`.

L’identité est `(InputDeviceKind,DeviceId)` ; chaque contact appartient à son périphérique. Types : `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`. L’identifiant zéro désigne une entrée agrégée. Énumérer des souris physiques ne garantit pas des événements distincts par souris. L’état provient des événements consommés par ce service ; traiter la file avant de le lire. Les modificateurs de l’instantané reflètent ses propres touches observées, sans verrouillage système. Événements : `InputDeviceAdded/Removed`, avec `DeviceKind`, `DeviceName`, `AggregateDevice`, `Synthetic`.

Déconnexion, perte de focus, cycle de vie et destruction de fenêtre effacent l’état concerné, avec relâchements/annulations synthétiques avant l’événement déclencheur. `ClearInputState(windowId)` efface une fenêtre, ou toutes pour zéro, retire les entrées normalisées en attente et produit `InputStateReset`. Un reset soumis par l’hôte conserve l’ordre des événements suivants. Un reset global synthétique peut signaler une perte de notifications : effacer captures/pressions locales et réénumérer. Les services externes ont des états indépendants alimentés par l’hôte. Le fournisseur choisi définit la découverte native et les catégories disponibles ; consulter son guide pour les limites des événements physiques. Versions inchangées ; reconstruire ensemble hôtes et fournisseurs natifs.

La déconnexion d’un périphérique non nul libère aussi, par précaution, les entrées agrégées du même type : leur origine physique est inconnue. Les autres pressions agrégées nécessitent un nouvel événement pour être observées. Un changement de routage ou de disposition du clavier produit également une libération/annulation synthétique du propriétaire initial.

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## Fenêtres hôtes, curseurs et capture

Les scripts disposent de Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture() et IsBorrowed(). Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows indiquent les interfaces disponibles ; le pilote peut refuser une opération. SDL exige que la fenêtre possède les focus souris et clavier. Perte de focus, masquage, réduction, arrière-plan, arrêt et réinitialisation libèrent la capture. Le curseur configuré suit le focus souris ; sortir rétablit le curseur visible par défaut. Le programme hôte coordonne la propriété globale du curseur et de la capture SDL. En C++, IService.AttachWindow reçoit FBorrowedWindowOptions : HandleKind="sdl3.window", adresse SDL_Window valide et Owner partagé non nul. Les scripts ne reçoivent aucune adresse. Close désinscrit le wrapper sans détruire la fenêtre hôte ; sa destruction native invalide le wrapper et efface les entrées observées. Fermez les surfaces avant la destruction de la fenêtre native. Owner reste conservé jusqu’à la destruction du dossier de retrait ; il ne peut empêcher une destruction explicite anticipée par l’hôte.

Voir [VeroScriptSdl3 — Fondations — Fenêtres hôtes, curseurs et capture](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/fr-FR/index.md#host-window-retirement).

## API graphiques et requêtes asynchrones

Créer une fenêtre compatible avec FWindowOptions.GraphicsApi ou Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi). Une fenêtre empruntée doit déjà être compatible.

Voir [VeroScriptGraphics — Ressources et dessin 2D — API graphiques et requêtes asynchrones](../../../../../VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md#resource-retirement).

## Manettes, joysticks et vibration

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad` fournit les commandes standard `GamepadAxis/GamepadButton`, et `Joystick` les axes, boutons et chapeaux bruts indexés à partir de zéro. Un périphérique physique produit un seul flux. Un changement de configuration libère l’ancienne disposition puis annonce la nouvelle : réénumérer. Les identifiants non nuls désignent une connexion. Les commandes sont globales (`WindowId=0`) ; traiter les événements avant de lire `(Kind,DeviceId)`.

Les informations de connexion exposent `AxisCount/ButtonCount/HatCount/SupportsRumble` (C++ `bRumble`). Une manette standard possède 6 emplacements d’axe et 26 de bouton ; les commandes absentes restent neutres. Les instantanés natifs contiennent `Axes/ControllerButtons/Hats`. Les scripts utilisent `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`, `IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`, `GetHat(int64)->Hat` et les trois nombres, avec contrôle des indices. Les événements `ControllerAxis/ControllerButtonDown/ControllerButtonUp/ControllerHat` portent `Control/Value`. Les axes utilisent [-1,1], les gâchettes [0,1], sans zone morte automatique.

`SupportsControllers()/SupportsRumble()` annonce les capacités du service ; vérifier aussi chaque périphérique. `Rumble(kind,id,amplitudeBasse,amplitudeHaute,int64 millisecondes)` accepte des amplitudes finies 0..1 et 0..65535 ms. Un nouvel appel remplace la vibration ; durée nulle ou deux amplitudes nulles l’arrêtent. Périphériques non pris en charge, déconnexions, arguments invalides et appels depuis un autre thread produisent une erreur.

Reset global, passage en arrière-plan/sortie, déconnexion, débordement et destruction du service neutralisent les commandes et arrêtent sa vibration. Perte de focus, masquage et minimisation d’une fenêtre gérée neutralisent aussi les manettes globales ; un reset explicite limité à une fenêtre ne les efface pas. Les relâchements manquants sont synthétisés avant l’événement déclencheur ; les anciens instantanés restent inchangés. Le mode externe reçoit les informations et événements de l’hôte sans ouvrir de manette ni envoyer de vibration native. Un fournisseur hôte peut implémenter ce même contrat.

Vérifiez `SupportsControllers()` et les capacités de retour de chaque périphérique. La disponibilité physique dépend du système, du matériel et du pilote. L’hôte définit la politique d’entrée en arrière-plan. Gyroscopes, pavés tactiles des manettes, trackballs et effets avancés de retour de force sont hors de cette interface. Continuez le retrait des ressources et retours sur le thread propriétaire avant le déchargement.

<a id="window-and-events"></a>

Les hôtes natifs incluent `VeroScriptPlatformApi.h` : `IService` gère fenêtres/événements, `IWindow` leur état, et `FInputDevice` / `FInputState` les instantanés d’entrée. Les ID sont opaques, pas des adresses natives. Une demande de fermeture est un événement ; l’hôte décide quand fermer. `PollEvent` sépare réussite et disponibilité, avec `None` pour une lecture vide. `WaitEvent` accepte 0..50 millisecondes ; en pompage externe, l’hôte attend puis transmet via `SubmitEvent`. Les textes/IME sont des chaînes UTF-8 possédées ; les sélections IME comptent les caractères Unicode. `ScanCode` est un usage USB HID de page clavier, ou zéro sans correspondance. Les boutons souris valent 1=gauche, 2=milieu, 3=droite, 4=retour, 5=suivant ; le masque utilise le bit N-1. Les zones de saisie utilisent les coordonnées natives et un curseur horizontal relatif à X.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
