# Mobile — Documentation développeur

Le dispatch UI et le cycle de vie appartiennent à l’adaptateur/hôte ; Core reste indépendant de la plateforme. Le mode embedded-only exclut CLI/tooling bureau et exporteurs. Interop statique appartient au graphe source ou à l’archive iOS fusionnée ; le paquet bureau reste partagé. Distinguez Android/Linux, iOS/macOS et appareil/simulateur. Une autorisation plateforme ne prouve ni backend construit/sélectionné ni validation appareil. Testez imports, cache corrompu/invalidation, plafonds, annulation et destruction.

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py` crée une sortie expérimentale séparée, refuse une destination existante et ne publie pas dans la collection bureau.

Les versions des outils et les validations propres aux plateformes sont maintenues dans [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/fr-FR/index.md) et [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/fr-FR/index.md).

[Documentation utilisateur](../../User/fr-FR/index.md)


## Ressources et localisation mobiles

<a id="mobile-localization"></a>

Le profil `mobile` assemble Core, Session C++, Interop et `Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization`, sans Tooling/UI bureau. Gradle copie ces catalogues dans l’AAR ; le packager vérifie inventaire et octets exacts. iOS copie vers `assets/localization`. L’application déploie/charge explicitement avant sélection ; n’ajoutez pas de découverte environnementale mobile. Les paquets mobiles contiennent les fichiers fonctionnels, les ressources d’exécution et les mentions légales. Ils excluent README, manuels et exemples ; la documentation User intégrée est disponible dans la distribution Resources séparée. Les chemins des ressources natives et les exigences de validation sur appareil restent inchangés.

`TranslateMessage` mobile délègue à l’ABI C publique. La propriété des tampons et les erreurs sont définies dans [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/abi-and-lifetime.md) ; l’encodage JNI est documenté dans [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/fr-FR/index.md).

Les empreintes d’implémentation Core et AOT cible incluent `Shared/Private/VeroScriptFloatParsing.h`, `VeroScriptFloatFormatting.h` et `VeroScriptJsonString.h`. Les distributions source Python conservent les entrées nécessaires de `Shared/Private`, dont `VeroScriptDarwinPaths.h` pour Application et le runtime de paquets. L’empreinte source Python Android couvre aussi Application et ses sources C. Ce sont des entrées de construction privées, pas des en-têtes SDK publics ni des preuves de validation sur appareil.

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack` vérifie la pile appelante sur Android/Apple. En dessous de 8 Mio, il crée un pthread de 8 Mio et attend sa fin ; sinon l’exécution reste sur le thread appelant. Le worker crée, exécute et détruit toute la FSession, pas seulement le parseur. Il capture les exceptions et les relance après join. Préservez l’effacement du pointeur de session active sous le mutex d’annulation avant destruction, puis l’état d’exécution externe à chaque sortie. Les appels directs FSession n’utilisent pas ce helper : l’hôte fournit leur pile. Ce budget n’augmente aucune limite de langage, profondeur d’appel ou exécution. Vérifiez entrée valide sur petite pile, diagnostics de limite, exceptions, annulation et réutilisation. La fixture Android couvre la récupération après imbrication ; les régressions Apple imposent une pile appelante de 512 Kio.

<a id="mobile-message-coverage"></a>

Maintenez le catalogue Mobile pour les erreurs C++ Session/runner, les validations AOT/Java Android et les erreurs de factory/backend iOS. C++ et Java passent par leurs entrées de traduction existantes ; JNI/NSError traduisent les exceptions à leur frontière. Pour std::system_error, traduisez le contexte du projet sans remplacer code/catégorie ; le suffixe système suit la langue de sa bibliothèque. Les échecs de ressources conservent le repli existant. Clés et paramètres doivent correspondre dans les six langues.

Les hôtes source Core/C++/C seuls peuvent désactiver les adaptateurs avec `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF`. `VEROSCRIPT_INTEROP_STATIC=ON` concerne les builds source/mobiles statiques, sans modifier la distribution Interop desktop partagée.

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
