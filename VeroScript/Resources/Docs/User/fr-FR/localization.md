# Localisation diagnostique

Cette page décrit la langue des diagnostics et l’interface des outils. Les catalogues chargés choisissent le texte affiché sans changer la sémantique. Le texte applicatif utilise l’[API std::i18n](standard-library/i18n.md), distincte : modifier son contexte ne change pas la langue des diagnostics du processus.

La sélection de langue s’applique aux diagnostics présents dans les catalogues chargés. L’aide CLI et certains messages de validation des arguments restent actuellement en anglais ; les messages absents des catalogues conservent leur texte d’origine.

## Politique d'assistance

Les paquets standard fournissent `en-US`, `fr-FR`, `ja-JP`, `ko-KR`, `zh-CN` et `zh-TW`. Les langues disponibles dépendent des catalogues réellement chargés ; consultez `GetAvailableLanguageTags` ou l’API correspondante du SDK. Un répertoire de culture valide peut ajouter une langue. Distribuez-le avec votre application ou chargez explicitement sa racine de catalogues.

Les applications doivent utiliser des balises canoniques BCP 47 telles que `en-US`, `zh-CN` et `zh-TW`. Le runtime normalise également les formulaires courants du système d'exploitation tels que `ja_JP.UTF-8`, les balises avec `@modifier` et `C`/`POSIX` ; le formulaire résolu utilise des traits d'union, une langue minuscule, une écriture de casse de titre et des sous-balises de région alphabétique majuscule.

La normalisation retire les espaces en début et fin ainsi que les suffixes d’encodage et de modificateur avant de résoudre `C`/`POSIX` en `en-US` ; elle couvre aussi `C.UTF-8` et `POSIX.UTF-8@modifier`.

## Correspondance et solution de repli

La résolution linguistique se déroule comme suit :

1. correspondre exactement à une balise canonique déjà chargée ;
2. supprimez la dernière sous-balise à plusieurs reprises et essayez les balises parents ;
3. choisir un candidat déterministe issu de la même langue principale ;
4. Si aucun candidat n'existe, une API de sélection explicite signale un échec, tandis que l'initialisation revient à `en-US`.

L’écriture chinoise est conservée : `zh-Hant-HK` peut correspondre à `zh-TW` et `zh-Hans` à `zh-CN`. Une sous-balise explicite `Hans` ou `Hant` est prioritaire sur la déduction à partir de la région ; les sous-balises d’extension ou d’usage privé ne déterminent pas l’écriture. Avec les catalogues par défaut, `zh-Hans-TW` correspond à `zh-CN` et `zh-Hant-CN` à `zh-TW`. Une demande de chinois simplifié ou traditionnel ne sélectionne pas silencieusement l’écriture opposée. Si un fichier traduit ou une clé de message manque, le runtime utilise le texte `en-US` correspondant, à condition que cette entrée existe dans le catalogue anglais de référence.

Une fois `TrySetLanguageFromTag` réussi, la langue active est la balise canonique qui correspond réellement. Une sélection échouée laisse la langue actuelle inchangée. `GetAvailableLanguageTags` renvoie les langues actuellement chargées à partir des catalogues.

Un diagnostic absent de la référence anglaise conserve son texte original. Charger un catalogue traduit ne traduit pas la sortie du script ni les chaînes arbitraires de l’application.

## Sélection d'une langue

La CLI utilise `--lang` ; la variable d'environnement est `VERO_LANG` :

```powershell
veroscript --lang fr-FR --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

La CLI initialise l'environnement et les catalogues avant d'analyser les arguments de ligne de commande, donc `--lang` remplace `VERO_LANG`. `VERO_LANG` remplace la langue du système d'exploitation ; si l'une ou l'autre des valeurs ne peut pas être résolue, l'initialisation utilise `en-US`. Un `--lang` explicitement non pris en charge est signalé comme erreur d’argument.

Pour une session gérée par un éditeur, définissez la préférence BCP 47 dans
`initialize.locale` avant d'ouvrir les documents. `workspace/didChangeConfiguration`
peut modifier les réglages d'analyse, mais la langue doit être appliquée lors de
la prochaine initialisation afin que les documents ouverts utilisent le même
catalogue. `veroscript-lsp` utilise les mêmes catalogues `tooling.json` et
compiler que le CLI ; la logique doit utiliser les identifiants de diagnostic,
non les messages traduits.
Les diagnostics de déclaration du parseur, y compris les contraintes de `static
class` et `static struct`, utilisent les mêmes identifiants stables et le même
catalogue. La CLI et le LSP sélectionnent donc le même message localisé pour
une même erreur dans le source.

## Disposition du répertoire

Une racine de localisation doit contenir une ligne de base `en-US` complète. Tout autre répertoire immédiat est un répertoire de culture et son nom doit être une balise canonique qualifiée BCP 47. VeroScript nécessite actuellement un nom de culture qualifié avec trait d'union, donc un répertoire `zh`, `ja` ou `zh_cn` non canonique est rejeté.

La présentation du catalogue par défaut est :

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

L'arborescence peut ajouter plus de cultures ou diviser les fichiers JSON en sous-répertoires tout en conservant les mêmes règles :

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

Le chargeur découvre les fichiers `.json` de manière récursive et les fusionne par chemin relatif. Pour chaque culture traduite :

- chaque fichier traduit doit avoir le même chemin sous `en-US` ;
- un fichier traduit ne peut pas ajouter un identifiant de message absent de son homologue anglais ;
- un fichier traduit manquant ou une clé utilise le texte anglais ;
- les fichiers au sein d'une même culture ne peuvent pas définir d'ID de message en double ;
- un répertoire de culture ne peut pas être dupliqué, vide, non canonique ou non qualifié.

Chaque fichier JSON doit être un objet plat UTF-8 dont les clés et les valeurs sont des chaînes :

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

Le texte peut utiliser `{0}` à `{9}` pour recevoir des arguments de diagnostic. JSON doit être un objet complet sans clés en double, sans échappements non valides, sans UTF-8 non valide, sans contenu de fin ou sans valeurs autres que des chaînes.

## Chargement, remplacement et fusion

`VERO_LOCALIZATION_DIR` désigne une racine de localisation complète. Définissez-la avant de lancer l’outil ou d’initialiser le SDK. Sans arborescence explicite valide, le runtime essaie les ressources distribuées près de l’exécutable (`localization` et `../assets/localization`) avant celles du répertoire de travail. Pour sélectionner vos catalogues, utilisez cette variable ou appelez `LoadCatalogDirectory`. La recherche automatique ne s’exécute que tant qu’aucun catalogue n’est chargé.

`LoadCatalogDirectory` valide et remplace l'arborescence du catalogue complète à l'échelle du processus ; si le chargement échoue, l'arborescence précédemment chargée reste active. `MergeCatalogDirectory` requiert que son argument soit une arborescence de catalogue complète et valide, puis fusionne chaque culture dans l'ensemble actif : les ID de message existants sont conservés par défaut et remplacés uniquement lorsque le remplacement est explicitement activé. Les deux opérations utilisent l’état de localisation thread-safe à l’échelle du processus du noyau.

La racine d'une bibliothèque standard, la racine d'un paquet de bibliothèque ou le frère d'un fichier de module peuvent contenir un répertoire `localization/`. Le chargeur de module fusionne ces arborescences sans remplacer les identifiants existants, permettant à une bibliothèque d'ajouter ses propres messages sans modifier de manière inattendue le texte de l'hôte. Les superpositions de sources conservent leur chemin de module logique, de sorte que les catalogues frères restent détectables.

## Limites du SDK

- Un hôte C++ utilise directement l'API `VeroScript::Locale` ; la langue et l’état du catalogue s’appliquent à l’ensemble du processus dans le noyau.
- Interop expose la même stratégie via `vs_interop_locale_set_language`, `vs_interop_locale_get_language`, `vs_interop_locale_get_available_languages`, `vs_interop_locale_load_catalog_directory` et `vs_interop_locale_merge_catalog_directory`.
- Une version WebAssembly/WASM intègre l'arborescence `localization` complète au moment de la construction dans son système de fichiers virtuel ; `setLanguage` peut sélectionner uniquement les cultures portées par cette version, tandis que `language` et `availableLanguages` signalent la langue canonique actuelle et les catalogues disponibles.

Voir [Outil de ligne de commande](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) pour les options CLI et les variables d'environnement, [Intégration d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md) pour les hôtes C++, [SDK Interop](../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md) pour l'ABI C et [SDK WebAssembly/WASM](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md) pour la limite Web.

Les outils Python Frontend, BindGen, Transpiler et AOT sélectionnent les messages `python_tools.*` indépendamment du runtime natif. Leur option `--lang` prime sur `VERO_LANG` et la langue système. Ils normalisent les tags système, préservent l’écriture chinoise et cherchent une correspondance exacte, parente, puis déterministe de même langue, avec repli anglais. Cela ne modifie pas la langue globale du SDK natif. Voir [Frontend Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/frontend.md).


## Localisation mobile explicite

Le SDK natif Android AAR et le SDK iOS C ABI fournissent six langues et exigent un chargement explicite. Copiez l’arborescence complète dans un dossier réel géré par l’application avant d’exécuter les scripts ; les chemins d’assets APK ne sont pas des dossiers du système de fichiers. Les ressources sont dans `veroscript/localization` pour l’AAR Android et `assets/localization` pour le SDK iOS. Android expose `VeroScriptEngine.loadCatalogDirectory(path)` et `setLanguage(tag)`, qui renvoient un booléen. Le C ABI iOS expose `vs_interop_locale_load_catalog_directory` et `vs_interop_locale_set_language` ; libérez les buffers d’erreur avec `vs_interop_buffer_destroy`. Vérifiez chaque résultat.

En C# mobile, installez les ressources avec `VeroScriptMobileResources.InstallAsync`, appelez `VeroScriptRuntime.LoadCatalogDirectory`, puis affectez `VeroScriptRuntime.CurrentLanguage`. Python charge l’arborescence voisine `veroscript/localization` à l’import de `veroscript` ; conservez-la dans le paquet déployé, puis appelez `veroscript.set_language`. Voir [Intégration mobile C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/mobile.md) et [Intégration mobile Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/mobile.md).

La langue est commune au processus, et non propre à chaque moteur. Un échec de sélection conserve la langue courante. La sortie du script reste inchangée. Les messages absents des catalogues conservent leur texte original ; un échec d’allocation peut aussi conserver le texte anglais.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/localization.md) · [fr-FR](localization.md) · [ja-JP](../ja-JP/localization.md) · [ko-KR](../ko-KR/localization.md) · [zh-CN](../zh-CN/localization.md) · [zh-TW](../zh-TW/localization.md)
