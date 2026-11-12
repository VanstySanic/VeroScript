# Créer des distributions

`pack` lit `veroscript-package.json` s’il existe, sinon `veroscript-project.json` avec les métadonnées `packageManager.distribution` (objet ou fichier JSON relatif au projet). L’archive générée contient un manifeste distinct `veroscript-package.json` et exclut la configuration du projet source. Les filtres doivent inclure ce manifeste et toutes les entrées déclarées.

## Manifeste et dépendances

Placez veroscript-package.json à la racine ZIP. Noms : 2–64 lettres minuscules/chiffres, tirets simples, sans noms réservés de périphériques. Versions major.minor.patch avec préversion facultative et sans métadonnées de build ; exigences exactes ou intersection de comparaisons séparées par des espaces. Le runtime valide aussi chemins, cibles, identités et graphe complet : le schéma seul ne suffit pas.

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

Les usages sont runtime/compile/development. --development sélectionne les dépendances de développement ; une optionnelle exige `packages.<name>.enabled=true`. Les préversions demandent une exigence explicite ou --prerelease. Un nom canonique n’a qu’une version et source par environnement. Seules les racines runtime applicables sont déployées ; une installation transitive n’accorde pas une autorisation directe à une autre déclaration.

## Ressources, entrées et textes

entries.modules publie les noms d’import ; resources nomme les groupes de ressources. Les chemins sont relatifs et portables, sans traversée, lien ni alias de périphérique. Une ressource facultative absente se distingue d’un fichier de reçu supprimé/modifié ; ne masquez pas les erreurs d’intégrité.

La racine de localisation contient des dossiers BCP 47 normalisés et une base JSON en-US. Les traductions peuvent omettre des clés, mais ni en ajouter, ni dupliquer une clé entre fichiers, ni changer formatId. Les chaînes sont littérales, accolades incluses ; un message formaté précise pattern et formatId=vero.named. Clés standard : package.displayName, package.description, install.before/install.after, `inputs.<name>.description` ; elles reçoivent package.name et package.version. Ces catalogues ne changent pas les diagnostics du script/compilateur.

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

Les entrées restent string, boolean, integer ou enum. `--input=name=<JSON>` fournit une valeur ordinaire ; --secret-env=name=ENV_NAME une chaîne sensible. Les secrets restent propres à l’opération et sont exclus des modèles déclaratifs. Priorité : `<package>@<version>.<input>`, puis `<package>.<input>`, puis nom seul. Le CLI est non interactif par défaut.

## Traitement d’installation et limites

copy/template écrit seulement dans le nouveau dossier generated. Un hook exige --allow-hooks et une fonction exportée non générique void entry(std::package::install_context). Seuls les services de ressources autorisées et d’écriture générée sont disponibles, sans fichiers/environnement/console/tâches/adresses/horloges ni enregistrement natif du paquet. Annulation et budgets VM sont coopératifs, pas une isolation du système.

Valeurs par défaut : 1 000 000 instructions et 2 000 ms par hook, 16 MiB par opération de service texte, 64 MiB de tampons, 65 536 octets d’affichage. Ce n’est pas une limite du heap entier. Les hooks reçoivent des entrées typées et un instantané d’affichage indépendant. Un processeur natif approuvé possède version et identité d’implémentation. Toute modification de configuration affectant la génération exige une réinstallation. uninstall --skip-hooks est une récupération explicite avec avertissement.

La compilation des hooks utilise `constexpr`, `if constexpr`, les génériques et les imports source/`.vslib` du Core. Chaque compilation partage un budget : 1 000 000 unités de travail, 64 appels imbriqués, 256 niveaux d’expression, 16 MiB de mémoire d’évaluation et 8 MiB de cache par défaut. L’hôte le configure avec `FLimits.HookConstantEvaluationLimits` ; seul `MaxCacheBytes` accepte zéro pour désactiver le cache. Prévalidation, compilation avant exécution et désinstallation utilisent la même politique. La compilation respecte le délai global et l’annulation de l’opération ; le délai VM de 2 000 ms concerne séparément l’exécution. Les limites numériques et de tampons restent des politiques d’exécution.

## Registres et publication

La CLI utilise https://veroscript.org/api par défaut si ni le projet ni le fichier de verrouillage ne définit la source officielle. --registry-api remplace cette adresse de repli ; packageManager.registries.official configure la source du projet. Les hôtes intégrés configurent toujours explicitement FClientOptions.OfficialApi ou une source de projet. HTTPS valide les certificats ; --allow-http est réservé au développement explicite. --credential-env lit la valeur Authorization, par exemple Bearer YOUR_TOKEN, sans le nom de champ Authorization:. Sans --credential-scope, les identifiants sont limités à --registry-api ou à l’API officielle intégrée ; seules les URL dans ce périmètre d’origine et de chemin reçoivent les identifiants. Définissez --credential-scope explicitement pour une API de projet ou un miroir hors de ce périmètre. Ne sauvegardez pas de secrets dans les verrous ou manifestes source.

Un miroir configuré exige trustedMirror=true ; --mirror autorise seulement la source officielle pour cette commande. allowOriginFallback est false par défaut. Les redirections hors origine ou périmètre retirent les en-têtes sensibles et ne dégradent pas HTTPS.

## Publier des plateformes supplémentaires

Utilisez publish pour créer une version ou lui ajouter des artefacts pour d’autres plateformes. Toutes les archives fournies doivent avoir les mêmes nom de paquet, version, dépendances et conflits. Leurs cibles de manifeste doivent être disjointes entre elles et avec les artefacts déjà publiés. Des contraintes différentes sur le système ou l’architecture peuvent distinguer les plateformes ; des versions minimales ou capacités différentes ne suffisent pas. Un artefact portable chevauche toutes les cibles.

Les ID, octets et métadonnées existants restent immuables. Un artefact identique est réutilisé sans nouvel envoi ; modifier les octets d’une cible existante exige une nouvelle version du paquet. Renommer l’archive ne change pas sa cible. Après un résultat réseau incertain, conservez les archives originales et le cache de publication, puis réessayez avec les mêmes fichiers. Le client vérifie le registre avant de confirmer la réussite.

Ne fournissez chaque archive qu’une fois par commande ; deux entrées ayant les mêmes octets sont rejetées même si leurs noms diffèrent.

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[Schéma de paquet](../../../Schemas/veroscript-package.schema.json) · [Schéma de localisation](../../../Schemas/veroscript-package-localization.schema.json) · [Exemple de manifeste](../../../Examples/Local/library/veroscript-package.json)

## Entrées natives et paquets CLI

Les entrées `entries.native` exigent `id`, `path`, `linkage` et `roles`. Le champ optionnel `componentVersion` vérifie la version du descripteur natif ; l’analyse des entrées bindings/compiler exige ce champ et `catalog`. Les rôles admis sont `bindings`, `compiler`, `provider` et `cli` ; leur ensemble doit correspondre au descripteur interrogé de la bibliothèque. Un paquet natif exige l’identité cible correspondante : OS, architecture, environnement, build Core et `abis.native-extension`. Reprenez les valeurs du `veroscript env --json` correspondant, pas de la seule version du paquet.

Une entrée `cli` enregistre commandes/options auprès du service hôte CLI. Après installation et activation dans l’environnement sélectionné, utilisez les noms enregistrés sans sélecteur de bibliothèque. Désactiver le paquet retire ses commandes de la découverte. Les entrées exclusivement CLI sont ignorées en analyse et hors des hôtes fournissant le service. Les rôles combinés bindings/compiler conservent leurs exigences de catalogue d’analyse et de version de composant. L’installation vérifie l’artefact immuable et ses métadonnées de chargement ordinaires.

[Utilisation des commandes](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md)

Les paquets installables officiels VeroScript utilisent le préfixe `vs-`, par exemple `vs-asmjit` ; son identifiant de backend JIT reste `asmjit`. Le registre officiel réserve ce préfixe aux éditeurs officiels autorisés. Un même préfixe dans une source locale ou privée ne prouve pas une origine officielle ; les vérifications de la source et des artefacts restent nécessaires.

Les ID natifs utilisent lettres ASCII minuscules et chiffres, séparés par des tirets simples, sans tiret initial ou final. Le schéma accepte 2–64 caractères ; le descripteur réserve 64 octets, NUL compris : limitez un module natif réel à 63 caractères. L’ID déclaré doit correspondre au descripteur et à l’ID attendu. `entries.backends[].module` désigne l’ID d’un fournisseur natif déclaré, pas un ID de service hôte.

## Distributions officielles des backends et de GUI

Les distributions JIT et les modules de base avec scripts fournissent des fichiers `.vspkg` installables à côté des archives portables ZIP/tar contenant un dossier de produit. Seule une archive avec `veroscript-package.json` à sa racine est installable. L’installation cible le projet courant par défaut ; `--path` accepte un chemin de projet relatif ou absolu et `--root` sélectionne un environnement Runtime/SDK. L’identifiant de backend JIT de `vs-asmjit` reste `asmjit`.

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

L’adaptateur GUI utilise le service hôte existant `veroscript.gui.tools`. Utilisez une CLI/Runtime compatible avec GUI ou un hôte intégrant explicitement GuiScript. L’installation n’intègre pas GUI dans Core seul. SDL3, RmlUi, Stb et FontBackend restent des fournisseurs installés et sélectionnés séparément. Pour une installation hors ligne, fournissez les archives des dépendances dans les dépendances du projet ou les arguments de commande ; les fichiers voisins ne deviennent pas automatiquement des sources de dépendances.

## Entrées de backend et sélections réservées

`entries.backends[]` exige `service` et `id`, selon la grammaire des ID de paquet (2–64 lettres ASCII minuscules/chiffres séparés par des tirets simples). `auto` et `none` sont réservés à la sélection et interdits comme ID de backend. Choisissez une seule forme : `module` référence une entrée native déclarée de rôle `provider`, sans `path` ni `contract` ; ou `path` et `contract` décrivent une bibliothèque VM/JIT dédiée, sans `module`, avec `service` et `contract` tous deux `vm` ou tous deux `jit`. Cible et ABI natives restent vérifiées. Dans `services` du projet, omettre `backend` hérite de la sélection automatique ; une valeur explicitement vide ou `none` désactive, et `auto` demande les candidats automatiques.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md) · [en-US](../en-US/authoring.md) · [fr-FR](authoring.md) · [ja-JP](../ja-JP/authoring.md) · [ko-KR](../ko-KR/authoring.md) · [zh-CN](../zh-CN/authoring.md) · [zh-TW](../zh-TW/authoring.md)
