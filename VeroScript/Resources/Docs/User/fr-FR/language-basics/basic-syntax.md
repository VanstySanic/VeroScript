# Syntaxe de base

Cette page décrit la structure de base d'un fichier source VeroScript, y compris les instructions, les blocs, les identifiants et les mots-clés.

## Fichiers sources

Les fichiers source VeroScript utilisent l'extension `.vs`. Un fichier source peut contenir des variables globales, des fonctions, des structures, des classes, des interfaces, des énumérations, des espaces de noms et des déclarations de module.

Un script d'application fournit normalement un point d'entrée `main` ; un module de bibliothèque peut exporter des types et des fonctions sans définir de fonction d'entrée. `main` peut renvoyer `int` ou `void` ; `int` est recommandé lorsque le code de sortie du processus doit être explicite.

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## Style de source recommandé

Les extraits VeroScript de la documentation utilisent une tabulation par niveau d'indentation, des accolades ouvrantes sur leur propre ligne, une instruction ordinaire par ligne et un point-virgule final après les déclarations `class`, `struct`, `interface` et `enum`. Le langage accepte certaines formes concises sans le point-virgule final, mais le code documenté devrait préférer la forme complète. Les fichiers d'exemple autonomes suivent le style du formateur décrit dans le README des exemples.

## Déclarations et points-virgules

Les instructions ordinaires se terminent par un point-virgule :

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

Les déclarations de fonction, les déclarations de type et les instructions de flux de contrôle utilisent des accolades pour les instructions composées. Les espaces et les sauts de ligne ne modifient normalement pas le sens d’une instruction.

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## Identifiants

Les identifiants nomment les variables, les fonctions, les types, les champs, les espaces de noms et les membres du module. Un identifiant peut contenir des lettres ASCII, des chiffres et des traits de soulignement, mais ne peut pas commencer par un chiffre :

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

Les identifiants sont sensibles à la casse. `item_count`, `ItemCount` et `itemCount` sont des noms différents.

## Mots clés

Les mots-clés ont des significations définies par la langue et ne peuvent pas être utilisés comme identifiants ordinaires. L'ensemble de mots-clés comprend :

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

L'ensemble de mots-clés fait partie de la grammaire du langage. Un nouveau mot-clé peut rendre indisponible un identifiant par ailleurs valide, le code de la bibliothèque doit donc éviter d'utiliser des mots-clés comme noms publics.

`do` est désormais réservé à `do ... while` ; un ancien source qui utilisait `do` comme identificateur doit le renommer.

## Groupes de déclarations de même type et chaînes brutes

Les déclarations globales, d'espace de noms et locales peuvent introduire plusieurs variables lorsqu'elles partagent un type complet initial et les mêmes qualificatifs `const` / `ref` valides à cet emplacement. Les initialiseurs sont évalués et les noms deviennent visibles de gauche à droite :

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

Chaque élément conserve son propre initialiseur et les vérifications habituelles de déclaration. En particulier, `auto first = 1, second = 2;` est volontairement refusé, et un suffixe de tableau après un nom ne permet pas de déclarer des formes différentes ; placez une dimension de tableau partagée dans le préfixe du type.

Les chaînes brutes UTF-8 utilisent la forme fixe `r"..."`. Elles conservent les barres obliques inverses et les retours à la ligne sans traiter les échappements :

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

Le premier `"` suivant ferme le littéral ; le texte brut ne peut donc pas contenir de guillemet. Il n'existe ni chaînes brutes larges, ni délimiteurs personnalisés, ni interpolation de chaîne.

## Limites syntaxiques volontaires

VeroScript refuse l'instruction vide isolée `;` ; utilisez `{}` pour un corps volontairement vide. Il ne prend pas en charge les expressions virgule générales, les arguments nommés comme `f(value: 1)`, les types de retour `auto` déduits, les liaisons structurées ni les opérateurs de disposition C++ comme `sizeof` et `alignof`. Utilisez les groupes uniquement pour les déclarations de même type, des retours explicites et les API du langage ou des conteneurs.

## Limites de sécurité de l'analyseur

L'analyseur accepte au plus 256 niveaux d'imbrication pour les expressions, les
expressions unaires et les expressions de préprocesseur `#if`. Les instructions
sont limitées à 32 niveaux et une chaîne d'expressions à 256 maillons. Au-delà,
la compilation échoue avec un diagnostic indiquant que l'expression ou
l'instruction est trop imbriquée, que l'expression `#if` est trop profonde ou
que la chaîne d'expressions est trop longue. Simplifiez ou découpez les sorties
générées très profondes.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/basic-syntax.md) · [fr-FR](basic-syntax.md) · [ja-JP](../../ja-JP/language-basics/basic-syntax.md) · [ko-KR](../../ko-KR/language-basics/basic-syntax.md) · [zh-CN](../../zh-CN/language-basics/basic-syntax.md) · [zh-TW](../../zh-TW/language-basics/basic-syntax.md)
