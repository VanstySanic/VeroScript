# Installer et gérer les paquets d’extension

Une source explicite dans les dépendances racines du projet contraint aussi les dépendances transitives du même nom sans source explicite. Les contraintes de version et d’empreinte restent vérifiées ; des sources explicites différentes restent une erreur. Pour installer les protocoles hors ligne, déclarez d’abord l’archive locale `vs-network`, puis installez `vs-http` ou `vs-websocket`.

`veroscript-project.json` conserve les dépendances, l’activation et la configuration des paquets du projet. La distribution contient `veroscript-package.json` à sa racine ; le graphe de construction utilise `veroscript-build.json`. Chaque fichier est validé avec son format et son schéma.

Le gestionnaire optionnel installe bibliothèques de scripts, ressources et extensions natives dans un projet ou un Runtime/Core SDK explicite. Une distribution est un ZIP .vspkg ou .zip avec veroscript-package.json à la racine, distinct des .vslib, .vsapp et caches .vsbc.

## Choisir l’environnement

Les commandes utilisent le dossier de démarrage comme projet. --path choisit un autre projet ; --root le composant Runtime/Core SDK vérifié du CLI ; --root --path un autre composant vérifié. Les chemins relatifs partent du dossier de démarrage. Stockage : .veroscript/packages pour projet/SDK, Packages pour Runtime. Aucun périmètre utilisateur, fusion implicite ou installation automatique à l’exécution.

## Commandes

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install résout et installe ; --locked conserve le verrou, --offline interdit le réseau. Les artefacts distants doivent être intégralement en cache ; les archives locales doivent exister. Un instantané de dossier verrouillé peut survivre à la suppression de sa source. --upgrade renouvelle les versions et exclut --locked. list/show inspectent, verify contrôle l’intégrité, disable conserve fichiers et versions sans casser une dépendance active, enable réactive. clean ne supprime que les générations possédées, non référencées et sans bail actif.

pack ne remplace pas un fichier existant. Choisissez un nouveau chemin de sortie ou déplacez l’ancienne archive avant de générer sa remplaçante.

## Mettre à niveau, désinstaller et répéter une installation

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

La première commande réexamine la dépendance racine nommée ; la deuxième, le graphe complet. Il n’existe pas de sous-commande `update`. `--upgrade` respecte les contraintes de version et la source déclarées et exclut `--locked`. Une nouvelle dépendance de registre ajoutée sans version est fixée à la version exacte sélectionnée ; modifiez sa contrainte ou installez explicitement `name@version` pour dépasser cette fixation. Un dossier ou une archive locale conserve sa source locale. Les [exemples locaux](../../../Examples/README.md) exécutent ces deux commandes sans registre.

Sans `--upgrade`, l’installation préfère une version verrouillée encore valide et réutilise le contenu identique. Des exigences, sources ou contenus locaux modifiés peuvent nécessiter une autre résolution. Chaque installation répétée crée néanmoins une génération et réexécute les actions, processeurs approuvés et hooks autorisés. Les hooks exigent `--allow-hooks` à chaque invocation. `--locked` contraint le résultat du verrou et `--offline` interdit le réseau ; aucun ne supprime le traitement d’installation.

`uninstall` accepte les dépendances racines déclarées. Il retire leur exigence et élague les dépendances devenues inutiles aux autres racines, en conservant les dépendances partagées. Retirez ou modifiez une racine dépendante au lieu de désinstaller directement une dépendance seulement transitive. `clean` peut récupérer séparément les générations non référencées après libération des baux actifs.

## Exécuter et déployer

Exécution, vérification, eval, REPL, watch, paquets bytecode et AOT source pris en charge consomment l’environnement choisi. Sans sélecteur, les scripts découvrent un projet depuis l’entrée, eval/REPL depuis le démarrage, puis utilisent le composant courant. LSP/DAP/Interop nécessitent leur propre intégration et ne partagent pas automatiquement les sélecteurs CLI ; DAP refuse les sélections explicites non prises en charge. AOT exige toujours les ponts natifs du générateur.

deploy exige un nouveau dossier et copie la fermeture runtime active, les ressources, licences et données de chargement portables. Les dépendances exclusivement compile/development sont exclues ; fournissez séparément l’application/.vsapp. Une installation croisée vérifie et copie sans exécuter les bibliothèques cibles. Validez chaque plateforme séparément.

Pour exporter une cible étrangère installée, fournissez le descripteur complet utilisé à l’installation à `deploy --target <file>` ou appelez `FInstalledEnvironment::DeployForTarget`. L’export vérifie et copie les données de la fermeture. Le chargement pour exécution exige une cible correspondant à l’hôte.

## Configuration, erreurs et langue

`recovery-conflict` indique qu’un fichier de gestion ne correspond ni à l’ancienne ni à la nouvelle valeur du journal interrompu. Le fichier et `transaction.json` sont conservés ; un rollback bloqué peut signaler `recovery-required`. Sauvegardez le fichier et résolvez le conflit avant de réessayer. Ne supprimez pas simplement le journal.

--packages-dir, --cache-dir et --lockfile choisissent les emplacements. Ne déplacez pas une installation en changeant ce chemin : réinstallez ailleurs ou déployez. environment-changed préserve les modifications concurrentes : relisez puis recommencez. layout-conflict indique un chevauchement état/verrou/propriété ; restart-required une implémentation native vivante incompatible avec son remplacement.

--lang et --fallback-lang répétable choisissent les textes du paquet indépendamment de la langue du compilateur. Les diagnostics internes du gestionnaire ont actuellement un message anglais et un code stable ; branchez sur code. --json sépare résultats machine et messages. Les commandes sont absentes si le gestionnaire optionnel est désactivé.

[Créer des distributions](authoring.md) · [Consommer les environnements en C++](embedding.md)

## Installer une liste de dépendances

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

`-r FILE`, `-rFILE`, `--requirement FILE` et `--requirement=FILE` sont répétables, réservés à `install`, et peuvent accompagner des paquets individuels. Le nom du fichier est libre, sans extension `.txt` obligatoire. Les chemins de fichiers de la ligne de commande partent du répertoire de démarrage, indépendamment de `--path` / `--root`. Dans chaque liste, les inclusions et distributions locales sont relatives au répertoire du fichier qui les contient. Les chemins absolus, UTF-8 et les espaces sont acceptés ; mettez entre guillemets les chemins avec espaces ou caractères de commentaire. Les antislashs Windows entre guillemets restent littéraux ; les slashs sont également acceptés.

Exemple UTF-8, avec LF/CRLF et BOM UTF-8 facultatif :

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

Chaque ligne logique contient une dépendance ou une inclusion locale `-r` / `--requirement`. Les lignes vides et commentaires `#` sont ignorés ; un commentaire en fin de ligne commence après un espace, hors guillemets. Un antislash final non échappé joint la ligne suivante avec un espace, puis les commentaires sont retirés. Les occurrences d'un même paquet croisent leurs contraintes de version ; des sources ou paramètres explicites différents échouent. Les références directes nommées vérifient le nom réel du manifeste. Les chemins locaux sont recalculés relativement au projet sélectionné puis enregistrés dans `veroscript-project.json` ; les empreintes URL/archives suivent le traitement habituel. Les autres dépendances du projet sont conservées et les entrées explicites modifient leurs racines correspondantes. Le lot utilise une résolution et une transaction uniques. `--locked`, `--offline`, `--upgrade` et les permissions des hooks conservent leur sens. Aucun lot partiel n'est validé en cas d'échec ; une modification concurrente des listes invalide la validation en attente.

Les opérateurs sont `==` / `=`, `<`, `<=`, `>` et `>=` ; virgules ou espaces signifient ET. La syntaxe VeroScript `name@<contrainte>` et `name@*` reste disponible. Un nom seul conserve une dépendance existante ; une nouvelle racine de registre sans version est épinglée à la version choisie. Une source directe s'écrit avec des espaces `name @ <chemin-ou-URL>`. Un répertoire sans slash doit utiliser `./répertoire` ou des guillemets.

Ce format s'inspire de pip et utilise les noms et versions VeroScript. Les extras Python, marqueurs d'environnement, VCS/editable, fichiers de contraintes, `~=`, `!=` et options pip ne sont pas acceptés. Les inclusions sont locales ; aucune expansion de variables, `~`, échappements shell ou motifs glob. Configurez registres, miroirs, identifiants et permissions avec les options CLI/hôte existantes. Les cycles, UTF-8 invalide et erreurs de syntaxe indiquent fichier et ligne. Lecture, profondeur, nombres de fichiers/entrées, métadonnées cumulées, annulation et délais utilisent les limites existantes. La liste est explicite : `install` seul continue de lire `veroscript-project.json` sans rechercher de liste texte.

### Exemple hors ligne

Copiez le dossier `library` de l’[exemple local](../../../Examples/README.md) vers `workspace/vendor/library`. Créez les deux fichiers ci-dessous puis exécutez les commandes depuis `workspace`. Le résultat contient `example-math` ; les exigences sont enregistrées dans veroscript-project.json et le verrou de l’application.

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

Cycles d’inclusion, encodages invalides et dépassements de ressources sont refusés. Les diagnostics indiquent le fichier et la première ligne physique lorsque disponibles. `requirements-changed` signale une liste modifiée avant commit : relisez les fichiers puis réessayez.

Une entrée URL explicite avec `--locked` ou `--offline` réutilise une empreinte déjà enregistrée pour la même URL, ainsi que le même nom pour une référence nommée. Les octets du cache sont toujours vérifiés ; une URL inconnue ou modifiée ne peut pas emprunter l’identité en cache d’une autre URL. Une répétition hors ligne nécessite les dépendances de projet enregistrées et une archive complète et vérifiable en cache.

Une installation incomplète renvoie `recovery-required`, même avant la création du projet ou de l’état ; effectuer une opération explicite d’écriture de paquets pour terminer la récupération. Un journal `committed` valide et entièrement appliqué ne bloque pas la lecture. `combine-processing` indique que les sorties générées ou les chargeurs correspondent à d’autres dépendances ou paramètres : installer le graphe souhaité dans un même environnement. `pack` ne remplace aucune sortie existante, même créée simultanément.

Le stockage et les fichiers de gestion refusent liens symboliques et entrées spéciales. macOS accepte les alias système root `/tmp`, `/var` et `/etc` seulement vers leurs cibles `/private/...` attendues, pour les ancêtres d’un projet/cache temporaire ordinaire. Les règles de rejet des entrées d’archives et des liens sous le stockage choisi restent inchangées.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md) · [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
