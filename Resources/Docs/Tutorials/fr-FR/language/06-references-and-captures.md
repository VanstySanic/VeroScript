# 06 : Références, retours de référence et captures lambda

Ce didacticiel utilise plusieurs petits programmes exécutables pour expliquer la différence entre un alias d'une valeur existante et une fermeture partageant un état externe. Les références sont utiles pour modifier un objet ou un élément existant en place ; Les captures lambda déterminent comment une fermeture stocke une liaison englobante.

## Étape 1 : créer un alias de référence local

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias` n’est pas un deuxième entier. Il s'agit d'un alias de référence pour `score`, donc l'écriture via `alias` modifie `score`. Une référence ne peut pas être renvoyée vers un autre entier après initialisation ; utilisez une variable ordinaire lorsque vous avez besoin d'une valeur indépendante.

## Étape 2 : passer une référence à une fonction

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&` déclare un paramètre de référence accessible en écriture. L'appel utilise `&value` pour rendre explicite l'opération d'alias. Pour un accès en lecture seule, utilisez `const int&` ; cela évite une copie tout en empêchant la fonction de muter l'objet via cette référence.

## Étape 3 : renvoyer une référence inscriptible

Une référence peut également être une valeur de retour de fonction, mais l'objet renvoyé doit toujours exister une fois l'appel terminé :

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

Cela renvoie le `counter` global, afin que l'appelant puisse continuer à le modifier via le résultat. Ne renvoyez pas de référence à une variable locale ordinaire : cette variable locale a quitté sa portée au retour de la fonction. Les éléments conteneurs et les membres d'objet peuvent également être renvoyés par référence tant que leur propriétaire reste valide.

## Étape 4 : modifier un conteneur via une itération de référence

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)` fait de `value` un alias pour chaque élément du tableau, de sorte que les affectations dans la boucle sont réécrites dans le tableau. Sans `&`, la variable de boucle reçoit une valeur et ses modifications ne sont pas réécrites. Passer un élément par index utilise la même forme explicite, par exemple `add_one(&values[0])`.

## Étape 5 : laisser un lambda capturer l'état externe

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]` capture les éléments locaux englobants utilisés par référence. VeroScript favorise une liaison locale capturée vers un stockage partagé sécurisé. Ainsi, après le retour de `make_counter`, `next` peut toujours accéder et modifier en toute sécurité `running`.

Si une seule variable nécessite une mutation partagée, écrivez `[&running]`. Pour une stratégie mixte, utilisez `[=, &running]` afin que les autres variables soient capturées par valeur tandis que seul `running` est capturé par référence.

## Étape 6 : capturer le récepteur d'une fonction membre

La capture du récepteur suit la sémantique propre à son type :

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

La fermeture de classe conserve l'objet et affiche `2` ; la fermeture de
structure conserve un instantané de valeur et affiche `3`. Un lambda sans
capture `[]` ne peut pas accéder à `this`.

## Choisir entre eux

- Utilisez une valeur ordinaire ou une capture de valeur lorsque vous avez besoin d'une copie indépendante.
- Utiliser un paramètre de référence inscriptible lorsqu'une fonction doit modifier une valeur appartenant à son appelant.
- Utilisez une variable de plage de référence lorsqu'une boucle doit mettre à jour les éléments du conteneur en place.
- Utilisez une capture de référence lorsqu'une fermeture renvoyée doit continuer à partager un état externe mutable.
- Considérez `const T&` pour un accès en lecture seule sans copier un objet volumineux, tout en garantissant que l'objet référencé reste actif suffisamment longtemps.

## Exercices

1. Écrivez `swap_values(int& left, int& right)` et appelez-le avec `swap_values(&a, &b)`.
2. Utilisez une boucle de plage de référence sur `array<int>` pour multiplier chaque élément par `2`.
3. Ajoutez un `step` avec valeur capturée à `make_counter` et implémentez la stratégie `[=, &running]`.
4. Essayez de renvoyer une référence à un local ordinaire, lisez le diagnostic `--check` et modifiez-le pour renvoyer un membre global ou toujours valide.
5. Modifiez le champ de `Counter` après la création de la fermeture et comparez le résultat avec celui de `Snapshot`.

Voir [Références et sémantique de référence](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/references.md) et [Lambdas et valeurs de fonction](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/lambdas.md) pour les règles complètes. Les exemples exécutables sont [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs), [`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs) et [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs).

Suivant : [Modules, importations et préprocesseur](07-modules-and-preprocessor.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/language/06-references-and-captures.md) · [fr-FR](06-references-and-captures.md) · [ja-JP](../../ja-JP/language/06-references-and-captures.md) · [ko-KR](../../ko-KR/language/06-references-and-captures.md) · [zh-CN](../../zh-CN/language/06-references-and-captures.md) · [zh-TW](../../zh-TW/language/06-references-and-captures.md)
