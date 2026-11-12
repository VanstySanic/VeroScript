# Maintenance GUI

`#include "module.vs"` suit la même détection que `export import "module.vs";` ; cet alias seul ne charge pas le framework. Les inclusions binaires conservent l’inspection des macros publiques. Dans les builds racine avec GUI, les régressions des exemples Core et du client de paquets utilisent l’hôte de test `VeroScriptCliCoreTests`. Les scénarios GUI utilisent toujours le CLI distribué et vérifient séparément la politique conservatrice des macros et générateurs, sans prolonger les délais des tests Core.

Gui possède widgets/propriétés/événements, portées/Schema, ML/CSS et cycle de vie. Platform/Graphics/Image/Font sont des services publics, RmlUi/Stb/FontBackend des fournisseurs séparés. Préservez environnement, racines de génération, nettoyage propriétaire, paramètres de type/valeur et identité de compilation. Vérifiez dispatch JIT et repli explicite du modèle de coût ; un simple repli pour non-support ne suffit pas.

Utilisez l’entrée CMake autonome ou VEROSCRIPT_BUILD_GUI ; VEROSCRIPT_GUI_SCRIPTING active l’intégration de compilation/script. VEROSCRIPT_GUI_USE_VISUAL_SDK réutilise les fondations installées, VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS contrôle Stb/FontBackend. Reconstruisez contre Core/C++ Runtime compatibles après changement ABI/bytecode. Vérifiez séparément cycle natif, scripts, Schema, ML, consommateurs installés et chaque rendu/entrée/police/plateforme. Une revue statique ne certifie pas runtime/IDE.

Le programme hôte choisit explicitement la VM avec `FEngineConfig::VmBackendFactory` ou `VmBackendPath` ; les scripts de balisage utilisent `FMarkupScriptOptions::Engine`. GUI transmet ce choix à l’exécution et aux processeurs de compilation en conservant la configuration existante. La VM officielle est le fournisseur indépendant `vs-bytecode` ; lier ou installer Gui ne la sélectionne pas. Les arbres natifs et l’analyse ML/CSS sans script ne nécessitent pas de VM.

Les régressions de script sont enregistrées par scénario indépendant. `ctest --test-dir BUILD -R VeroScriptGuiScriptTests --parallel 4 --output-on-failure` les couvre tous. VeroScriptGuiScriptTests exécute directement tous les scénarios par défaut ou accepte un nom. Ce découpage conserve les assertions existantes ; les délais des tests ne changent pas les limites d’exécution de l’application.

Le cœur peut garder les accesseurs dans la VM pour le coût, les références ou finally. JitSynchronization distingue NativeLeafExecutions / NativeCfgExecutions, dispatch, entrées Linear/Bridge/VM trampoline et replis, puis vérifie une entrée native prise en charge et les compteurs par invocation lors de la réutilisation de Session. ScalarNativeFastPathCalls compte les appels scalaires de l’hôte, pas toute exécution native du script ; les compteurs imbriqués ne sont pas des appels indépendants à additionner. Les diagnostics détaillés restent désactivés par défaut : activez FJitPolicy.Diagnostics et lisez GetLastJitDiagnostics() dans une exécution séparée, jamais pour mesurer le débit.

`ApplicationOptions.FontBackend` et `TextLayoutBackend` sélectionnent deux services indépendants. Les champs vides héritent de `FApplicationFactories.DefaultBackend` ou des choix hôte/CLI (`--font-backend`, `--text-layout-backend`) ; `auto` choisit un candidat et `none` désactive le service optionnel. `FApplication::Create` utilise le repli conventionnel `font-backend` seulement si l’hôte natif ne fournit aucun choix pour ce service. Fournissez `FApplicationFactories.Font` / `TextLayout` ou les instances natives `Fonts` / `TextLayouts`. Les contextes RmlUi actifs partagent les instances immuables ; les fabriques enregistrées utilisent des caches faibles. Fermez les contextes avant toute reconfiguration. `FTextLayout::Create` reçoit d’abord le service choisi, dont l’identité participe au cache de paragraphes. Reconstruisez fournisseurs et consommateurs compatibles avec GUI `0.1.1`, Font `0.1.2` et text-layout `0.1.0` ; `ContractVersion` référence la `Version` de Gui. La revue statique ne certifie pas l’exécution.

## Régressions et détails du cycle de vie

Les tests couvrent activation directe, validation numérique à la perte de focus, défilement imbriqué, boucle Tab et focus flottant.

Vérifiez les statistiques réelles de dispatch et de repli JIT ; mesurez séparément mise à jour et rendu après préchauffage, hors compilation et attente de présentation.

Context.Post conserve les callbacks via les racines de valeurs du cœur liées à la génération ; après exécution ou abandon, les captures sont libérées à un point sûr du cœur. Context.SetFocus conserve l’invocateur de script actif pour les notifications synchrones des contrôles.

[User](../../User/fr-FR/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)

## Politique des imports source

La configuration CLI ordinaire conserve les liaisons natives et charge le framework selon les sources. Les noms GUI, annotations et ML généré le demandent ; commentaires et chaînes seuls ne le font pas. Les imports source se terminant par .vs suivent la même détection. Les petits imports locaux .vslib inspectent leurs macros publiques via des dépendances suivies ; les déclarations ordinaires et les macros sans paramètre constituées uniquement de littéraux ne demandent pas le framework. Les macros constantes numériques, de caractères ou de chaînes et les directives conditionnelles ordinaires seules ne le demandent pas. Les macros paramétrées de source, d’hôte ou de bibliothèque importée ou contenant des noms dans leur remplacement, la concaténation, la conversion en chaîne, les directives inconnues, les fournisseurs implicites opaques associés aux macros de l’hôte, les imports non résolus/échappés/logiques/volumineux et les processeurs personnalisés conservent l’import complet. Les tâches gui-* et ConfigureGuiScriptEngine gardent l’import implicite. Alignez cette politique sur les métadonnées et identités de cache ; les régressions doivent couvrir les sources sans GUI et celles qui l’introduisent par import ou processeur.

Pour les propriétés fixes, capturez les révisions des champs d’entrée avant le rappel et validez-les uniquement après sa réussite, si son enregistrement est inchangé. Les modifications pendant l’évaluation restent visibles au tour suivant ; échecs et réenregistrements ne valident aucun instantané périmé. Réutilisez les tampons d’entrée et effacez une configuration par défaut conservée uniquement lors d’un changement de source. Les getters dynamiques s’exécutent toujours à chaque tour de synchronisation.

`BakeGuiApplication` reste l’API de l’image Core interne destinée à l’intégration. Le CLI enveloppe ses octets avec `Application::BuildApplication` et `WriteArchive` au lieu de les écrire directement comme `.vsapp`. Le hook de préparation restaure les services de ressources GUI après disponibilité des fournisseurs. Le CLI et le Runtime avec GUI partagent ce chemin. Vérifiez ensemble la structure ZIP, inspect, la vérification et l’exécution déplacée sans sources. Sans Application, la vérification et l’aperçu des sources restent disponibles ; les opérations sur conteneurs signalent la capacité manquante.

Pour les générateurs source `import compiler`, le délai Core couvre aussi la préparation du graphe source GUI. Pour les grands graphes, configurez un budget adapté via `--compiler-timeout 20000` dans la CLI ou `FCompileOptions::ScriptCompileExtensions.TimeoutMilliseconds` dans l’hôte natif.

Quand les identités et révisions des entrées sont vérifiées inchangées, réutilisez leur instantané pris avant le rappel sans parcourir les champs à nouveau. La classification des sources peut être conservée, jamais le résultat des getters entre deux tours. Les contrôles numériques exacts du framework peuvent configurer leur état natif depuis les propriétés déjà synchronisées dans leur propre rappel natif, hors des getters. Synchronisation manuelle, classes dérivées et requêtes récursives gardent le chemin normal. Les getters parents des propriétés héritées restent évalués à chaque tour ; les valeurs validées inchangées évitent copie et validation répétées. La repartition native examine les groupes actifs, conserve la sequence des getters et traite toutes les liaisons actives.

Les attributs natifs numériques, booléens et textuels conservent directement leurs valeurs et utilisent une sortie par référence, sans objets optional temporaires. Un résultat absent conserve le repli et sera réévalué au prochain cycle. Pour les contrôles numériques exacts du framework, un hook généré vide peut être omis dans la portée native vérifiée. Les contrôles dérivés et les hooks déclarés conservent leur appel virtuel.

Les configurations natives fixes ne nécessitent pas de lecture périodique du script. Les changements de configuration, les mutations natives et le remontage réveillent la synchronisation ; les getters liés ou optionnels restent interrogés, même après un résultat absent.

Chaque contrôle possède ses propres objets de cache natif. Les mises à jour internes des instantanés préservent ainsi la stabilité du suivi des entrées ; les setters réveillent la synchronisation lors du remplacement de configuration.

Le type de base Widget exact peut également suspendre la synchronisation. Les sous-classes sans annotation conservent la synchronisation personnalisée conservatrice.

La classification d’une configuration attribute reste en cache jusqu’à son remplacement par Set/Reset. Elle n’évalue aucun getter. Les getters liés ou optionnels continuent à être évalués à chaque cycle requis.

Le remplacement conserve l’ancienne charge jusqu’à la publication de la nouvelle configuration et de sa classification. Une réentrée depuis le destructeur peut lire ou remplacer le nouvel état sans être écrasée ensuite.

## Liaisons notifiées et mises à jour groupées

Le graphe natif regroupe les notifications avec des abonnements faibles aux widgets. Les widgets possèdent leurs jetons et lecteurs attribute ; la destruction d’un jeton n’appelle aucun finaliseur de script. Un compteur monotone du Bridge horodate les notifications : le maximum des révisions d’un ensemble fixe de dépendances détecte exactement les changements, sans hachage ni tableau de script créé à chaque lecture. Un Binding partagé mémorise uniquement les lectures réussies, y compris les résultats optionnels vides. Les attributs dynamiques existants gardent leurs epochs.

IFrameBindingService ajoute une phase après tous les BeginFrame sans modifier l’interface virtuelle existante de `IFrameService`. La publication déduplique les widgets et réveille leur observateur de cycle de vie ; elle n’exécute pas de tâche de script par propriété et ne force pas l’invalidation de la mise en page. La synchronisation existante compare les résultats, conserve l’ordre du cycle de vie et reprend les getters échoués. Les observateurs des widgets scriptés couvrent aussi les mises à jour directes de l’hôte. La profondeur native des lots et les wrappers empêchent les mises à jour intermédiaires. SetX/ClearX détachent les abonnements ; l’installation interne publie la nouvelle configuration avant la libération des anciennes captures. FromAttribute conserve le parcours du repli pour les changements indépendants des attributs par défaut et styles natifs ; le résultat du getter transmis reste mémorisé selon les révisions de notification.

## Résultats de synchronisation et soumission du rendu

La synchronisation native réutilise les propriétés résolues après l’évaluation des getters, de l’héritage et des notifications de changement. Les valeurs modifiées et les animations, y compris leur arrêt ou leur fin, actualisent cette sortie. Les notifications conservent les instantanés de configuration valides. Chaque Widget possède un conteneur stable et crée les caches de propriétés natives lors de leur première utilisation ; ces écritures de sortie n’invalident pas l’instantané de configuration source. Les attributs dynamiques et le repli de FromAttribute conservent leurs règles d’évaluation et de reprise.

RmlUi utilise la portée facultative IRenderSubmission2D pour les passes consécutives d’un même moteur de rendu. Les passes productrices sont soumises avant leur lecture par le compositeur d’un autre moteur, puis l’enregistrement reprend. Les échecs et exceptions annulent les portées inachevées. Sans image de swapchain disponible, le travail non soumis limité aux cibles temporaires peut être annulé ; les captures persistantes restent exécutées. SDL_GPU et son fournisseur de module exposent cette capacité ; les autres moteurs gardent leur chemin habituel. OnPaint est toujours exécuté à chaque rendu.

Le marqueur d’invalidation du cache de script avance avant la publication native, afin qu’une interruption après publication ne laisse pas un résultat partagé marqué comme actuel. Les révisions des dépendances déterminent toujours les changements réels. Cet ordre ne rend pas l’exécution interrompue transactionnelle.

Observable.Set publie sa notification depuis finally, même si l’exécution s’arrête après l’écriture de la valeur. Une affectation échouée ou interrompue peut invalider les caches par précaution ; une valeur inchangée ne produit toujours aucune notification. La reprise reste soumise au contrat RequiresReset du Core décrit ci-dessous.

Les indicateurs temporaires d’évaluation, d’initialisation et de construction sont modifiés dans des blocs protégés ; les évaluations et lots imbriqués rétablissent leur profondeur initiale. Un rappel de désabonnement en échec reste réessayable, sans désabonnement récursif. La reprise après une limite, un délai dépassé ou une annulation dépend toujours du nettoyage borné du Core. Si le Core exige une réinitialisation, recréez la session de script GUI concernée et son contexte de liaison ; finally ne garantit pas de retour transactionnel de l’état applicatif.

Les propriétés natives sont synchronisées en neuf groupes dans leur ordre initial, en conservant les évaluations dynamiques et la récupération après exception. Les rappels faibles des contrôles vérifient à nouveau la libération dans le script ; une cible libérée termine la distribution sans rappeler le pont hôte.

Les initialiseurs de capture ML sont réécrits dans la portée englobante ; leurs alias restent locaux au corps de la fermeture. Les instantanés des sources utilisent le déballage de Core pour comparer l’objet attribut réel et sa révision derrière un stockage de capture par référence ou en lecture seule. Les champs objets ordinaires gardent leur accès direct, sans retenir les données capturées. Les rappels conservent FValueRoot et InvokeCallable ; les interfaces applicatives restent inchangées.

La synchronisation des propriétés natives scalaires ne revérifie la configuration qu’en l’absence de valeur, évitant une requête redondante sur le chemin courant. Les interfaces des caches et des accesseurs restent inchangées. Les groupes ordonnés réutilisent les variables de contrôle scalaires sans partager la portée des valeurs. Une lecture de Binding vérifie la récursion avant de réutiliser le cache de révision et évite un appel supplémentaire de rafraîchissement en cas de succès du cache.

Les recherches d’éléments natives et les tests de présence de style déclarent explicitement leurs métadonnées de membre en lecture seule : Core peut ainsi les appeler via des handles const ou capturés par valeur. Les opérations modificatrices conservent leurs métadonnées et les interfaces GUI publiques restent inchangées.

## Maintenance des artefacts natifs

Après modification des options natives, des structures publiques ou de la bibliothèque embarquée, reconstruisez ensemble fournisseurs GUI, hôtes, bibliothèques de scripts et candidats SDK/paquets. Un numéro de version produit identique ne prouve pas la compatibilité des implémentations.

## Choix des backends et dispatch interne

Les types auxiliaires et les points de dispatch des scripts utilisent `VS::Gui::Internal`, notamment `NativeAttributeCache<T>` et `NativeValueAttributeCache<T>`. Ils relèvent de l’implémentation du framework, et non des API applicatives. Les membres commençant par `Vsgui` restent réservés au code généré et aux hooks d’héritage pour éviter les collisions avec les membres applicatifs.

La détection des imports distingue l’usage d’un espace de noms/annotation d’une variable, d’un champ ou d’un paramètre simplement nommé `Gui` ; actualisez l’identité de configuration du processeur si cette détection change. Gardez bindings, appels générés et deux extracteurs d’éditeur alignés sur les noms qualifiés `VS::Gui::Internal`. `FContext` marque `bLayoutNotificationPending` après modification géométrique, publie toute la géométrie avant les callbacks et ne retire le marqueur qu’après livraison réussie. Les notifications échouées ou non livrées restent en attente ; celles déjà livrées ne se répètent pas sans nouvelle modification. Les changements au montage sont synchronisés avant la mise en page, ceux du callback de mise en page au prochain `Update`. Entrée/rendu exigent une mise à jour réussie. Vérifiez séparément échec/reprise, livraison aux voisins et géométrie visible des callbacks.

## Implémentation de la synchronisation

Le chemin statique exige un type d’exécution exact, une initialisation terminée et aucune synchronisation script personnalisée. Les sous-classes inconnues conservent le chemin prudent. Les configurations primitives fixes peuvent suspendre l’interrogation ; remplacement, setters, styles, mutations de l’hôte et installation de liaisons la réactivent. Les getters liés et optionnels restent interrogés, même avec un résultat vide. Les observations faibles ne doivent pas retenir les widgets.

Les propriétés primitives générées gardent des instantanés non encapsulés des valeurs locales, styles et sorties. Ne validez les jetons de changement qu’après la réussite des callbacks ; échec et réinscription ne doivent pas valider des entrées périmées. Les jetons natifs évitent les relectures inchangées sans supprimer getters dynamiques ni codecs personnalisés. Les styles conservent les littéraux intégrés analysés ; expansion des variables, ressources, sélecteurs et thèmes restent actifs. Les méthodes Builder communes sont émises avec leur type pour réduire les opérations de déclaration, sans changer la syntaxe.

Pendant la synchronisation native, les vérifications répétées du widget monté courant partagent la portée validée à l’entrée du callback. Restaurez-la après succès ou échec ; les autres widgets et les requêtes hors callback gardent leurs vérifications natives. Préservez évaluation des attributs, repli optional, stabilisation et ordre du cycle de vie.

## Repli de Context et routage des options Runtime

Le constructeur standard `__NativeContext` de `Private/Bindings/VeroScriptGuiRuntimeBindings.cpp` consulte `FScriptServices.DefaultBackend("gui")`, puis `Application.DefaultBackend("gui")` résolu si l’argument est omis. Revérifiez le résultat vide avant `Factories.Gui`, et n’utilisez `rmlui` que dans ce cas ; préservez ID/auto/none explicites. `FScriptServices.CreateContext` contourne cette branche et relève de l’hôte. Dans `VeroScriptRuntimeMain.cpp`, conservez VM/JIT dans la configuration d’exécution et `SelectedBackends` des paquets ; excluez `vm` et `jit` des valeurs transmises à l’adaptateur de commandes GUI. Vérifiez séparément callbacks absents, résultats vides, priorité d’environnement, choix explicites et vérification/exécution de conteneurs GUI avec options VM/JIT.

## Réutilisation de scène et limites de synchronisation

La synchronisation des styles réutilise la structure inchangée de la scène et les correspondances des sélecteurs ; les sorties de géométrie, de texte et d’animation ne les invalident pas à elles seules. La résolution des ressources, le décodage des valeurs et les getters dynamiques conservent leurs règles de synchronisation. Seuls les widgets dont le code généré confirme l’absence de travail périodique cessent d’être interrogés ; les liaisons dynamiques et les comportements de synchronisation inconnus suivent le chemin existant.

`FMarkupRuntime::MatchesScene` compare thème, widgets, parents, portées, appartenances et feuilles avant de réutiliser `FContext::SceneStyles`. Les gagnants des sélecteurs suivent éligibilité, focus/modal/survol/appui et entrées de style ; les getters d’éligibilité dynamiques désactivent le réemploi. Validez les entrées antérieures à l’application pour que les sorties invalident le tour suivant. Effacez la scène au remplacement de racine/destruction. `CanSuspendSynchronization` reste prudent avec méthodes/constructeurs inconnus, génération tierce, codecs et anciennes bases empaquetées sans helpers. Les noms privés générés gardent l’identité canonique de liaison du module.

## Configuration de compilation GUI

GUI `CompilerOptions` transmet la VM choisie aux extensions de compilation ; `FallbackVmBackendFactory`/`FallbackVmBackendPath` ne servent qu’en l’absence de VM explicite et si `bClearVmBackend` est faux. `ExecutionControl` est hérité seulement si les options d’extension n’en fournissent pas. Désactivation explicite et contrôle propre à l’appelant restent prioritaires.

<a id="gui-text-layout-maintenance"></a>

## Invariants des consommateurs de mise en page

L’identité du cache de paragraphe inclut l’instance de service et sa configuration immuable, le texte logique et la direction ; un identifiant de backend seul ne suffit pas. Gardez le paragraphe logique complet entre plages de style, choisissez le retour à la ligne avant l’ordre bidi de la ligne et conservez les leases par les services résolus. Appliquez l’espacement après le dernier glyphe de chaque cluster visible, sans modifier avances ni décalages d’origine, pour aligner dessin, largeur et carets. Préservez les espacements négatifs finis et le bornage de la largeur finale à une valeur non négative. Le presse-papiers et les offsets restent logiques. Les limites publiques de façonnage, de clusters et de capacités relèvent du [contrat Font](../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md).

<!-- docs-navigation -->

---

[Documentation de développement](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
