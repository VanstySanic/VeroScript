# Norme de packaging et de publication

Portée : [développement et contributions à VeroScript](README.md). Cette norme régit les collections officielles, les candidats et leur validation, sans modifier les licences ni les pratiques des projets externes.

## Norme et état de l’implémentation

Le registre commun et les scripts implémentent les chemins classés et le nouveau format. La planification pure et les régressions statiques ne remplacent pas des compilations réelles réussies et les contrôles d’intégrité des contenus. Enregistrer séparément les résultats d’exécution cible et de cycle de vie des installateurs, y compris les échecs et les contrôles non exécutés. Accepter uniquement le nouveau contrat, sans compatibilité, conversion, double écriture ni migration ; supprimer les anciens résultats gérés puis reconstruire.

Synchroniser registre, versions, planification pure, producteurs, charges, manifestes, validation, transactions, nettoyage et CI. Définir des schemaVersion explicites ; les nouveaux lecteurs acceptent uniquement le nouveau contrat et refusent sûrement les anciens formats sans branches de compatibilité. Versions de format et produit sont indépendantes ; les versions produit restent définies par le registre. Voir la [disposition de publication](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/release-layout.md). Ancien `migrate-layout` exclu du nouveau processus.

## Répertoires et responsabilités

La racine officielle est `Release/` sous la racine source, indépendamment des noms externes `Script` ou `VeroScript`. Conserver les chemins internes lors d’un déplacement et reconstruire les caches dépendant des chemins compilés. Candidats, caches et preuves de validation restent dans un staging dédié hors de Release.

```text
Release/
  release-index.json
  indexes/<target>.json
  Runtime/<target>/<version>/
  CoreSDK/<target>/<version>/
  Interop/<target>/<version>/
  Resources/any/<version>/Resources/
  Modules/
    Execution/{Jit,Vm}/<target>/<version>/
    Network/{Json,Network,Http,WebSocket}/<target>/<version>/
    Network/Backend/{Boost,Curl}/<target>/<version>/
    Multimedia/{Platform,Graphics,Audio,Image,Font,Gui}/<target>/<version>/
    Multimedia/Backend/{Sdl3,Stb,FontBackend,RmlUi}/<target>/<version>/
  Extensions/
    Language/{Cpp,CSharp,Python}/<target>/<version>/
    Editor/VSCode/{VSCode,GuiVSCode}/<target>/<version>/
    Editor/JetBrains/{JetBrains,GuiJetBrains}/<target>/<version>/
    Platform/Mobile/{Android,iOS}/<target>/<version>/
    Platform/WebAssembly/<target>/<version>/
```

`<business-route>` comprend le chemin de catégorie enregistré et le nom du produit. `<target>` désigne un périmètre de distribution : cible native/mobile, `any` ou `wasm32`, et non l’hôte de compilation. `{A,B}` représente des répertoires distincts.

Créer uniquement les catégories utilisées ; manifeste et sommes restent à la racine de version :

| Chemin | Rôle |
| --- | --- |
| `archives/` | Distributions runtime déployées par extraction |
| `sdks/` | SDK avec interfaces de développement, exports de construction et dépendances nécessaires |
| `packages/` | `.vspkg`, `.vsix`, ZIP de plugin, `.nupkg`, wheel, sdist, AAR |
| `installers/` | Installateurs pris en charge et conteneurs de téléchargement choisis |
| `symbols/` | Symboles du produit propriétaire, dont `.snupkg` |
| `Resources/` | Arbre libre Assets, Docs, Scripts, Examples, réservé à Resources |
| `release-manifest.json`, `checksums.txt` | Inventaire et empreintes du produit, périmètre et version |

Classer selon l’usage et le rôle déclaré, pas seulement l’extension. Ne pas réemballer mécaniquement les paquets dans un ZIP équivalent ; conserver une archive SDK complète si elle apporte en-têtes ou ressources de construction. Les conteneurs Unix préservent permissions et liens de bibliothèques nécessaires. Choisir par défaut une seule forme de téléchargement de l’installateur ; conserver forme brute et compressée uniquement pour des usages distincts. Ces catégories ne changent pas automatiquement les chemins installés.

Runtime, CoreSDK, Interop et Resources sont à la racine ; Cpp, CSharp, Python, WebAssembly, quatre produits d’éditeur et Android/iOS sont dans Extensions ; moteurs, réseau et multimédia sont dans Modules. Le client de paquets reste intégré à Runtime/CoreSDK, sans nouveau produit. Enregistrer produits, dépendances, rôles, capacités et motifs d’exclusion. La complétude dépend de la cible et du profil sélectionnés, pas de tous les produits sur chaque plateforme.

Noms fixes au pluriel : `archives`, `sdks`, `packages`, `installers`, `symbols`, `indexes`. Conserver les catégories sources sous Modules et Extensions, dont Execution, Network, Multimedia, Language, Editor, Platform et Backend ; identifiants métier et noms de paquets restent stables.

## Cibles et artefacts partagés

Séparer les distributions natives par système et architecture : `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`. Les autres architectures enregistrées doivent préciser leurs profils réellement pris en charge ; leur enregistrement ne qualifie pas une publication complète. `android` est multi-ABI ; `ios` couvre appareils et simulateurs, avec le même SDK pour iPadOS. Le SDK WebAssembly utilise `wasm32`, ses installateurs gardent la cible hôte.

`any` est l’emplacement commun d’une distribution non découpée par cible native : fichiers neutres ou même paquet couvrant plusieurs cibles. Déclarer exactement `supportedTargets`, ABI, TFM, dépendances runtime et limites ; `any` ne signifie pas toute plateforme. Les AAR multi-ABI Android et XCFramework iOS gardent leur périmètre plateforme.

Conserver une seule distribution partagée par produit/version, référencée par les manifestes. Les quatre plugins actuels, Resources neutre, les sdists Python nettoyés et le même NuGet C# Android/iOS utilisent cet emplacement. Générer un SDK JS/WASM canonique, sans qualifier son installateur natif d’universel. Un produit mixte peut avoir `any` et des dossiers natifs : sdist Python dans `any`, wheels selon leur plateforme/ABI réelles.

Fusionner seulement si produit, version, usage, support, dépendances, ABI et empreintes finales concordent ; nom ou extension ne suffisent pas. Former les octets canoniques suivant un contrat de génération explicite avant manifestes, signatures et empreintes. Texte propre en UTF-8/LF, avec exceptions BOM et batch requises ; ne pas réécrire mécaniquement binaires, signatures, originaux tiers ou fixtures sensibles aux octets. Les références formelles ne reposent pas sur des liens symboliques ou physiques entre dossiers.

Installateurs autonomes, wheels et SDK hors ligne gardent leurs dépendances runtime déclarées. L’utilisation indépendante prime sur la déduplication disque : ne pas déplacer une bibliothèque requise dans un dossier Release voisin supposé disponible chez l’utilisateur.

Distinguer les cibles réelles dans `supportedTargets` des cibles consommatrices autorisées dans `consumerTargets`. Un SDK wasm32 peut être référencé par les index natifs. La composition conserve des versions réelles compatibles et correspondantes des paquets.

## Versions et dépendances indépendantes

Produits et cibles peuvent avoir des versions indépendantes. `ReleaseVersions.json` fournit des valeurs par défaut, pas une version produit globale. Une distribution partagée a sa version explicite ; une mise à jour plateforme ne remplace pas silencieusement le contenu encore référencé ailleurs. Épingler versions et SHA-256 des manifestes, sans résolution implicite du dernier. NuGet, wheels, sdists, plugins et binaires conservent leurs vraies versions ; ne pas renommer ni falsifier les tags plateforme.

Consigner séparément version native, version produit, ABI publique, identité de construction et format. Vérifier Cpp/Core, extension GUI/plugin de base et modules selon versions, ABI, identités ou empreintes réelles. Ne pas remplacer silencieusement les octets d’une identité déjà publiée extérieurement. Reconstruire un candidat local non publié met à jour les empreintes et invalide les validations affectées, avec politique de version explicite.

Une identité de paquet doit désigner un contenu unique. Ne pas placer des NuGet ou sdists différents avec mêmes ID/version dans plusieurs dossiers plateforme pour les distinguer par leur chemin. Employer TFM, RID, ABI ou tags de l’écosystème pour les variantes ; si la fusion est impossible, définir explicitement des identités distinctes en respectant les contrats ID/version existants.

Après publication d’un manifeste partagé `any`/`wasm32` dans une collection gérée, son identité produit/périmètre/version est immuable, y compris les symboles facultatifs et les cibles déclarées. Rassembler l’inventaire partagé complet avant de le sceller ; toute modification exige une nouvelle version du produit. Une reconstruction propre ne conserve la version configurée qu’après suppression explicite des sorties locales non publiées ; supprimer les fichiers locaux n’autorise pas à modifier une identité déjà distribuée aux utilisateurs.

Les manifestes `any`/`wasm32` ne portent pas la version native de l’hôte éditeur. Le contexte de version native C# appartient à chaque entrée métier de l’index consommateur ; l’identité partagée dépend du contenu et du contrat du paquet.

## Index, manifestes et références

`release-index.json` utilise la version de format **1** et le produit `VeroScript Release Set` ; `indexes/<target>.json` utilise **5**, le manifeste métier **3**. Le manifeste candidat temporaire utilise **2** et le produit `VeroScript Release Candidate`. `checksums.txt` est propre au métier, périmètre et version. Chaque référence dans `manifests[]` indique périmètre, version réelle, chemin relatif et SHA-256 ; catégories et rôles proviennent du registre commun.

`release-index.json` est l’entrée de collection : versions de disposition/format, génération, chemins relatifs et SHA-256 des `indexes/<target>.json`, sans version produit commune fictive. Chaque index cible sélectionne indépendamment ses distributions ; l’index partagé gère les distributions communes.

Un produit dans un index cible peut référencer plusieurs manifestes, par exemple wheel Python natif et sdist `any`. Chaque référence précise `target`, `version`, chemin relatif et SHA-256 du manifeste. Abandonner l’hypothèse d’un seul manifeste de même cible par produit. Les produits et les cibles peuvent choisir des versions indépendantes ; les composants partagés et natifs composés pour un même produit dans un même index cible doivent conserver des versions d’écosystème/produit correspondantes et passer ensemble les contrôles de compatibilité et de complétude.

Le manifeste produit consigne produit, périmètre, vraie version, identité source/construction, `supportedTargets`, dépendances, rôles et inventaire complet. Chaque fichier indique chemin classé relatif, taille et SHA-256. Les entrées natives indiquent système, architecture, ABI, système/API minimum, outils et tranches ; les paquets ajoutent ABI Python, TFM et sélecteurs nécessaires. Générer `checksums.txt` du même inventaire. Sélection, chemins, métadonnées et identités binaires doivent concorder.

Résoudre les fichiers depuis la racine de version produit et les chemins des manifestes d’index depuis Release. Refuser traversée, absolus, liens sortants, alias de casse/noms réservés, doublons, fichiers inconnus et cycles. Références entre périmètres uniquement vers des manifestes gérés supportant la cible ; vérifier toute la fermeture de dépendances/références sans déduire le contenu des noms de dossier.

L’inventaire couvre artefacts et ressources libres, sauf son propre manifeste et `checksums.txt`, pour éviter les empreintes autoréférentielles. Les index parents épinglent les manifestes et l’entrée racine les index. Consigner ou vérifier types de fichiers, permissions exécutables et liens relatifs sûrs selon le format, sans perdre l’exécutabilité en ne comparant que les octets des fichiers ordinaires. Les empreintes ne remplacent ni l’identité de l’éditeur ni la politique de signature effective.

Les index distinguent composants sélectionnés publiés et plateforme complète. Consigner produits requis par profil, manques, produits partial et contrôles non exécutés ; la réussite d’un composant ne certifie pas toute la plateforme. Ne pas fabriquer d’index réussi sans artefacts formels ; garder les lacunes de construction/validation dans les rapports candidats.

## Charges fonctionnelles et sources

Runtime, SDK, Extensions, Modules et installateurs contiennent fonctions, ressources runtime, métadonnées et notices légales, sans README, manuels, tutoriels, exemples, tests, benchmarks ni historique. Resources fournit User intégré et ressources liées. Standards relève des développeurs/contributeurs dans All seulement, exclu de User et des paquets fonctionnels. Garder licences, NOTICE, localisation, icônes, syntaxes, extraits et données nécessaires. Node.js/Sharp de génération d’icônes ne deviennent pas des dépendances Runtime/SDK.

Le fichier racine `extension/readme.md` du VSIX VS Code constitue les métadonnées de sa page Détails. Ce fichier de présentation précis peut être inclus s’il correspond au texte du README maintenu du module et renvoie à des ressources du paquet ou à des guides en ligne. Les autres README, manuels et arborescences Developer/Tutorials restent exclus des paquets fonctionnels.

Tous les Modules, Jit et futurs modules compris, sont sans installateur de produit autonome ; leurs symboles sont distribués séparément sous le produit propriétaire. L’installateur exclut les choix documentation/exemples et conserve pagination bornée et propriété. Décocher/désinstaller associations, icônes ou PATH ne nettoie que les réglages possédés par cette installation.

Le sdist est l’exception explicite de source constructible : inclure le minimum de sources, en-têtes, scripts, métadonnées et notices pour construire après extraction indépendante, sans explications/exemples inutiles. Exclure et contrôler `.cxx`, `.gradle`, `.externalNativeBuild`, caches CMake, objets, sorties compilées et chemins locaux. `.gitignore` ou un checkout hôte fortuitement propre ne suffisent pas. Inspecter les vrais membres d’archive et reconstruire les consommateurs depuis un sdist isolé si nécessaire.

Installer un module, activer un paquet et sélectionner un backend/fournisseur sont des opérations distinctes. La configuration hôte suit les règles communes omission/auto/none/choix explicite ; auto peut choisir parmi les candidats installés, activés et compatibles. Publication et installation préservent la configuration des backends et les choix désactivés de l’hôte, sans ajouter silencieusement Jit ou les fournisseurs optionnels au contenu Runtime/CoreSDK de base.

Les icônes proviennent des Assets officiels et de l’inventaire automatique. Ajouter les nouveaux fichiers au contrôle de version et aux entrées CI, sans fixer un nombre historique. Comparer individuellement SVG/PNG/ICO/ICNS et autres ressources brutes/binaires à leurs sources ; les références installées utilisent des chemins stables du produit. Reconstruire programmes/installateurs lors d’un changement d’icône embarquée ; remplacer un ICO externe ne reconstruit pas l’EXE. Associations, rafraîchissement et désinstallation respectent toujours la propriété de l’installateur.

La validation avec les sources vérifie également les images des icônes intégrées aux EXE Windows CLI, Runtime, launcher, LSP, DAP et d’installation, y compris les builds réutilisés. Le PNG produit déclaré par VS Code est comparé directement à la ressource maintenue.

## Android, iOS et SDK de modules

Le SDK Android principal intègre Core/Interop, adaptation Mobile commune, JNI, AAR, bibliothèque standard, ressources et licences ; iOS intègre son adaptation statique et XCFramework. La couche Mobile n’est pas un autre produit à installer. Le SDK principal ne contient pas automatiquement tous les modules optionnels.

`Modules/<category-route>/<business>/<android|ios>/<version>/sdks/` est un développement CMake natif optionnel, ni autre SDK principal ni `.vspkg` de bureau. Garder en-têtes, exports CMake et fermeture déclarée, distinguer dépendances embarquées/externes et éviter conflits ou édition de liens double. L’AAR principal ne justifie pas la suppression d’API C++ ou de SDK de modules requis indépendamment.

Une publication mobile complète couvre adaptation, CoreSDK, Cpp, Interop, Vm, CSharp, Python et modules optionnels sélectionnés. Une application ordinaire consomme le SDK principal et les modules nécessaires sans télécharger tous les kits. Android comprend actuellement arm64-v8a/x86_64 ; iOS device-arm64, simulator-arm64 et simulator-x86_64. Vérifier binaires et liens consommateurs par tranche ; même CPU ne signifie pas même système/ABI appareil-simulateur. Exclure explicitement le JIT natif indisponible sur iOS, sans renommage simulant un support.

Sélectionner C# selon TFM, bibliothèques et ressources effectifs, Python selon matrice CPython déclarée et tranches mobiles. Associer .NET, Xcode, NDK et dépendances cibles compatibles. Distinguer version SDK de compilation et système minimum d’exécution.

## Compilation croisée et validation

Distinguer précontrôle des sources/configurations, compilation réelle, assemblage, validation statique, exécution cible, publication et vérification finale. Produire les véritables artefacts cibles lorsque la compilation croisée est possible ; une machine locale ne pouvant pas les exécuter ne prouve pas une impossibilité de compilation. Les paquets compilés et vérifiés peuvent entrer dans Release même si la recette reste non exécutée ou échoue. Conserver explicitement ces états ; une compilation réussie ne vaut pas recette réussie.

Consigner hôte et cible séparément. Employer compilateurs, sysroots, en-têtes et dépendances cibles ; les générateurs exécutés sont des versions hôtes. Qualifier individuellement natif, C# AOT, wheels, installateurs et bibliothèques tierces, sans simple changement de RuntimeIdentifier, nom ou tag. ARM64 de bureau n’hérite pas des validations mobiles.

La recette distingue `passed`, `failed`, `not-run` et `not-applicable`, avec motifs, environnement, outils, configuration et empreintes des preuves. Suivre séparément compilation/édition de liens croisée, émulateur, appareil réel, interface graphique, cycle installation/désinstallation, signature/notarisation, C# AOT/élagage et Python. Compilation réelle et vérifications des charges, architectures, sources, dépendances, inventaires et SHA-256 restent obligatoires avant distribution. Les lacunes de recette ne bloquent plus la diffusion des charges compilées. Ne jamais transformer une omission, un échec, une absence de signature ou d’outils en réussite. Le champ métier `qualification`, version 1, lie les contrôles aux chemins et empreintes exacts de `manifests[]` ; toute modification invalide les anciennes preuves. `complete` indique uniquement un inventaire complet ; `qualificationStatus` résume la recette. `qualified` vaut true seulement si l’inventaire et tous les contrôles requis sont validés. Une preuve absente est `not-recorded`, jamais une réussite supposée.

Les installateurs macOS utilisent `.dmg` contenant une `.app` standard, l’ICNS officiel, Info.plist et l’exécutable. Linux utilise `.tar.gz` préservant les permissions et Windows `.exe`. Le format des SDK est indépendant. Terminer la signature avant création et validation de l’image ; une signature ad-hoc ne vaut ni Developer ID ni notarisation. Sur un autre système, l’audit DMG exige l’outil de construction 7-Zip (`VEROSCRIPT_7ZIP` ou PATH), sans nouvelle dépendance d’exécution.

L’assemblage croisé de bureau utilise `Resources/Tools/Packaging/package-veroscript-desktop-cross.py`. Le compilateur cible évalue les expressions des en-têtes officiels pour produire les ABI, sans copier celles de l’hôte. Un catalogue neutre ne peut être réutilisé sans exécution que si les sources sont identiques et les exports concordent sur au moins deux plateformes natives. Une cible 32 bits exporte son propre catalogue. `--target-tool` permet un outil cible ou un émulateur ; cette lecture ne vaut pas recette complète. Les outils hôtes peuvent assembler, vérifier et archiver une autre cible ; l’installation réelle exige toujours le système et l’architecture correspondants.

Terminer signatures, notarisation et traitements changeant les octets avant de consigner les empreintes finales et d’y lier les preuves ; vérifier séparément l’inventaire interne. Une empreinte avant signature ne certifie pas le fichier signé. Revérifier dossiers finaux, archives, références et charges embarquées après publication ; tout changement d’octets invalide la preuve correspondante.

## Publication, annulation et nettoyage

Publication et nettoyage partagent le verrou de collection. Vérifier candidats, support, fermeture et provenance, préparer fichiers/manifestes/index gérés puis commettre produits, index cibles et enfin entrée racine dans une transaction récupérable. Restaurer contenu et références modifiés en cas d’échec. Les lecteurs verrouillent ou revérifient génération/empreintes et réessaient pendant une incohérence ; ne pas prétendre à un échange atomique de toute la collection.

L’annulation arrête seulement descendants de construction et transferts de la tâche, ne publie aucun candidat inachevé ni ne remplace les artefacts formels, conserve les diagnostics nécessaires et restaure les réglages temporaires. Avant reprise, vérifier source, configuration, outils et empreintes ; ne pas réutiliser une validation périmée.

Nettoyer selon la fermeture de tous les index et dépendances de manifestes. Supprimer par défaut les anciennes versions sans référence, en gardant celles utilisées par autre cible, Cpp ou extension GUI. Retirer un paquet commun seulement après sa dernière référence. Fichiers inconnus/non gérés ou vérification de chemin échouée arrêtent le nettoyage ; âge et nom ne prouvent pas la propriété. Supprimer uniquement des répertoires temporaires dédiés vérifiés, jamais sources, fichiers utilisateur ou dépendances externes.

Avant suppression, vérifier propriété, protection des sources, processus actifs et outils réutilisables. Retirer anciens artefacts Release et candidats/caches obsolètes, conserver les diagnostics nécessaires sans sauvegarde des anciens paquets, ni maintien de l’ancienne collection par ses dépendances. Reconstruire, valider et publier depuis un staging propre, avec les versions produit définies par le registre. Les versions valides référencées dans une nouvelle collection restent protégées par fermeture ; ce n’est pas une compatibilité avec l’ancienne norme.

## Conditions d’implémentation

Fournir planification partagée avec producteurs, références/versions communes, validation des catégories/métadonnées, tests de dépendances/verrous/restauration/nettoyage entre périmètres, refus des sources polluées et anciens formats, capacités/preuves cibles, reconstruction propre et récupération. Avant conformité des producteurs et vérificateurs, aucune nouvelle publication ni certification par ancien planificateur.

Une modification de norme seule ne lance aucune compilation, génération d’installateur ou distribution. Synchroniser six langues, navigation et guides d’implémentation, puis exécuter les contrôles documentaires temporaires sans publier dans Release :

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```
