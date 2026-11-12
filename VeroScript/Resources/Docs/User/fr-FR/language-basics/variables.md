# Variables

Une variable est une déclaration nommée avec un type qui stocke une valeur au moment de l'exécution. Les variables peuvent être déclarées au niveau du module, à l'intérieur de fonctions ou de blocs, et en tant que membres de structure ou de classe.

## Déclarations

Une déclaration de variable indique un type, un nom et une valeur initiale :

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

Les variables locales et au niveau du module doivent être initialisées ; `int count;` est invalide. Fournissez une valeur explicite, un appel de constructeur ou une initialisation par accolades. Les champs de structure ou de classe peuvent omettre l’initialiseur ; les règles de valeur par défaut des champs s’appliquent alors.

## Initialisation

VeroScript prend en charge les appels de constructeur et l'initialisation des accolades :

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

Les types définis par l'utilisateur peuvent être initialisés en appelant un constructeur :

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## Affectation

Après déclaration, une variable peut être modifiée avec une expression d'affectation :

```VeroScript
int value = 1;
value = 2;
value += 3;
```

Les deux côtés d'une affectation doivent satisfaire aux règles de conversion du langage. L'affectation ne modifie pas le type statique de la variable.

## Variables locales et globales

Une variable déclarée dans une fonction ou un bloc est locale. Une variable déclarée dans la portée du module est globale et accessible par les fonctions du même module :

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

L'accès à un module global à travers les modules est soumis à des règles d'importation et d'exportation. La portée décrit où un nom est valide ; la visibilité est également affectée par les limites des modules et les spécificateurs d'accès.

## Variable `auto`

`auto` déduit le type à partir de l'initialiseur :

```VeroScript
auto answer = 42;
auto text = "hello";
```

Une variable `auto` doit être initialisée. Une fois déduite, elle reste une variable typée statiquement et ne peut pas se voir attribuer un type différent ultérieurement.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/variables.md) · [fr-FR](variables.md) · [ja-JP](../../ja-JP/language-basics/variables.md) · [ko-KR](../../ko-KR/language-basics/variables.md) · [zh-CN](../../zh-CN/language-basics/variables.md) · [zh-TW](../../zh-TW/language-basics/variables.md)
