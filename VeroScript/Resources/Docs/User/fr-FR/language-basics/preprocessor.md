# Préprocesseur

Le préprocesseur s'exécute avant l'analyse lexicale et sémantique. Il prend en charge la compilation conditionnelle, les macros de type objet, les macros de type fonction, la stringification, le collage de jetons et l'inclusion de modules.

## Macros de type objet

Une macro de type objet remplace un nom par une séquence de jetons prédéfinie :

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

L'expansion des macros se produit avant que le compilateur n'effectue la vérification du type. Une macro n'a pas de type statique VeroScript qui lui est propre ; l'expression développée doit toujours satisfaire aux règles de syntaxe et de type ordinaires.

## Macros de type fonction

Une macro de type fonction accepte des arguments. Mettez les arguments entre parenthèses dans les expressions composées afin que l'expansion ne modifie pas leur priorité :

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

Utilisez des macros pour une simple substitution de jetons. Préférez une fonction ou une fonction générique lorsque la vérification de type, les surcharges ou le comportement de débogage sont importants.

## Stringification et collage de jetons

Dans une macro de type fonction, `#` convertit un argument en texte et `##` colle les jetons adjacents en un seul jeton :

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

La stringification et le collage de jetons se produisent lors de l'expansion de la macro. Le résultat collé doit toujours être un jeton VeroScript valide.

## Macros variadiques

Une macro de type fonction peut utiliser `...` pour accepter des arguments supplémentaires et les développer via `__VA_ARGS__` :

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

L'implémentation actuelle prend en charge `##__VA_ARGS__` pour supprimer la virgule supplémentaire lorsqu'aucun argument variadique n'est fourni. Il prend également en charge `__VA_OPT__(tokens)` : les jetons entre parenthèses sont émis lorsque les arguments variadiques ne sont pas vides et omis dans le cas contraire :

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__` n'est valide que dans la liste de remplacement d'une macro variadique et doit être suivi d'une paire de parenthèses équilibrées.

## Macros intégrées dynamiques

Le compilateur fournit également deux macros générées à partir du contexte actuel du module source :

| Macros | Agrandissement |
| --- | --- |
| `__FILE__` | Le chemin logique normalisé du module actuel ; `<memory>` pour le texte source sans chemin. |
| `__LINE__` | Ligne source physique contenant l'appel de macro, commençant à `1`. |

Elles sont calculées dans le contexte de prétraitement de chaque module plutôt que d'être des macros globales ordinaires :

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`, `__LINE__`, `__VA_ARGS__` et `__VA_OPT__` sont des noms réservés. Un script, une définition initiale de ligne de commande, une macro prédéfinie d'hôte ou un paramètre de macro de fonction ne peuvent pas les redéfinir.

## Compilation conditionnelle

Utilisez `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` et `#endif` pour sélectionner la source qui participera à une compilation ultérieure :

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

L'ensemble complet des directives source est `#include`, `#define`, `#define_export`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif` et `#error`. Dans une condition, `defined(NAME)` et `defined NAME` testent l'existence d'une macro. Les macros sont d'abord développées ; tout identificateur restant indéfini vaut zéro.

Une expression `#if` utilise des constantes entières 64 bits (décimales, octales avec zéro initial à la manière de C, ou hexadécimales), les parenthèses, les opérateurs unaires `+ - ! ~`, arithmétiques `* / % + -`, de décalage, de comparaison, d'égalité, binaires `& ^ |`, les courts-circuits `&& ||` et `?:`. Elle n'utilise pas les préfixes binaires ni les séparateurs de chiffres des littéraux du script. Une division par zéro ou un dépassement dans une branche évaluée est une erreur ; une branche court-circuitée n'est pas évaluée.

Dans une branche active, `#error message` arrête le prétraitement et conserve le message élagué dans le diagnostic ; une branche inactive l'ignore. Le message n'est pas développé comme une macro. `#warning`, `#line` et `#pragma` (dont `#pragma once`) ne sont pas pris en charge et sont signalés par leur nom.

L'outil de ligne de commande peut fournir une macro objet simple avec `-D` ou `--define`. Un fichier source peut toujours définir les macros fonctionnelles et variadiques décrites plus haut :

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## Inclure un module

`#include` est un alias de `export import` : il importe et réexporte les déclarations et macros publiques, sans inclusion textuelle.

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

Utilisez `import` et `export import` pour exprimer les dépendances et les réexportations de modules. Ne traitez pas `#include` comme un mécanisme d’inclusion de texte de style C++.

## Macros publiques

<a id="exported-macro-contract"></a>

Utilisez `#define_export` avec la même syntaxe objet, fonction et variadique que `#define`. Les définitions ordinaires restent privées. Un `import` explicite rend les macros publiques finales disponibles immédiatement après la chaîne du chemin ; `export import` les réexporte aussi. `#include` est un alias de `export import` pour les déclarations et macros publiques. Cela vaut aussi pour les bibliothèques `.vslib`. Les imports implicites de l’hôte rendent aussi les macros publiques finales disponibles dès le début du code consommateur, sans les réexporter. Les fournisseurs et leurs dépendances explicites établissent leurs interfaces en premier ; les dépendances de macros entre fournisseurs utilisent un `import` ordinaire. Les macros privées restent privées et les conflits suivent les règles normales. Par exemple, `veroscript --implicit-import settings.vs main.vs` permet de les utiliser dans `main.vs` sans y écrire d’import.

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

Le remplacement est développé dans l’environnement de l’appelant, sans capturer les macros auxiliaires privées. Exportez explicitement les auxiliaires nécessaires. `__FILE__` et `__LINE__` désignent l’appel. Des définitions originales différentes portant le même nom sont en conflit, même si leurs corps sont identiques. Utilisez `#undef` avant de remplacer une définition importée. Réimporter la même définition est idempotent et la rétablit après `#undef`. Une redéfinition avec `#define` devient privée ; `#undef` la retire de l’interface publique.

Les cycles sans définition de macro publique restent acceptés. Si la fermeture des sources cycliques contient un `#define_export`, même inactif, l’interface ne peut pas être déterminée et le chargement est refusé. Supprimez le cycle d’imports. Les exports conditionnels des bibliothèques sont fixés à la construction ; les paquets applicatifs ne contiennent pas d’interface de macros publique.

<a id="preprocess-resource-limits"></a>

## Limites de ressources du prétraitement

Par défaut, le prétraitement limite le texte développé de chaque fichier, y compris les remplacements intermédiaires, à 32 MiB. Une compilation partage un budget de 268,435,456 unités internes entre ses fichiers. Dépasser une limite produit une erreur de prétraitement et refuse la compilation. Ces unités ne sont ni des instructions VM ni une garantie de durée. Un hôte natif peut fournir `FPreprocessContext::Budget` avec un `FPreprocessBudget` ; zéro est une limite nulle, pas une valeur illimitée. Ces limites sont distinctes des budgets d’exécution des extensions de compilation.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/preprocessor.md) · [fr-FR](preprocessor.md) · [ja-JP](../../ja-JP/language-basics/preprocessor.md) · [ko-KR](../../ko-KR/language-basics/preprocessor.md) · [zh-CN](../../zh-CN/language-basics/preprocessor.md) · [zh-TW](../../zh-TW/language-basics/preprocessor.md)
