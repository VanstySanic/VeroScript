# Contrats du framework GUI

## Périmètre

Les API natives utilisent `VeroScript::Gui`, les scripts `VS::Gui`. Le framework fournit l’arbre de widgets, les identifiants par portée, le cycle de vie, les propriétés typées, les attributs, les événements, Context/Application, les thèmes, les animations de présentation et les instantanés i18n. Les contrôles de base, les collections, la navigation et les dialogues modaux sont inclus. Les compagnons d’éditeur sont documentés séparément.

## Compilation et construction

L’hôte enregistre `RegisterScriptBindings` sur son catalogue puis appelle `ConfigureGuiScriptEngine` avant de charger les sources. Dans un environnement publié, le catalogue faisant autorité doit également contenir les liaisons GUI. Le module copie les environnements modifiés sans les muter.

Pour conserver les variables globales entre des appels indépendants à `VeroScriptSession::CallFunction`, activez `session.SetHotReloadEnabled(true)` avant l’initialisation. Sans cet état persistant, chaque entrée reçoit de nouvelles variables globales. `Application.Run/Tick` dans une même entrée partage déjà son état.

Une CLI avec GUI configure automatiquement les dépendances du framework lorsque la compilation ordinaire ou `--check` utilise ses fonctions. Les programmes GUI existants ne demandent aucun nouveau commutateur. Les tâches `gui-*` et l’appel natif `ConfigureGuiScriptEngine` conservent l’import implicite du framework.

Le seul nom `Gui` pour une variable, un champ ou un paramètre ne provoque pas l’import du framework. Les références d’espace de noms, annotations GUI, macros et imports non inspectables conservent leurs règles de nécessité ou de traitement conservateur.

`@GuiWidget`, `@GuiProperty`, `@GuiMethod` et `@GuiEvent` partagent l’option facultative `Mark`. `GuiWidget` accepte aussi `Id`; `GuiProperty(Style)` autorise une valeur CSS. Les noms et options d’annotation sont insensibles à la casse; leurs valeurs et les noms ML/CSS restent sensibles. Le nom original et le Mark fonctionnent tous deux. Un champ ordinaire accepte une valeur; seul un `std::attribute<T>` accepte une liaison.

Cette comparaison ASCII insensible à la casse concerne les annotations GUI et leurs paramètres. Les noms d’annotation Core sont sensibles à la casse par défaut ; le processeur GUI reconnaît ses options sans distinction de casse ASCII. La réflexion `find_arguments(name)` compare toujours exactement le nom enregistré du paramètre, y compris pour une annotation GUI.

`Type::Create().Property(value)[{ children }]` produit un Builder convertible implicitement en widget, sans Build final. Les propriétés sont appliquées avant l’initialisation complète. Le constructeur prépare l’état interne; les hooks de synchronisation propagent les valeurs aux parties internes. GetX lit la valeur de base; l’animation possède une couche de présentation distincte.

L’initialisation réussit une fois par instance, des enfants vers les parents ; le montage suit les parents puis les enfants, et le démontage l’ordre inverse. Les changements d’état des hooks de montage sont synchronisés avant la mise en page ; ceux des hooks de mise en page le sont au prochain `Update`. Les notifications de mise en page échouées ou non exécutées restent en attente pour une mise à jour ultérieure ; celles déjà réussies ne se répètent pas sans nouveau changement. Les rappels voient toute la géométrie actuelle ; les entrées et le rendu exigent une mise à jour réussie.

## ML et CSS

Un fichier `.vswml` a une racine widget, ou un `<component type="Demo::Card" base="Base" mark="card">` contenant un `<template>`. Un composant exporte un véritable type VS et peut hériter d’un type VS ou ML. Une page ordinaire possède son état privé. Les directives import/script/style sont traitées par Gui; leur contenu ne devient pas automatiquement du RML/RCSS.

Les attributs ordinaires sont fixes. `bind:gap="{ Count }"` installe un Getter typé; `on:pointer-down="Increase"` ou un bloc entre accolades installe un événement. Les préfixes bind/on sont intégrés et n’exigent pas de déclaration xmlns. Un script intégré utilise l’instance de page/composant; un script externe utilise le contexte déclaré. Les membres externes accessibles peuvent être non qualifiés ou préfixés par context; les variables locales conservent leur portée. Les expressions sont du VS, sans observateur de champs ordinaire implicite.

Les événements personnalisés suivent leur signature exacte, leur retour et leur nature delegate/multicast_delegate. Les événements natifs utilisent le routeur capture/cible/bulle. StopPropagation, StopImmediatePropagation et PreventDefault restent distincts. Les rappels ML sont faibles; les liaisons fortes explicites du Core conservent leur sémantique.

Chaque déclaration possède une portée d’id privée. Les enfants ajoutés à l’utilisation ne deviennent pas des parties privées du composant. `elements.Require("part")` retrouve une partie dans la portée de la déclaration. Les ressources sont relatives au module ML/CSS/VS propriétaire, jamais au répertoire courant. `elements.Find("part")` rend `nullptr` si aucun widget n’est trouvé ; `elements.Require("part")` exige le succès de la recherche par une assertion.

VSWCSS prend en charge types/Mark, *, class/id, combinaisons, descendants/enfants, états et variables bornées `var(...)`. Les syntaxes non prises en charge produisent un diagnostic. L’ordre est défaut/type, thème, feuille locale, puis configuration locale/liaison. Un Setter remplace la liaison; ClearX ne restaure pas un ancien Getter. Style n’accorde pas automatiquement une animation aux objets personnalisés.

## Exécution, outils et déploiement

Context pilote une interface intégrée; Application assemble Platform, Graphics et Gui avec un seul responsable de la pompe d’événements. `MakeRegisteredServiceFactories` utilise les fournisseurs natifs correspondants et conserve leurs propriétaires. Les ressources empruntées ne sont pas fermées par l’adaptateur. Le fournisseur GUI renvoie `IBackendService`.

Application convertit les exceptions de nettoyage des fournisseurs en erreurs et conserve les ressources restantes ainsi que le bail de la pompe d’événements. Close peut être réessayé; après la libération de la dernière référence, CollectRetired réessaie sur le thread propriétaire.

`SetLocalization` associe le contexte i18n du Core. `GetLocalizationSnapshot` renvoie l’instantané fixé pour la frame; les changements apparaissent à la frame suivante. Les descriptions de messages et les arguments restent typés; Gui ne réimplémente pas ICU.

`BakeGuiApplication` produit une image applicative Core interne avec ses métadonnées GUI. Les consommateurs natifs de cette image utilisent `ReadPackage`, `RegisterGuiArtifactBindings` et `LoadPackage` de Session. La commande `gui-compile` utilise en plus Application pour l’envelopper dans un conteneur ZIP `.vsapp` standard, chargé via `FApplication`/`LoadApplication`. Compiler ou charger ce conteneur exige Application ; la vérification et l’aperçu des sources restent disponibles sans ce service.

`CompileGuiSource`/`CompileGuiFile` utilisent le compilateur complet sans exécuter les initialiseurs ni créer de fenêtres/périphériques. Pour charger directement une image native, appelez `RegisterGuiArtifactBindings` avant `LoadPackage` de Session. Le manifeste conserve la résolution relative à l’appelant après suppression des sources/informations de débogage ; scripts et styles compilés n’exigent plus leurs sources, mais les ressources externes nécessitent toujours leurs fournisseurs.

Le Runtime compatible avec GUI fournit `gui-check INPUT`, `gui-compile INPUT --output app.vsapp` et `gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]`. La vérification et la compilation n’exécutent pas les initialisations et ne créent pas de services UI. `--frames N` borne seulement la boucle de prévisualisation ML générée ; les entrées VS contrôlent leur durée de vie et rejettent cette option.

`--platform-backend`, `--graphics-backend` et `--gui-backend` précèdent la commande ou le script et concernent aussi les applications VS ordinaires. Un choix explicite du script prime sur la configuration de l’appel ou de l’environnement. Une valeur vide hérite du choix ; `auto` demande à la fabrique de choisir un fournisseur disponible ; `none` désactive le service. Désactiver la plateforme, le GUI ou Graphics lorsque le rendu est actif produit une erreur. Sans choix par défaut pour ce service, un hôte natif conserve les replis habituels sdl3/sdl3/rmlui. `--backend` reste réservé à la VM et au JIT.

`CreateCommandLineService` expose les mêmes tâches à l’hôte. Conserver l’Owner tant que les descripteurs empruntés sont utilisés. Les hooks SetEngineOptions/SetRuntimeServices/RegisterBindings/ConfigureEngine/UseArtifact partagent la configuration de l’invocation. VeroScriptGuiToolsModule transfère les appels aux services de l’hôte sans incorporer un second Core; une inscription déjà assurée par l’hôte est réutilisée.

L’API native `FMarkupProgram` reste disponible avec des profils natifs et un `IMarkupScriptContext` facultatif. Les types VS exportés, les Builders générés et les propriétés/événements personnalisés utilisent le compilateur complet. Voir [l’exemple du framework](../../../../Examples/Framework/main.vs).

Un hôte peut déclarer les contextes métier requis et les codecs de valeurs dans `FGuiCompilationConfiguration`. Les analyseurs de littéraux et de styles sont des fonctions VS visibles ordinaires recevant `string` et renvoyant le type de valeur déclaré ; le profil immuable participe à l’identité de compilation.

Les validations Windows/Linux ne certifient pas macOS, Android, iOS ou les navigateurs. Les capacités, les plateformes et les modes réellement vérifiés doivent être annoncés séparément.

Exemple partagé : [main.vs](../../../../Examples/Framework/main.vs), [page.vswml](../../../../Examples/Framework/page.vswml), [page.vswcss](../../../../Examples/Framework/page.vswcss). Les tâches check/preview acceptent aussi une application GUI .vsapp déjà compilée.

## Texte, polices, images et dessin

FreeType 2.13.3 et HarfBuzz 10.4.0 assurent le texte. Lato Latin régulier, inchangé, est intégré sous Vero Sans. Chargez les polices et replis des autres écritures avec Context.LoadFont. La variante à six arguments précise graisse, italique, repli et index de collection.

Text est une chaîne UTF-8, liée éventuellement par attribute, jamais interprétée comme RML. Le texte brut ML et le contenu mixte ne sont pas acceptés. Polices, styles du texte, direction et langue héritent du parent. Direction définit la direction de base du paragraphe : auto/ltr/rtl. Auto suit le premier caractère fort de chaque paragraphe, avec LTR par défaut ; Language agit uniquement sur le façonnage. RmlUi résout les séquences bidirectionnelles Unicode, puis façonne et réordonne chaque ligne affichée. Les valeurs UTF-8 restent dans l’ordre logique ; left/right gardent leur sens physique.

<a id="image-resources"></a>

`Image` charge une image de contenu. `BackgroundImage` accepte `url(...)` et les dégradés linéaires, radiaux, coniques et répétés. Les images relatives sont résolues depuis leur source VS/ML/CSS, y compris pour les getters d’attribut différés. L’hôte natif configure `FResourceReader` et, si nécessaire, `FImageDecoder` ; les pixels décodés utilisent du RGBA8 sRGB à alpha non prémultiplié. Les formats dépendent du [service Image](../../../../../../VeroScriptImage/Resources/Docs/User/fr-FR/index.md) choisi ; voir [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/fr-FR/index.md) pour `image/stb`. SVG et animations nécessitent un décodeur ou lecteur hôte adapté.

La disposition par défaut place l’image, le texte puis les widgets enfants ; cet ordre est conservé après modification des propriétés ou reconstruction de l’arbre.

Les getters Image, BackgroundImage et MaskImage renvoient des identités/URL résolues pour les valeurs directes comme liées ; un transfert à un autre module conserve ainsi leur origine.

<a id="compositor-properties"></a>

`CornerRadius`, `ClipContent`, `Transform`, `Filter`, `BackdropFilter`, `MaskImage` et `BoxShadow` partagent le même chemin de propriétés, liaisons et styles. Le backend choisi détermine les opérations de composition et limites de ressources ; voir [composition et ressources RmlUi](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/fr-FR/index.md#composition-resources) pour ce fournisseur.

OnPaint(PaintContext canvas) dessine dans le contenu après le fond et avant les enfants. VS expose FillRect, DrawLine, DrawImage, PushClip/PopClip, GetWidth/GetHeight ; le `FPaintContext` natif ajoute maillages et transformations. Les clips restent dans les coordonnées du contenu et les transformations touchent les géométries suivantes. Le canevas expire à la sortie ; aucune modification GUI ni conservation du canevas.

Utilisez FGuiAssetOptions avec BakeGuiApplication ou répétez --asset dans gui-compile pour intégrer images et polices. Les chemins de liste suivent le répertoire de travail, les références restent relatives au module. Le SDK installe les licences des dépendances. Contrat GUI 0.1.1 .

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage et VeroScriptFont définissent les interfaces communes. VeroScriptStb fournit `image/stb` ; VeroScriptFontBackend fournit `font/font-backend` et le service distinct `text-layout/font-backend`. Un hôte GUI natif injecte images et polices par `FBackendOptions.Images` / `Fonts` / `TextLayouts`. Choisissez un hôte et des SDK compatibles avec les contrats Font et de disposition sélectionnés ; installez les paquets de services et de fournisseurs nécessaires avec leurs notices.

`ApplicationOptions.FontBackend` et `TextLayoutBackend` sélectionnent les services indépendamment. Les valeurs vides héritent des choix de l’hôte ou de la CLI (`--font-backend`, `--text-layout-backend`) ; `auto` délègue le choix du fournisseur et `none` désactive ce service facultatif. Sans choix par défaut pour ce service, l’hôte natif utilise `font-backend` comme repli habituel. L’hôte fournit `FApplicationFactories.Font`/`TextLayout` ou les instances `Fonts`/`TextLayouts`. Utilisez des fournisseurs compatibles avec GUI `0.1.1`, Font `0.1.2` et text-layout `0.1.0`. Conservez les services tant que leurs contextes existent et fermez tous les contextes avant de remplacer la configuration partagée. Passez le service choisi en premier argument de `FTextLayout::Create`.

## Contrôles de base

ProgressBar, Slider et ScrollBar ramènent GetActualValue aux dernières bornes configurées avant de calculer GetNormalizedValue, même si le changement de plage n’est pas encore synchronisé. Les plages invalides restent refusées lors de la synchronisation.

`VS::Gui` fournit Text, Image, Border, Spacer, Separator, Stack, Overlay, Button, CheckBox, ToggleSwitch, ProgressBar, Slider, ScrollBar, ScrollView, TextBox et TextArea. Enfants, Builder, Attribute, événements et ML/CSS sont communs. Border utilise BorderWidth/BorderColor ; les éditeurs utilisent Value.

Clicked, CheckedChanged, ValueChanged, Scrolled et TextChanged/Submitted signalent les actions utilisateur. Les propriétés non liées acceptent la modification ; les Getter liés sont conservés et signalent une proposition. Les setters et mises à jour ordinaires n’émettent pas ces événements. IsXBound() expose la liaison. Les événements hérités gardent leur sender CheckBox/TextBox. Minimum/Maximum/Step normalisent les valeurs ; Step=0 est continu et GetActualValue retourne la valeur affichée.

Souris, tactile, Space/Enter, Tab/Shift+Tab, flèches, Home/End et PageUp/PageDown sont pris en charge. PreventDefault bloque l’action par défaut ; :checked/:read-only complètent les styles. ScrollView accepte molette/tactile/clavier. ScrollX/Y sont des demandes, GetActualScrollX/Y et GetScrollWidth/Height lisent la dernière géométrie.

Sélection en points de code logiques, presse-papiers, annuler/rétablir, lecture seule, texte indicatif et IME sont disponibles. La fin de Select est exclusive ; une plage logique peut couvrir plusieurs intervalles visuels. Flèches, pointage et caret IME utilisent les mêmes lignes affichées ; Backspace/Delete conservent l’ordre logique. L’extrémité active et l’affinité du caret sont préservées lors des restaurations, changements de focus et reconstructions. Limites : texte UTF-8 de1 Mio, chaque historique de128 instantanés/8 Mio. Application raccorde la plateforme ; les hôtes intégrés fournissent FTextInputServices. Un échec du presse-papiers restaure l’édition. IME physique, accessibilité, navigation complète par graphèmes et autres plateformes demandent une validation ou implémentation distincte. Reconstruire ensemble les utilisateurs du contrat GUI0.1.1 . [ML](../../../../Examples/Controls/main.vs), [page](../../../../Examples/Controls/page.vswml), [styles](../../../../Examples/Controls/page.vswcss), [code](../../../../Examples/Controls/code.vs).

Checked, Value, bornes/pas, ReadOnly et défilement se règlent en code/ML ; les styles règlent l’apparence et l’orientation. TextBox retire les sauts de ligne/tabulations des valeurs non liées ; GetActualValue lit le texte effectif.

Les bornes restent accessibles si Step ne divise pas l’intervalle. Le glissement tactile de Slider／ScrollBar prévaut sur le défilement des ancêtres. Select utilise les indices de points de code du texte validé ; une sélection sans focus est conservée jusqu’au focus, et une sélection valide annule la préédition. Remplacer le texte ou changer le type d’éditeur efface annuler／rétablir, même pendant une reconstruction de l’arbre. La bibliothèque complète augmente le coût de compilation des sources ; utilisez gui-compile pour éviter cette compilation au démarrage.

ScrollWidth/Height excluent Padding : soustraire la taille de la zone de contenu donne la plage de défilement réelle du backend. Pour une interface scriptée intégrée, entrez une seule fois dans la session par image et appelez Context.Update ou Application.Tick/Run depuis cette fonction script. FScriptServices.Invoke sert de repli pour chaque rappel hôte et ajoute un coût d’entrée de session. Un Context purement natif n’utilise pas ce pont script.

Si le défilement demandé dépasse la plage du contenu actuel, les entrées de molette, de clavier et tactiles partent de la position ramenée dans cette plage ; les événements successifs avant la prochaine mise en page continuent de se cumuler. Retirer la souris ou le périphérique tactile qui effectue une sélection de texte termine ce glissement sans annuler l’appui actif d’un autre périphérique. Si le conteneur interne ne peut plus défiler, la molette est transmise à un ancêtre pouvant défiler ; un déplacement nul ne signale aucun changement. Lors du retrait d’un périphérique, sa capture est libérée même si le service de saisie de texte hôte renvoie une erreur ou lève une exception, et cet échec est signalé.

## Disposition, sélection, saisie et éléments flottants

Grid utilise des colonnes de même largeur, Columns (int64, 1–4096), et Gap pour les espacements. La hauteur suit le plus grand enfant de chaque ligne ; Collapsed ne réserve pas de cellule. Grid détermine la largeur des enfants, sans pistes ni fusion de cellules CSS Grid. Wrap revient à la ligne horizontalement par défaut ; le flux vertical demande Layout=Vertical et une Height limitée. Splitter accepte deux enfants ordinaires au maximum, hors éléments flottants. Value est la fraction du premier panneau après retrait de DividerSize. Minimum/Maximum restent dans [0,1], Step vaut 0.01 et DividerSize 6 par défaut. Vertical active la division haut/bas. Glissement et clavier émettent ValueChanged(Splitter,double) ; ReadOnly bloque les modifications.

RadioGroup.SelectedKey choisit une RadioButton.Key non vide et unique dans le groupe le plus proche. Les groupes imbriqués sont indépendants ; une clé absente laisse le groupe sans sélection. RadioButton expose IsChecked/Select/Focus et Selected(RadioButton), le groupe SelectionChanged(RadioGroup,string). Les flèches parcourent les choix éligibles en boucle ; pointeur, Space et Enter sélectionnent. Les attributs liés conservent leur Getter et signalent le candidat. Une configuration programmée ne produit pas d'événement utilisateur.

NumberBox expose double Value/Minimum/Maximum/Step (0/0/100/1 par défaut), ReadOnly, Placeholder, Focus, Select, GetActualValue et ValueChanged/Submitted(NumberBox,double). Les brouillons incomplets restent éditables jusqu'à Enter ou un changement de focus. La validation accepte les nombres décimaux/exponentiels finis et applique bornes et Step ; un brouillon invalide restaure la dernière valeur. Up/Down utilise Step (ou 1 si nul), Escape annule. Le point décimal est indépendant de la locale ; utilisez les services du noyau pour un affichage localisé. PasswordBox hérite de TextBox et conserve sélection, IME et signatures des événements, avec affichage masqué. Les getters renvoient le texte réel.

Popup est non modal et ancré au parent logique. Son élément est placé au-dessus du contenu normal, hors flux et hors découpage des ancêtres, tout en conservant propriété et chemin des événements. OffsetX/Y et le viewport limitent la position, éventuellement au-dessus de l'ancre. Open vaut false par défaut. Escape ou un appui extérieur (DismissOnOutside=true) signale OpenChanged(Popup,bool) et consomme l'appui. Une liaison reste intacte ; le focus est explicite et la fermeture restaure le précédent focus encore éligible. La composition IME traite Escape en premier. ToolTip et ses descendants sont non interactifs. AutoOpen=true utilise le survol du parent et Delay (0.5 seconde), puis masque au départ ou à la réinitialisation de la souris ; false utilise Open. IsActuallyOpen lit l'état récent. Modal=true active le confinement ; Dialog fournit ce réglage par défaut.

Columns, DividerSize, Vertical, OffsetX/Y et Delay sont des propriétés CSS ; les autres valeurs et politiques de liaison viennent du code/ML. Les contrôles réutilisent les générateurs et événements existants. Reconstruisez fournisseurs et consommateurs pour le contrat GUI **0.1.1**, avec bExtendedControls pour ces contrôles . [Entrée](../../../../Examples/AdditionalControls/main.vs), [ML](../../../../Examples/AdditionalControls/page.vswml), [CSS](../../../../Examples/AdditionalControls/page.vswcss).

Le contenu Image/Text occupe aussi des cellules de même largeur avant les widgets enfants. La hauteur initiale de Splitter est de 120 unités GUI.

Si le conteneur est plus petit que DividerSize, le séparateur est limité à l’espace disponible. Les rappels de RadioGroup voient la sélection entière mise à jour ; s’ils choisissent une autre option, aucun événement Selected périmé de la demande initiale n’est émis. NumberBox valide aussi le brouillon lors de la perte du focus de la fenêtre ou de la réinitialisation des entrées. Si la validation et la libération des entrées par l’hôte échouent toutes les deux, l’erreur de validation initiale est conservée ; un Update ultérieur réessaie la libération après le rétablissement de l’hôte. Un nombre accepte au plus un signe initial.

Lors du remplacement de FTextInputServices, un échec de l’arrêt conserve l’ancien service et le focus logique pour permettre une nouvelle tentative. Le changement de focus revérifie le contexte et la cible après les rappels de validation numérique, qui peuvent fermer le contexte ou retirer la cible.

## Visibilité du focus et erreurs de rappel

Après la mise en page, Tab/Shift+Tab et Focus() utilisent la dernière géométrie pour rendre la cible visible dans les ScrollView ancêtres avec le déplacement minimal, de l’intérieur vers l’extérieur. Les contrôles surdimensionnés ne basculent pas entre les bords. Un contenu flottant ne fait pas défiler ses ancêtres logiques. Scrolled est émis seulement si un décalage doit changer ; les propriétés liées conservent leur Getter et l’application accepte les décalages candidats via cet événement. La géométrie change au prochain Update. Le focus donné par un pointeur ne déplace pas le conteneur, afin de garder le contrôle sous le pointeur.

Activate/Focus directs et la distribution des entrées partagent la gestion des exceptions de rappel. Toutes deviennent des erreurs gui.callback_exception sans sortir de l’interface à retour d’erreur. Les valeurs déjà validées et effets applicatifs ne sont pas annulés ; traiter l’erreur avant de réessayer.

Le framework conserve les dernières coordonnées de fenêtre de chaque souris et refait le test de pointage après la mise en page. Les styles de survol et ToolTip automatiques suivent dans Update sans événements de pointeur synthétiques. Annulation, retrait du périphérique, réinitialisation, perte de focus et fermeture effacent les positions concernées pour éviter un survol périmé sous une souris immobile.

## Curseurs et capture de souris

Cursor est une propriété héritée VS::Gui::Cursor, utilisable dans les styles. Le générateur et SetCursor acceptent une valeur ou std::attribute<VS::Gui::Cursor> ; ML : Cursor="pointer", CSS : cursor: pointer. ClearCursor restaure la source héritée ou le style ; Auto rétablit le comportement du contrôle.

Auto choisit la main pour les boutons, cases, commutateurs, choix radio, curseurs de valeur et barres de défilement ; le curseur texte pour les saisies ; une flèche de redimensionnement uniquement sur le séparateur de Splitter. Les zones ordinaires et contrôles désactivés utilisent la flèche, sauf style explicite, par exemple :disabled { cursor: not-allowed; }. Les enfants suivent le contrôle interactif ; un portail ne reprend pas le curseur automatique de son bouton d’ancrage.

Valeurs : Auto, Default, Text, Wait, Crosshair, Progress, ResizeNWSE, ResizeNESW, ResizeEW, ResizeNS, Move, NotAllowed, Pointer, None. Marques CSS : auto, default, text, wait, crosshair, progress, nwse-resize, nesw-resize, ew-resize, ns-resize, move, not-allowed, pointer, none. None masque le curseur ; le graphique Showcase utilise crosshair.

Application raccorde les capacités Platform. Les hôtes intégrés fournissent éventuellement `FContext::SetPointerServices(FPointerServices, Error)` ; les rappels restent sur le thread propriétaire et ne modifient pas le GUI. Sans service, le routage interne reste disponible. Les mises à jour identiques sont évitées ; le curseur n’est activé qu’après une entrée souris. Le tactile ne demande pas de capture souris.

Le glissement conserve curseur et capture hors fenêtre. Relâchement, annulation, retrait du périphérique, perte de focus, réinitialisation et fermeture nettoient les états. La mise en page actualise aussi une souris immobile. MouseEntered/MouseLeft sont des notifications de fenêtre, exposées à la racine comme mouse-entered/mouse-left ; la sortie efface survol et ToolTip sans annuler un glissement ni le focus clavier. Les hôtes externes les transmettent.

Erreurs et exceptions sont retournées. Un échec d’acquisition annule la capture logique ; Update peut réessayer un curseur. Remplacement du service et Close conservent le nettoyage inachevé ; le détachement restaure une flèche visible et libère la capture détenue. Contrat GUI **0.1.1** : reconstruire fournisseurs et consommateurs ensemble.

La capture est synchronisée avant l’évaluation du curseur : une erreur de curseur ne bloque donc pas sa libération native. Un hôte qui fournit seulement SetMouseCapture n’évalue pas Cursor pour ce service. Les opérations publiques maintiennent le contexte en vie jusqu’au retour des rappels, même si l’hôte y libère sa dernière référence.

Si la fenêtre native est déjà fermée, Application considère la désactivation de la saisie, la libération de capture et la restauration du curseur visible par défaut comme terminées. Activer la saisie ou appliquer un autre curseur reste une erreur.

Application.Run utilise WaitMilliseconds comme intervalle maximal entre les frames au repos (16 ms par défaut), en déduisant le temps de mise à jour et de rendu précédent avant l’attente. Les événements peuvent réveiller la boucle plus tôt. Tick n’ajoute aucune attente ; l’hôte le planifie. Les propriétés natives non configurées évitent le sondage côté script. Les attributs configurés conservent leurs règles de synchronisation ; une valeur directe ou Clear supprime la liaison correspondante.

`Run(beforeUpdate)` et `Tick(beforeUpdate)` exécutent le rappel avant la synchronisation GUI, normalement après les entrées. Si la racine n’a pas encore de disposition, la mise à jour initiale avant les entrées l’exécute également : le premier pas peut donc l’appeler deux fois. La surcharge Run conserve le budget d’attente pour les animations et le modèle ; Tick reste une étape sans attente. Les exceptions sont propagées et le nettoyage actualise toujours la référence de la vue racine.

Les getters liés, y compris ceux dont le résultat optional est vide, sont évalués à chaque cycle de synchronisation requis. Les setters directs et Clear remplacent ou retirent la liaison locale. Les notifications de changement ne sont validées qu’après la réussite du hook ; un échec peut être retenté lors d’une mise à jour ultérieure. Les valeurs personnalisées conservent les règles normales de copie du langage.

GUI reprend la configuration de l’hôte. Utilisez `--backend=vm` ou `--jit-backend="/path/to/Jit" --backend=jit`, avant `gui-preview`. Les hôtes C++ définissent FEngineConfig.ExecutionBackend et JitBackendFactory (LoadJitBackend). Le mode mixed sans fabrique JIT, le débogage, l’initialisation persistante et le nettoyage borné utilisent la VM. Les fonctions ordinaires d’une session persistante peuvent utiliser le backend JIT sélectionné. Vérifiez le résultat de `VeroScriptSession::ApplyConfig` et transmettez la raison du refus avant de charger les sources.

Les archives officielles sont `vs-gui` et `vs-rmlui`. Gui dépend de Platform/Graphics/Image/Font ; RmlUi dépend de Gui. Les backends restent sélectionnables. Utiliser `find_package(VeroScriptGui)` ; ajouter `COMPONENTS Bindings` et les SDK Core/C++ Runtime correspondants pour les scripts. L’entrée compatible `VeroScriptGuiFramework` est conservée.

## Contrats des scripts et de l’hôte

Les types fermés des propriétés et événements conservent les paramètres de modèle de type et de valeur du cœur, notamment bool, les entiers de taille fixe et les énumérations. Le code généré conserve exactement le type et la plage des entiers, décode les types imbriqués des conteneurs intégrés, références et signatures de fonctions, et préserve l’ordre des dimensions des tableaux natifs. GuiWidget exige une classe fermée, non statique, déclarée dans un espace de noms ; les classes locales et imbriquées sont refusées. GuiMethod et les hooks de changement de propriété doivent être des méthodes d’instance sans paramètres de modèle, y compris de valeur.

CompileGuiSource/File conservent FCompileOptions.ConstantEvaluationLimits et utilisent l’évaluation constexpr du cœur. Les attributs dynamiques restent évalués lors des cycles normaux de synchronisation GUI.

Context.Post conserve les rappels et leurs captures jusqu’à leur exécution ou leur abandon. Les objets capturés peuvent rester en vie jusqu’au nettoyage différé. Context.SetFocus transmet les notifications des contrôles de façon synchrone.

TextArea conserve CR, LF, CRLF, NEL (U+0085), LS (U+2028) et PS (U+2029) dans la valeur logique. Chaque séquence produit un seul saut de ligne ; Backspace/Delete suppriment CRLF ensemble. Les sélections et le presse-papiers comptent et préservent la séquence originale. Les contrôles sur une seule ligne filtrent ces séparateurs. Les tabulations gardent leur filtrage existant ; les espaces du texte indicatif sont normalisés uniquement pour l’affichage.

L’affinité leading/trailing distingue les positions visuelles aux frontières de direction. Le déplacement horizontal franchit les lignes repliées selon la direction de base du paragraphe et rejoint le bord visuel correspondant, même pour une ligne composée uniquement d’un segment de direction opposée. Cette politique ne garantit pas le même comportement que tous les éditeurs système. La navigation complète par graphèmes et les carets précis dans les ligatures restent distincts.

Les callbacks de saisie de texte de l’hôte peuvent consulter l’état GUI. Les mutations du contexte ou des widgets montés, le remplacement des services et Close sont refusés jusqu’au retour du callback.

Après une modification de l’ABI publique du cœur ou du format interne du bytecode, reconstruisez GuiScript et les fournisseurs natifs avec les SDK Core et C++ Runtime correspondants. Un numéro de version produit identique ne suffit pas à garantir la compatibilité binaire ; régénérez le bytecode avec le format correspondant.

## Liaisons notifiées et mises à jour groupées

`Observable<T>.Get()/Set()` conserve une donnée observable. `Set` compare avec `==` et ne notifie pas une valeur identique ; appelez `Notify()` après une modification interne d’un objet mutable. `ChangeSignal.Notify()` représente aussi les entrées d’un autre service. Utilisez `AsBinding()` pour une source, ou `Binding<T>(getter, inputs)` avec un tableau explicite de signaux. Les champs ordinaires du script ne sont pas suivis automatiquement. Les appels AsBinding réutilisent un Binding vivant via un cache faible, sans cycle de possession.

Les getters doivent être en lecture seule et déclarer toutes leurs entrées variables. Le résultat, y compris une valeur optional vide, est mémorisé selon les révisions ; les widgets partageant le même Binding partagent le calcul. `Binding<T>::FromAttribute(attribute, inputs)` adopte explicitement ce contrat. Les dépendances inconnues, le temps et l’expiration des propriétaires weak demandent des signaux adaptés. Les liaisons `std::attribute<T>` directes gardent leur interrogation périodique et leur repli.

Les notifications répétées sont regroupées dans une file native ; chaque widget concerné est réveillé une seule fois par publication. `BindingUpdates.Batch(work)` accepte les appels imbriqués et diffère le travail GUI à la prochaine mise à jour sûre. Les données changent immédiatement et ne sont pas annulées en cas d’exception. Context.Update, Application.Tick et Application.Run sont interdits dans un lot. La construction conserve l’ordre habituel d’initialisation et de cycle de vie parent/enfant.

La file est publiée après la capture de tous les instantanés de services. La synchronisation existante lit les attributs concernés ; un résultat identique ne provoque ni hook de changement ni invalidation de propriété native superflue. Les données de trame doivent signaler après leur capture ; les entrées non observables restent des attributs dynamiques ordinaires. Les mises à jour programmatiques ne déclenchent pas d’événements d’entrée. SetX/ClearX déconnectent l’ancien abonnement et IsXBound conserve sa sémantique. Dispose et la libération finale déconnectent les abonnements ; le démontage conserve la configuration. Un getter échoué garde la dernière valeur validée et utilise la reprise du cycle de vie existant. FromAttribute capture la configuration transmise ; remplacer ensuite celle de l’attribut original ne remplace pas ce Binding. FromAttribute conserve le parcours du repli pour les changements indépendants des attributs par défaut et styles natifs ; le résultat du getter transmis reste mémorisé selon les révisions de notification.

VS, les Builder générés et les blocs VS de .vswml utilisent les mêmes setters. `bind:` conserve sa sémantique périodique et .vswcss reste une couche de style. Notifications et traitement s’exécutent sur le thread propriétaire de la GUI.

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## Collections, navigation et dialogues

ListView/ListItem et TreeView/TreeItem offrent une sélection unique par SelectedKey (chaîne), FindItem, GetActualSelectedKey, Select, Activate et les événements SelectionChanged/ItemActivated. Key utilise à défaut Id puis l'indice ; préférez des clés explicites stables. Select demande une sélection existante ; Activate exige aussi un élément interactif. Flèches et Home/End naviguent, Entrée/Espace active. L'arbre ajoute Expanded, IsActuallyExpanded et ExpandedChanged ; les enfants restent montés et conservent leur Visibility.

TableView utilise TableColumn/Row : Key, ColumnWidth positif en pixels, Sortable et une cellule widget par colonne. SetColumns, SetRows, GetCellText/SetCellText et SortBy réutilisent ces widgets. Le tri UTF-8 ordinal est stable, conserve les objets et la sélection, sans déduire de tri numérique/localisé. AutoSort=true traite les clics d'en-tête ; événements ColumnClicked, Sorted et sélection. Les sources de données proposent une virtualisation à hauteur fixe, sans édition de tableur ; préférez SetChildren/SetRows en lot pour les enfants écrits à la main.

ComboBox reçoit des ListItem : SelectedKey, Open, Placeholder, DropDownHeight, Select, SetItems, GetList, RequestOpen et événements associés. Il gère son Text depuis l'élément sélectionné. TabView reçoit des TabItem avec Key/Title ; sans liaison, la première page disponible est sélectionnée. Select/SelectionChanged changent la page sans démontage et sans remplacer Visibility ; flèches gauche/droite et Home/End naviguent entre en-têtes.

Menu/MenuItem fournissent des sous-menus, Command, Checkable/Checked, Invoked, Invoke/OpenMenu/CloseMenu. Les portails se placent dessous/à droite ; un appui extérieur ferme toute la chaîne active et est consommé. Les menus natifs du système et accélérateurs restent du ressort de l'hôte.

Popup reste non modal par défaut : Placement=below/right/center, MatchAnchorWidth et DismissOnEscape. GetDismissReason renvoie escape/outside ou une chaîne vide. Dialog utilise Modal=true, placement centré et DismissOnOutside=false : Show, Close(result), Closed(Dialog,string), confinement du focus et restauration du focus valide. Il réutilise la boucle existante. Les liaisons Open conservent leur source après les demandes candidates.

Builders : OnXxx ; instances : AddXxx. SetChildren des composites vise leur contenu ; GetChildren/parents gardent l'arbre réel, GetContentChildren lit les enfants de contenu. Différez les modifications structurelles hors des getters/mises à jour avec Context.Post. Grid conserve ses colonnes égales ; TableColumn donne des largeurs indépendantes. [Exemple](../../../../Examples/CompositeControls/main.vs), [ML](../../../../Examples/CompositeControls/page.vswml), [CSS](../../../../Examples/CompositeControls/page.vswcss).

`MenuItem.IsActuallyChecked()` lit la valeur candidate ; les demandes consécutives la basculent avant que la synchronisation restaure le modèle Checked lié. Closed conserve le résultat précédant les rappels OpenChanged, même s’ils rouvrent le dialogue.

Les listes et tables de données proposent une virtualisation automatique à hauteur fixe : `ListView.SetVirtualItems(labels,rowHeight=28,overscan=2)` et `TableView.SetVirtualRows(rows,rowHeight=28,overscan=2)` ne créent et ne recyclent que les lignes proches de la zone visible. `SetVirtualItemText(index,text)` et `SetCellText(key,column,text)` modifient aussi les données hors écran. Pour des lignes personnalisées, `SetVirtualSource(keys,rowHeight,factory,overscan=2)` appelle factory avec `(string key, CollectionItem recycled)`. Elle renvoie cette ligne ou un nouveau widget non attaché ; une table exige TableRow. Réinitialiser l’état lié à l’ancienne clé. `RefreshVirtualItems()` réassocie les lignes visibles ; `SetVirtualOrder(keys)` fournit un ordre contenant toutes les clés. Les tables de données intégrées conservent le tri textuel stable ; une source personnalisée traite ColumnClicked et fournit son ordre.

`GetItemCount()` compte les lignes logiques ; `GetItems()`, `GetRealizedItemCount()` et `FindItem()` décrivent uniquement les lignes réalisées. La sélection conserve sa clé, et `ScrollToItem(key)` révèle une ligne hors écran. Une ligne ayant le focus, une pression ou une capture de pointeur reste réalisée. La réalisation utilise la phase différée sûre de Context.Update ; un changement découvert pendant la mise en page est traité à la mise à jour suivante. Les lignes ont une hauteur border-box fixe et découpent leur contenu ; le padding doit tenir dans cette hauteur. Clés non vides et uniques, overscan de 0 à 256, hauteur totale limitée à 16 777 216 pixels logiques. Ne pas remplacer ni réordonner la source dans factory ou un getter. Les erreurs de factory sont propagées et peuvent être retentées à une mise à jour ultérieure ; la vue possède les lignes renvoyées. Les enfants écrits à la main, SetItems et SetRows gardent leur cycle de vie. Les lignes de hauteur variable et la virtualisation des arbres sont exclues.

Un appel ScrollToItem précédant la première mise en page est corrigé lorsque la zone visible réelle est connue ; un défilement explicite ultérieur reste prioritaire.

## Backend par défaut de Context

Avec le pont standard des fabriques d’application, `VS::Gui::Context("", width, height)` donne priorité au backend explicite du constructeur, sinon au choix de l’invocation, puis au défaut de la fabrique application/environnement. Sans choix à ces niveaux, GUI utilise `rmlui`. Une chaîne vide renvoyée par le rappel hôte de sélection laisse ce niveau non spécifié ; elle ne désactive pas le service. `auto` ou `none` explicite n’est pas remplacé par `rmlui`. Une valeur de configuration d’environnement explicitement vide est normalisée en `none`, selon les règles communes. Une fabrique Context personnalisée de l’hôte gère elle-même les noms de backend.

<a id="context-callback-reentrancy"></a>

## Restrictions des callbacks du Context

Effectuez les changements de configuration du Context hors des callbacks invoqués pendant le remplacement des thèmes, services d’image, de pointeur ou de saisie. La configuration récursive du même Context est refusée. Utilisez `Post` pour planifier le travail ultérieur sur le thread propriétaire.

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/Framework.md) · [fr-FR](Framework.md) · [ja-JP](../../ja-JP/Reference/Framework.md) · [ko-KR](../../ko-KR/Reference/Framework.md) · [zh-CN](../../zh-CN/Reference/Framework.md) · [zh-TW](../../zh-TW/Reference/Framework.md)
