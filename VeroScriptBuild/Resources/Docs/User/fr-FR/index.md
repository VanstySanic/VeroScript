# Construire et distribuer une application

`--build-file <fichier>` sélectionne tout nom de configuration avec un chemin absolu ou relatif au répertoire de lancement. Sans `--path`, le parent du fichier devient la racine du projet ; avec `--path`, les entrées restent relatives à cette racine. Le nom par défaut est `veroscript-build.json`.

Le module optionnel `VeroScriptBuild` lit un fichier strict `veroscript-build.json`. Les chemins de configuration sont relatifs à la racine explicite du projet ; les chemins CLI sont relatifs au répertoire de lancement.

`--profile <nom>` sélectionne un profil déclaré dans `profiles` ; la valeur par défaut est `release`. `--target <nom>` sélectionne une cible et peut être répété. Sans sélection explicite, les `defaultTargets` configurées s’appliquent.

```sh
veroscript build --path /absolute/project --plan --json
veroscript build --path /absolute/project --build-file /absolute/config/release.json --plan --json
```

Ces commandes valident sans exécuter de code utilisateur. Supprimez `--plan` pour construire ; utilisez un nom ou `--target` pour choisir une cible. Installez et verrouillez les dépendances au préalable : la construction ne les télécharge pas.

Les cibles library, application, distribution, deployment, bundle, custom et group produisent respectivement des bibliothèques `.vslib`, applications `.vsapp`, distributions `.vspkg/.zip`, environnements déployés, assemblages, étapes personnalisées et groupes. Une référence utilise `{ "target": "library", "output": "main" }`, avec `path` pour un membre d’une arborescence source.

La validation applicative du build utilise la cible sélectionnée, notamment sa plateforme et son ABI, plutôt que l’identité de la machine de build. Une autre cible peut être préparée lorsque ses composants déclarés sont disponibles ; ses composants natifs doivent encore être validés et exécutés sur cette cible.

Runtime, environnement et lanceur sont indépendants et optionnels. Les applications source conservent les usages runtime et compile. Les fichiers uniques PE/ELF réutilisent le contenu ZIP ; les autres enveloppes utilisent un fournisseur personnalisé. Reçus, empreintes et baux protègent les sorties et leur publication atomique.

Les étapes script/processus exigent une autorisation explicite. Les scripts disposent de trois fonctions build pour lire les entrées déclarées, écrire dans le répertoire temporaire et journaliser, avec limites VM. Un processus natif ne constitue pas un bac à sable système. Les fichiers de clés restent hors des distributions.

Les applications sources sont validées à partir du contenu livré. Macros et options portables sont conservées ; utilisez files/modules et un environnement explicite, sans chemins de recherche de développement. Les surcharges d’éditeur doivent devenir des sources matérialisées. `Resources/Examples/generated` illustre un générateur. Les répertoires et exécutables autonomes utilisent une signature du contenu.

La recomposition d’une application signée exige une politique de signature explicite pour le nouveau contenu.

## Étapes, sorties et cache

`--allow-process` et `--allow-script` autorisent leurs étapes pendant l’exécution ; `--plan` n’exécute ni processeur, étape personnalisée, hook d’installation ni sous-processus. Une étape processus reçoit un exécutable et un tableau d’arguments sans shell. Les scripts disposent de `build_read_text(input, relative)`, `build_write_text(output, relative, text)` et `build_message(text)`. Les imports restent dans le répertoire du script, les entrées déclarées et les racines standard explicites. Les services standard de fichiers, environnement, console, tâches, adresses et non-déterminisme sont désactivés ; limites d’instructions, temps, profondeur, mémoire et nombres s’appliquent. Les fournisseurs/processus natifs restent des capacités de confiance, sans bac à sable OS.

Copiez `Resources/Examples/hello` dans un emplacement inscriptible. Les sorties sont sous `dist/<profile>/<target-identity>/<target>/` ; l’identité portable est `portable`. Les fichiers étrangers/modifiés ne sont pas remplacés. Chaque cible est publiée atomiquement sur son volume ; l’échec suivant n’annule pas les cibles indépendantes déjà publiées. Gardez les baux retournés pendant l’utilisation des artefacts. Le cache dépend de la configuration, des entrées figées, du contenu amont, des fournisseurs et des empreintes de sortie. Les étapes personnalisées doivent déclarer déterminisme et identité d’outillage ; processeurs dynamiques et compilation sensible à la confiance sont réexécutés prudemment. Toute modification d’entrée exige un nouveau plan.

Un hôte embarqué exécutant une étape `script` doit fournir explicitement une VM avec `FServices::Compile.ScriptCompileExtensions.VmBackendFactory` ou `VmBackendPath`, deux options exclusives. L’absence de VM produit `script-vm`. La commande de bureau `veroscript build` utilise la VM assemblée avec le produit, tout en conservant les contrôles de permissions, instructions, délai, annulation et mémoire. Le chemin et le contenu de la VM participent à la validation du cache ; une fabrique personnalisée exige une valeur stable de `FServices::Identity` pour réutiliser le cache des étapes script.

## Composants et protection

Les sources exigent l’usage `["runtime", "compile"]`, les applications compilées normalement `["runtime"]`. `bundle.runtime` copie uniquement les fichiers listés par le composant vérifié `veroscript-runtime.json`, jamais le SDK entier. Le lanceur exige un modèle précompilé compatible explicite, sans recherche PATH ni invocation de compilateur. Le modèle est figé lors de la planification ; sa modification invalide plan/cache. PE/ELF matérialise les composants natifs dans un cache vérifié ; Mach-O et autres enveloppes utilisent un fournisseur personnalisé. Des dépendances natives différentes exigent des sorties distinctes par plateforme/ABI.

Les références d’artefacts ne sont reconnues que dans les champs d’entrée déclarés ; les `options` fournisseur restent du JSON opaque. Un rapport d’entrée ne remplace pas son empreinte figée. Les racines import/stdlib de l’hôte sont résolues depuis le projet et précèdent celles de la cible. Une politique de protection vérifie la signature existante d’une ressource avant transformation. Le rechiffrement décode l’enveloppe une fois ; toute modification signée exige une nouvelle politique. La nouvelle signature remplace l’ancienne, une copie inchangée la conserve. Un échec interdit la publication. Utilisez `--crypto-library <absolute-library> --keyring <absolute-file>` ; les fichiers de clés restent hors des distributions. Le registre `publish` accepte les distributions installables, pas les conteneurs applicatifs. Consultez `Resources/Schemas/veroscript-build.schema.json` et le guide Application.

L’inventaire du Runtime Windows conserve les bibliothèques redistribuables MSVC préparées et leur notice. Les applications en répertoire ou ZIP avec Runtime intégré les placent aussi à côté du lanceur. Un lanceur en fichier unique reste soumis aux prérequis du chargeur natif : une compilation avec CRT dynamique nécessite le VC++ Redistributable correspondant sur la cible ; pour supprimer ce prérequis, compilez les composants natifs depuis les sources avec un profil CRT statique cohérent.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
