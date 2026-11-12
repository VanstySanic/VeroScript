# Flux de contrôle

Les instructions de flux de contrôle déterminent l'ordre dans lequel les instructions s'exécutent. VeroScript prend en charge les conditions, les boucles, `switch`, les sauts et les retours de fonction.

## `if`

`if` sélectionne un bloc en fonction d'une condition. `else if` et `else` sont facultatifs :

```VeroScript
if (score >= 60)
{
	print("pass");
}
else if (score >= 0)
{
	print("fail");
}
else
{
	print("invalid score");
}
```

La condition doit être une expression pouvant être évaluée par les règles booléennes du langage. Les accolades sont recommandées même pour une seule instruction car elles rendent explicite la limite de la branche.

## `while`

`while` vérifie son état avant chaque itération :

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

Si la condition est initialement fausse, le corps de la boucle ne s’exécute pas.

## `do ... while`

`do` exécute son corps une première fois avant de tester la condition. Le point-virgule final est obligatoire :

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

La condition doit être de type `bool`. Dans le corps, `continue` passe au test de la condition et `break` quitte la boucle.

## `for`

Une instruction `for` traditionnelle contient un initialiseur, une condition et une expression d'itération :

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

Une variable déclarée dans l'initialiseur est limitée à l'instruction `for`. La condition et l'expression d'itération peuvent être omises, mais la boucle doit toujours avoir une condition de fin claire.

Les clauses d'initialisation et d'itération peuvent chacune contenir une liste d'expressions séparées par des virgules, évaluée de gauche à droite. Cette syntaxe est réservée aux clauses de `for` ; VeroScript ne fournit pas d'opérateur virgule général. L'initialisation peut aussi utiliser un groupe de déclarations de même type tel que `int index = 0, end = 3` ; les noms deviennent visibles de gauche à droite et restent limités à l'instruction `for`.

## Boucles basées sur une plage

Les boucles basées sur une plage utilisent `:` pour parcourir un tableau ou un conteneur :

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

Utilisez une variable de boucle de référence pour modifier les éléments en place :

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

Les conteneurs de cartes prennent également en charge les formulaires de boucle clé/valeur ; voir la référence du conteneur pour les formulaires exacts.

## `break` et `continue`

`break` quitte la boucle ou le `switch` englobant le plus proche. `continue` passe à l'itération suivante de la boucle englobante la plus proche ; un `switch` n'est pas une cible de `continue` :

```VeroScript
for (int value : values)
{
	if (value < 0)
	{
		continue;
	}
	if (value == 100)
	{
		break;
	}
	print(value);
}
```

## `switch`

`switch` sélectionne un `case` en fonction d'une expression. L'implémentation actuelle accepte les entiers, les énumérations, les chaînes et les chaînes larges comme valeurs de commutateur :

```VeroScript
switch (state)
{
case 0:
	{
		print("off");
		break;
	}
case 1:
	{
		print("on");
		break;
	}
default:
	{
		print("unknown");
		break;
	}
}
```

VeroScript n'autorise pas les reprises non marquées. Utilisez `fallthrough` explicitement lorsque l'exécution doit se poursuivre dans le prochain `case` :

```VeroScript
switch (value)
{
case 1:
	{
		print("one");
	}
	fallthrough;
case 2:
	{
		print("one or two");
		break;
	}
default:
	{
		break;
	}
}
```

`fallthrough;` doit être la dernière instruction directement rattachée au `case`. Si le contenu de la branche est regroupé entre accolades, placez cette instruction après le bloc, hors de tout bloc imbriqué, condition ou boucle.

## Exceptions

VeroScript distingue les exceptions du script des fautes du moteur et de
l'exécution. `throw expression;` lève toute valeur de script non `void`. Une
instruction `try` doit se trouver dans une fonction et comporter au moins un
gestionnaire `catch` ou un bloc `finally` :

```VeroScript
int main()
{
	try
	{
		throw "failed";
	}
	catch (const string& message)
	{
		print(message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

Les clauses `catch` sont examinées dans l'ordre du code source. Un gestionnaire
typé peut s'écrire `catch (Type name)`, `catch (const Type& name)`,
`catch (Type& name)` ou `catch (ref Type name)`. Le nom peut être omis, comme
dans `catch (Type)`. `catch (...)` accepte toute exception de script, ne crée
aucune liaison et doit être la dernière clause `catch`. Un gestionnaire typé
correspond au type de la valeur levée ; pour une classe ou une interface, un
objet dérivé ou assignable est également accepté. Aucune conversion numérique
n'est effectuée pour faire correspondre un gestionnaire.

Une liaison par valeur est une copie. Une liaison par référence observe la
valeur originale ; une référence modifiable peut donc modifier la valeur qui
sera propagée. Un `throw;` sans expression n'est valide que dans un gestionnaire
`catch` et relance l'objet d'exception original, et non une copie par valeur
modifiée. Une valeur levée se propage à travers les appels jusqu'à trouver un
gestionnaire correspondant.

`finally` peut suivre les gestionnaires ou être utilisé sans `catch`. Il
s'exécute lorsque le contrôle quitte le `try` ou le `catch` protégé, notamment
après une exécution normale, `return`, un transfert de boucle, la propagation
d'une exception de script ou d'une faute d'exécution. Les transferts non locaux
hors de `finally`, tels que `return`, `goto` ou un `break`/`continue` visant une
boucle externe, sont rejetés par le compilateur.

Les boucles et les blocs `try`/`catch` imbriqués sont autorisés dans `finally`. Une exception capturée à cet endroit conserve le retour ou l’exception extérieure en attente. Une exception de script non capturée qui quitte `finally` remplace ce transfert et se propage vers un gestionnaire englobant. Ces règles s’appliquent aussi si le nettoyage appelle un destructeur qui exécute à nouveau du code de script.

Les fautes du moteur ou de l'exécution ne sont pas des valeurs de script et ne
peuvent pas être absorbées par `catch (...)`. Un `assert` qui échoue est une
erreur d'exécution de cette catégorie ; `finally` s'exécute néanmoins pendant
sa propagation.

## `goto` et étiquettes

Une étiquette est constituée d'un nom suivi de deux points. `goto` accède à cette étiquette :

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

Réservez `goto` pour le flux de contrôle local qui en profite réellement ; des sauts excessifs rendent une fonction plus difficile à analyser.

## `return`

`return` termine la fonction en cours. Une fonction non `void` doit renvoyer une valeur compatible avec son type déclaré. Une fonction `void` peut renvoyer sans expression :

```VeroScript
int square(int value)
{
	return value * value;
}

void log_done()
{
	print("done");
	return;
}
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/control-flow.md) · [fr-FR](control-flow.md) · [ja-JP](../../ja-JP/language-basics/control-flow.md) · [ko-KR](../../ko-KR/language-basics/control-flow.md) · [zh-CN](../../zh-CN/language-basics/control-flow.md) · [zh-TW](../../zh-TW/language-basics/control-flow.md)
