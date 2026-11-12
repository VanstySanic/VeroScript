# Intégration mobile expérimentale

Les adaptateurs Android et iOS restent expérimentaux. Utilisez les artefacts SDK correspondant à votre cible et validez votre application dans ses conditions réelles de déploiement. `FSessionOptions` fournit un répertoire absolu de bibliothèque standard et un instantané des sources. Les chemins réels ou virtuels sont acceptés si chaque source virtuelle figure dans `Sources` ; gardez les mêmes labels absolus pour l’entrée et les imports relatifs. Le cache facultatif doit être réel, accessible en écriture et absolu ; vide, il est désactivé. Recréez le runner après une mise à jour des ressources.

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner` autorise une exécution à la fois et crée une session neuve à chaque appel ; les globaux ne persistent pas. Les appels Execute sont synchrones. Sur Android et Apple, l’exécution peut utiliser un worker interne ; les callbacks hôtes ne doivent supposer ni le thread appelant ni le thread UI. Construction et exécution peuvent lire ou compiler : utilisez un thread de travail. Un autre thread peut demander l’annulation tant que l’objet reste vivant. Attendez la fin des appels avant destruction. Création, appels ordinaires et destruction de `FSession` appartiennent au thread créateur ; l’hôte fournit au moins 8 Mio de pile native pour compiler et son annulation reste verrouillée. Les callbacks gèrent leurs propres droits et annulation. Celle-ci ne préempte ni compilation ni callback natif bloqué.

Lorsque le cache est activé, le contenu de l’entrée et des sources importées participe à son invalidation. Un succès réutilise le bytecode mais peut encore charger et analyser les ressources pour vérifier leur identité ; il ne supprime pas tout travail de démarrage. Un cache inutilisable ou corrompu revient à la compilation normale.

Valeurs par défaut : VM, 1 000 000 instructions, 100 ms, profondeur 256. Seule la sortie console est autorisée ; entrée, fichiers, environnement, changement de répertoire et tâches sont désactivés. Sortie/diagnostics partagent 1 Mio ; erreur limitée à 64 Kio. Sources et chaque entrée ont un budget de 16 Mio, au plus 4096 ressources et chemins de 4096 octets. Ce ne sont ni quota de tas VM ni sandbox complet. L’hôte charge explicitement les catalogues décompressés avec `vs_interop_locale_load_catalog_directory` et choisit la langue ; aucune découverte via environnement, cwd ou exécutable. Sans catalogue, les diagnostics restent anglais. iOS appareil/simulateur interdit JIT dynamique et émission native.

## Localisation mobile explicite

Les SDK fournissent six langues sans chargement implicite. Copiez l’arbre complet des catalogues vers un vrai dossier de l’application avant création/exécution ; un chemin asset APK/virtuel ne suffit pas. AAR Android : `veroscript/localization`, SDK iOS : `assets/localization`. Android fournit `VeroScriptEngine.loadCatalogDirectory(path)` et `setLanguage(tag)`, avec succès booléen ; iOS/C++ utilisent `vs_interop_locale_load_catalog_directory` et `vs_interop_locale_set_language`. Libérez les tampons erreur via `vs_interop_buffer_destroy`. Vérifiez les retours. La sélection est globale au processus, non par moteur, et un échec préserve la précédente. Sans catalogues, les messages inconnus restent anglais. Diagnostics et validation suivent la même langue ; la sortie script ne change pas. Une allocation échouée peut conserver le repli anglais. Les détails d’erreur du système peuvent suivre la langue de sa bibliothèque même lorsque le contexte de l’adaptateur est traduit.

## Quotas du stockage d’octets géré

Les valeurs par défaut sont 16 MiB par tampon et 32 MiB retenus. Les options mobiles Core exposent `MaxBufferBytes` et `MaxRetainedBytes` ; zéro signifie illimité. Ces quotas couvrent capacités possédées et octets externes, y compris le pic de croissance, pas tout le heap du VM. La politique limitée à la console désactive les adresses natives.

## Exécution native contrôlée et AOT statique

<a id="mobile-jit-selection"></a>

Le mode mobile reste VM par défaut. Android peut sélectionner explicitement un AAR `vs-asmjit` et un fichier/répertoire JIT. Conservez un seul `libc++_shared.so` compatible ; une application utilisant un chemin JIT doit extraire les bibliothèques natives (`jniLibs.useLegacyPackaging = true` dans l’application). iOS interdit le JIT dynamique sur appareil et simulateur.

<a id="mobile-core-aot"></a>

`CompileSourceToCoreAot` / `GenerateCoreAotSourceBundle` dans `VeroScriptCppCoreAot.h` produisent le C++ cible, les octets exacts `.vsapp` / `.vsbc` et un rapport JSON. Compilez avec le SDK cible, choisissez Mixed et exécutez le paquet associé. Les fonctions non couvertes et les initialisations de modules/champs utilisent VM ; le mode natif strict les refuse. Ce profil conserve la sémantique Core et diffère de l’export autonome.

API de plateforme : [Android](../../../../../VeroScriptAndroid/Resources/Docs/User/fr-FR/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/fr-FR/index.md#ios-native-selection).

Voir [Backend JIT optionnel — Exécution native contrôlée et AOT statique](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/fr-FR/index.md#controlled-native-execution).

Le SDK mobile complet fournit `aot/include` : déclarations C++ publiques des services Core et identité de compilation cible correspondante. Ajoutez ce répertoire avec les en-têtes Interop. Il contient uniquement des en-têtes ; liez un seul runtime Interop. Une installation limitée à l’AAR nécessite ce jeu d’en-têtes séparément.

Le choix de la VM est indépendant de `ExecutionBackend` et du JIT. `FSessionOptions.VmBackendPath` vide conserve la VM statique explicitement assemblée ; sinon, il faut un chemin absolu de bibliothèque native ou de répertoire, sans repli en cas d’erreur. Le constructeur Android complet accepte `vmBackendPath` ; iOS expose `VSMConfiguration.vmBackendPath` et conserve par défaut l’assemblage statique.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
