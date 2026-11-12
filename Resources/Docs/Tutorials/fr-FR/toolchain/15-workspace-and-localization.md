# 15 : Configurer les options du projet et la localisation des diagnostics

Utilisez les options CLI pour choisir les chemins d’importation, les définitions et les arguments du script. Ces paramètres ne modifient pas la sémantique du langage VeroScript.

## Préparer les fichiers

Utilisez un Runtime apparié avec sa VM et ses ressources de localisation. Créez un répertoire modifiable contenant `main.vs` et les sous-répertoires `src`/`shared`. Enregistrez les trois fichiers ci-dessous et exécutez les commandes depuis ce répertoire. Chemins et définitions s’appliquent à chaque invocation ; ils sont répétés dans toutes les commandes qui vérifient ou exécutent `main.vs`.

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

Avec `FEATURE_SCORE=1`, l’exécution avec `demo 42` affiche `score=42, argc=2` et renvoie zéro. Sans définition, elle choisit `feature disabled`. Une vérification réussie renvoie zéro sans appeler `main`.

## Étape 1 : configurer la compilation

Passez les chemins avec `-I` et les définitions avec `-D`. Répétez ces options pour plusieurs valeurs. La CLI lit directement ses options.

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## Étape 2 : choisir le moteur d’exécution

Pour cette leçon, utilisez la VM fournie par Runtime, sans paquet JIT facultatif. La commande transmet `demo` et `42` à `main.vs` et conserve la sortie et le résultat de sortie habituels du script. Les autres stratégies et la sélection par environnement de modules figurent dans la [référence des moteurs CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md#cli-backend-policy).

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## Étape 3 : vérifiez avant d'exécuter

Vérifiez le point d’entrée avant l’exécution :

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)` reçoit des arguments après le chemin du script ; le chemin lui-même n'est pas dans `args`. Le code de sortie du processus CLI provient du résultat final de `int main`, `void main` ou `exit(n)`.

## Étape 4 : choisissez une langue de diagnostic

Utilisez `--lang` pour sélectionner une langue pour un appel CLI :

```powershell
veroscript --lang fr-FR --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

Vous pouvez également définir `VERO_LANG` pour la session PowerShell en cours :

L’exemple de variable d’environnement rétablit ensuite la valeur précédente de `VERO_LANG`.

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "fr-FR"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

Les langues sont découvertes dans les catalogues déployés. Une traduction manquante utilise l’anglais ; une valeur explicite `--lang` non prise en charge est une erreur d’argument.
Les diagnostics de déclaration du parseur, y compris les déclarations `static class` et `static struct` invalides, utilisent les mêmes identifiants stables et les mêmes catalogues dans la CLI et le LSP. Utilisez l'identifiant de diagnostic pour la logique des outils ; le message traduit est destiné à l'affichage.

## Étape 5 : fournir un répertoire de localisation du projet

Copiez l’arborescence `localization/` complète de la distribution dans le projet et conservez sa référence `en-US` complète. Le schéma ci-dessous montre le catalogue du langage ; gardez aussi les autres fichiers copiés.

```text
localization/
  en-US/
    language.json
    ...
  fr-FR/
    language.json
    ...
```

Sélectionnez explicitement cette arborescence avec `VERO_LOCALIZATION_DIR` : les catalogues placés à côté de l’exécutable sont prioritaires sur la découverte dans le répertoire de travail. L’exemple rétablit le réglage précédent après la commande.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang fr-FR --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US` définit la référence complète. Les autres répertoires BCP 47 canoniques qualifiés ne peuvent contenir que des fichiers et identifiants présents dans l’arborescence anglaise. Les fichiers ou clés traduits manquants utilisent l’anglais. Les fichiers doivent être des objets JSON UTF-8 plats à valeurs textuelles, sans clés dupliquées ni contenu final. Pour ajouter une langue, ajoutez son répertoire canonique et ses fichiers JSON à cette arborescence sélectionnée.

## Exercices

1. Retirez `-D FEATURE_SCORE=1` pour une exécution et observez `feature disabled` ; rétablissez-le puis confirmez `score=42, argc=2` avec `demo 42`.
2. Transmettez trois arguments à l'entrée et imprimez-les à partir de `main(string[] args)`.
3. Avec l’arborescence du projet sélectionnée, supprimez `diag.expected_expression` de `fr-FR/language.json`, exécutez la commande ci-dessous et vérifiez que « expected expression » apparaît en anglais.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang fr-FR -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[Référence CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/15-workspace-and-localization.md) · [fr-FR](15-workspace-and-localization.md) · [ja-JP](../../ja-JP/toolchain/15-workspace-and-localization.md) · [ko-KR](../../ko-KR/toolchain/15-workspace-and-localization.md) · [zh-CN](../../zh-CN/toolchain/15-workspace-and-localization.md) · [zh-TW](../../zh-TW/toolchain/15-workspace-and-localization.md)
