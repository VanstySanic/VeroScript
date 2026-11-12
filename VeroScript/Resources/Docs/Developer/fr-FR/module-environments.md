# Environnements de modules résolus et extensions natives

## Responsabilités et points d’entrée

Core reçoit un instantané immuable déjà résolu et filtré, et fournit les contrats communs de sélection des backends. Les clients et hôtes lisent les paramètres, résolvent les versions et fournissent les candidats installés. Core ne télécharge, ne décompresse, n’installe et ne recherche ni installations ni chemins système. Installer un paquet n’importe pas implicitement toutes ses déclarations.

Construisez `FModuleEnvironmentDesc`, puis appelez `FModuleEnvironment::Create`. Affectez l’environnement partagé immuable à `FCompileOptions::ModuleEnvironment` et transmettez-le à `VeroScriptVm::SetModuleEnvironment` avant l’exécution. L’export de source utilise `FCodegenOptions::ModuleEnvironment`. L’exemple SDK compilable est dans `Resources/Examples/ModuleEnvironment`.

## Modules et portée

`Scopes` décrit les propriétaires et leurs dépendances directes. `EntryOwnerId` choisit la portée d’entrée ; `bHostProvided` autorise explicitement tous les modules à accéder à la portée hôte. `SourceRoots` contient des chemins absolus canoniques avec leur propriétaire. `Modules` associe chaque nom d’import public à un ID de module, un propriétaire, une identité de contenu et un fichier, ou à un module mémoire `veroscript-module:/...` avec `Source`.

Les imports ordinaires consultent les correspondances exactes ; un candidat local concurrent est une erreur. Un import explicite `./` ou `../` utilise uniquement le chemin relatif à l’importateur, sous contrôle des racines et dépendances ; une cible absente n’entraîne aucune recherche dans d’autres racines. Une correspondance invalide ne revient pas à un fichier homonyme. `import`, `include`, les imports réexportés et la découverte des macros partagent le même chargeur. Par défaut, les sources sont strictement délimitées et les recherches implicites dans l’environnement ou le répertoire courant sont désactivées ; la bibliothèque standard exige aussi des racines et une portée explicites.

Un module accède à ses propres sources et à ses dépendances directes déclarées. Les interfaces réexportées suivent les règles de visibilité du langage. Les déclarations natives portent un `FBindingOrigin` ; la compilation vérifie propriétaire et visibilité des imports. Une signature publique peut exposer les types natifs nécessaires à l’utilisation de ses valeurs de retour. Cette isolation des dépendances ne constitue pas un bac à sable pour du code natif non fiable.

Publiez une seule fois les catalogues et contrats de ressources, puis conservez leurs déclarations et identités de callbacks. `Apply` fusionne les entrées de compilation de façon transactionnelle et refuse les catalogues actifs incompatibles. Construisez un instantané commun à partir du même registre de base ; ne concaténez pas des bindings actifs utilisant des espaces d’ID de types indépendants. Les surcharges explicites de source restent appliquées par chemin exact.

## Services de ressources et localisation

Les entrées `FResourceProviderBinding` contiennent le fournisseur, son propriétaire, une identité stable du contrat et les éventuels `ModuleIds` réservés aux ressources. `IResourceProvider` reçoit la requête logique `(ProviderId, ModuleId, Group, Path)`, un contrôle d’annulation et une limite d’octets. Les chemins sont relatifs, portables et sans traversée. Une ressource absente avec succès se distingue d’une erreur d’E/S ou du fournisseur. Toute ressource présente exige une identité de contenu ; son éventuel chemin physique absolu sert au diagnostic et n’autorise pas à contourner le fournisseur.

À l’exécution, utilisez `FModuleEnvironment::ReadResource`. Les processeurs de compilation utilisent `Context.ReadResource` et les extensions script `read_resource(provider,module,group,path)`. Le registre normal des dépendances mémorise contenu lu et absence observée ; leurs changements invalident les résultats préparés. Les fournisseurs doivent être sûrs en concurrence, respecter limites et annulation et éviter les lectures sans borne. Core revérifie taille et identité. `MaxResourceBytes` limite une lecture ; la compilation limite aussi les entrées cumulées et les dépendances.

Réutilisez `I18n::FI18nServices`, les messages typés, les contextes et leurs limites via l’environnement. Le client de paquets interprète le JSON de métadonnées ; Core n’ajoute pas un format de localisation concurrent. Les extensions natives peuvent demander des services typés à l’hôte avec `GetService(id,version)` ; les identités déterministes de ces services entrent dans les clés du cache des processeurs.

## Enregistrement natif et durée de vie

Les ID natifs gérés par paquet (`entries.native[].id`) suivent `[a-z0-9]+(?:-[a-z0-9]+)*`. Le schéma autorise 2–64 caractères, mais `Descriptor.Id[64]`, terminé par NUL, limite un ID natif chargeable à 63 caractères ASCII. Descripteur, manifeste et `ExpectedId` doivent correspondre ; `entries.backends[].module` et les clés statiques `package:ID` désignent cet ID. Le chargeur Core de bas niveau valide son contrat UTF-8 borné sans imposer la grammaire des noms de paquet. Les modules fournis utilisent `veroscript-sdl3`, `veroscript-rmlui`, `veroscript-stb`, `veroscript-font-backend` et `veroscript-gui-tools` ; reconstruisez bibliothèques, catalogues et environnements après un changement d’ID.

Les ID de service, les choix de backend et les ID de contrat d’interface suivent leurs propres protocoles. L’extension native `veroscript-gui-tools` demande notamment le service hôte distinct `veroscript.gui.tools`, qui reste la clé de recherche `ToolServiceId`.

Incluez `VeroScriptNativeExtensionLoader.h`. `LoadNativeExtension` exige un fichier de bibliothèque absolu explicite et ne découvre ni paquets, ni noms, ni PATH, ni répertoire courant. Un module lié statiquement utilise `QueryNativeExtension` avec la même fonction de requête et un propriétaire. Le symbole C exporté est `veroscript_extension_query` ; `NativeExtensionApiVersion` vaut **0.1.1**. Vérifiez ID, version et rôles attendus avant l’enregistrement.

Le descripteur de requête à disposition fixe doit correspondre à l’ABI SDK publique, à l’identité d’implémentation Core, au compilateur/STL/runtime, à l’architecture/plateforme et aux modes RTTI et exceptions. La requête C ne rend pas les objets C++ suivants compatibles avec une ABI C stable. Les extensions MSVC dynamiques exigent le CRT partagé ; iOS et WebAssembly utilisent l’enregistrement statique. Le hachage du binaire identifie le cache, sans vérifier authenticité ou signature ; le client valide les artefacts de confiance et conserve les baux des fichiers immuables.

Préparez un module avec `FNativeExtensionRegistry::Add`. Son `Create` reçoit `INativeExtensionHost` pour enregistrer fonctions, types/membres, modèles génériques, alias, annotations, processeurs de compilation et fournisseurs de services. L’extension utilise la table virtuelle hôte sans lier un Core statique privé. L’enregistrement est atomique ; un échec conserve l’instantané précédent. `GetBindings`, `GetProcessors`, `GetProviders`, `GetOwners`, `GetDependencies` et `GetFingerprint` assemblent l’environnement résolu. Renseignez les contrats disponibles à partir de `GetDependencies`, qui reflète descripteurs chargés et identités binaires vérifiés, et non de la seule version du paquet. Leur version est celle du composant natif, distincte de la version de distribution. Tout fournisseur de bindings natifs exige un contrat de dépendance externe exact disponible.

Les services hôte sont des interfaces versionnées à durée de vie possédée. Seule la phase d’enregistrement peut modifier les déclarations. Ensuite, les services retenus peuvent créer des objets hôte, lire des ressources et soumettre des opérations de compilation via le pont. Utilisez `MakeHostObject`/`MakeHostObjectNamed` et une fonction de destruction qui ne lève pas d’exception ; Core alloue les blocs de contrôle. Les références fortes retiennent le code du module tant que vivent callbacks, fabriques/instances de processeurs, fournisseurs, services et objets échappés. Évitez les références fortes en retour vers les objets propriétaires du même enregistrement ; utilisez des références faibles. Fabriques et destructeurs respectent leurs contrats d’erreur et de propriété ; les destructeurs ne lèvent jamais d’exception.

L’enregistrement d’un fournisseur n’initialise ni GUI ni autre service. Au-dessus de Core, choisissez `(ServiceId, BackendId)`, vérifiez le contrat et appelez `CreateNativeService`. L’instance conserve environnement et code jusqu’à sa destruction. JIT utilise le protocole existant `LoadJitBackend`.

Les modèles génériques natifs gardent le contrat du registre : instanciez les types concrets nécessaires avec `FHostBindingRegistry::InstantiateGenericHostType` dans le registre de préparation avant de publier le catalogue immuable. L’exécution ne modifie pas le catalogue publié ; les génériques script sont instanciés normalement par le compilateur.

## Cache, paquets et rechargement

Les empreintes de compilation incluent propriétaires du graphe, identités source, bindings, versions/configurations des processeurs, annotations et politique du compilateur. La configuration de l’enregistrement natif entre automatiquement dans l’identité des processeurs, même sans empreinte hôte séparée. Les ressources ajoutent leurs dépendances de lecture exactes. La configuration d’exécution possède une identité distincte. Des services natifs non identifiés ou non déterministes désactivent le cache des processeurs. L’hôte doit fournir des identités stables pour les autres callbacks ou configurations opaques qui influencent le code généré.

`FExternalDependency` conserve ID/version/ABI/build/identité de contenu exacts ; Core les vérifie, fusionne et sérialise sans résoudre les versions. Les contrats requis survivent au bytecode, aux `.vslib`/`.vsapp` et à l’édition de liens. Un contrat d’exécution absent ou incompatible échoue avant les initialiseurs ou JIT. Les sources génériques ouvertes conservent la portée du producteur ; l’hôte importateur fournit des bindings compatibles et les exigences mappées. Les propriétaires des modules auxiliaires empaquetés doivent aussi avoir leurs portées et dépendances directes dans l’environnement effectif, même si les sources sont intégrées à la bibliothèque. Produit et bytecode restent en **0.1.1** ; les anciens bytecodes de prépublication sont refusés.

L’AOT autonome peut exporter les résultats purement script avec le même environnement et les déclarations générées. Il refuse les contrats natifs requis à l’exécution tant qu’un adaptateur cible ne fournit pas leur déploiement et leurs bindings ; il ne remplace pas silencieusement les callbacks d’extension par des déclarations externes sans rapport.

Le rechargement publie un nouvel environnement à une frontière inactive. Le travail en cours conserve l’ancien instantané et ses baux. Un module sans `NativeExtensionParallelRegistrations` exige un redémarrage avant un nouvel enregistrement. Le remplacement sur place d’une bibliothèque chargée est refusé ; utilisez un chemin de version immuable. Les objets natifs portent l’identité de leur enregistrement : un ancien objet ne peut entrer dans de nouveaux callbacks, même si son type porte le même nom. Les règles de threads et d’annulation VM/session restent applicables ; un callback natif bloquant ne peut être interrompu de force. Les workers déjà démarrés gardent leur environnement initial.

## Vérification et intégration

`VeroScriptModuleEnvironmentTests`, `VeroScriptNativeExtensionTests`, les tests de headers publics et `VeroScriptModuleEnvironmentExample` vérifient les contrats Core. La bibliothèque de test dynamique ne lie volontairement aucun Core. Chaque cible de publication exige encore ses propres tests de chargement et de périphérique. Le client de paquets, la CLI et les adaptateurs Interop/langage possèdent leurs opérations d’installation et de sélection d’environnement ; Core reçoit leurs instantanés résolus.

## Service hôte de ligne de commande

Le rôle `CommandLine` (`0x8`, écrit `cli` dans le paquet) utilise le service SDK compatible `veroscript.cli` version `0.1.1`. CLI fournit `ICommandLineExtensionService` ; le module le demande avec `GetService` pendant Create et enregistre son descripteur avant la fermeture de l’enregistrement. Un module à rôles combinés doit accepter l’absence du service dans les hôtes SDK ordinaires. Le client ignore les entrées exclusivement CLI en analyse et dans les hôtes sans ce service. CLI possède l’analyse des options et le dispatch.

[CLI](../../../../../VeroScriptCli/Resources/Docs/Developer/fr-FR/process-and-assets.md)

## Sélection des backends et adaptateurs

`VeroScriptBackendSelection.h` définit `ResolveBackendSelection`, `SelectServiceBackend` et `ResolveExecutionBackends`. Une requête omise hérite de `SelectedBackends` ; une valeur vide enregistrée signifie `none`. Un `auto` explicite remplace le choix configuré. Les candidats sont triés par ID ; les ID vides, réservés ou dupliqués sont rejetés. Seul un échec de fabrique/préparation permet d’essayer le candidat automatique suivant. L’annulation se propage ; aucune opération en cours n’est rejouée.

`FBackendSelectionResult` distingue `Selected`, `Disabled`, `Unavailable` et `Failed`, avec `BackendId`, tentatives rejetées et erreur. Un service optionnel automatique peut être indisponible sans faire échouer l’environnement ; vérifiez la fabrique autant que le succès. `FInstalledEnvironment::GetBackendCandidates` lit uniquement les métadonnées et exclut les paquets désactivés. `LoadJitBackend` / `LoadVmBackend` peuvent rendre le résultat de sélection ; `BackendDiagnostics` conserve les diagnostics des modules fournisseurs automatiques indisponibles.

`FModuleEnvironmentDesc` transporte `SelectedBackends`, `ServiceProviders`, `BackendDiagnostics` et `ExecutionBackends`. L’adaptateur fournit des résolveurs immuables et `ExecutionBackends.Identity`, en conservant l’instantané installé et les baux de code. `AnalysisOnly` n’installe pas les résolveurs d’exécution. `FNativeServiceProviderDesc` est déclaré dans l’en-tête de sélection ; les helpers natifs vérifient service/backend/contrat/version avant création. C, C++, C# et Python partagent la résolution Session native, sans découverte de paquets propre à chaque langage.

`ResolveExecutionBackends` prépare une copie avant validation. `VmBackendFactory` / `VmBackendPath` explicites priment sur l’environnement ; fabrique, chemin et effacement explicite sont exclusifs. `FallbackVmBackendFactory` / `FallbackVmBackendPath` sont des entrées d’assemblage produit, utilisées seulement si la sélection automatique ne trouve aucune VM utilisable ; elles ne remplacent ni échec explicite ni désactivation. `bDisableJitBackend` empêche le défaut JIT de l’environnement. Session conserve le repli produit ; `SetFallbackVmBackendFactory` le modifie et `SetVmBackendPath("auto")` peut le rétablir avant exécution. Le domaine VM se fige au début d’exécution, JIT compris ; Clear/reload ne le libère pas. Conservez modules et environnements pendant la vie des fabriques, instances et codes retenus.

Avec un environnement de modules, un processeur doté de `Origin.ProviderId` reçoit seulement les événements des propriétaires autorisés à accéder au fournisseur, ou via son `Origin.ModulePath` exporté et visible. Le propriétaire producteur conservé dans un paquet et le propriétaire source capturé précèdent la recherche physique. Les processeurs globaux sans fournisseur et les hôtes sans environnement gardent leur portée. Les scripts restent explicitement activés par `import compiler` ou les racines configurées. Le filtrage ne donne aucun accès supplémentaire aux sources, ressources ou liaisons natives.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [en-US](../en-US/module-environments.md) · [fr-FR](module-environments.md) · [ja-JP](../ja-JP/module-environments.md) · [ko-KR](../ko-KR/module-environments.md) · [zh-CN](../zh-CN/module-environments.md) · [zh-TW](../zh-TW/module-environments.md)
