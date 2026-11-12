# Lambdas et valeurs de fonction

Un lambda est une valeur de fonction qui peut être stockée, transmise et invoquée. Les types de fonctions utilisent l’orthographe `function` ; `fn` est son alias compact.

## Types de fonctions

Un type de fonction combine sa liste de paramètres et son type de retour. Conservez des espaces autour de la flèche dans le code public pour que les déclarations plus longues restent lisibles :

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

Les valeurs de fonction peuvent être des paramètres, des valeurs de retour ou des variables locales :

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int` et `function(int) -> int` sont équivalents. La documentation publique et les déclarations plus longues utilisent généralement l'orthographe complète `function`.

## Expressions lambda

Un lambda peut utiliser soit un corps d'expression, soit un corps de bloc :

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

Appelez une valeur de fonction avec la même syntaxe de parenthèses que celle utilisée pour une fonction ordinaire :

```VeroScript
int a = increment(41);
int b = square(5);
```

Lorsqu'un type de fonction a un paramètre de référence, l'appel doit utiliser un marqueur de référence explicite. Les exemples publics préfèrent `T&` et `&expression` ; `ref T` et `ref expression` restent les orthographes prises en charge :

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## Captures de valeur

Un lambda peut capturer des variables locales à partir d'une portée englobante. Une capture de valeur de requête lambda et `[=]` ordinaire ; une liste explicite capture uniquement les noms répertoriés :

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

Les captures par valeur sont initialisées à la création et sont en lecture seule par défaut. Ajoutez `mutable` après les paramètres pour modifier les captures possédées et conserver les changements entre appels. Copier une fonction partage son environnement ; réévaluer sa création produit un environnement indépendant. Une capture ordinaire conserve le const de sa source.

VM, Mixed, JIT et AOT avec Core partagent les services d’exécution. Les exports source C++/C# autonomes prennent en charge leurs captures par valeur et les statiques scalaires. Les captures initialisées par référence, les objets statiques complexes et les références aux statiques externes exigent Core. L’export Python refuse encore les lambdas et les statiques locales.

L’export C++ autonome exige aussi Core AOT si une lambda possède des captures ou un receveur et renvoie une référence : la référence native ne peut pas conserver le stockage de la fermeture après la libération de la valeur fonction.

Une liste de capture vide `[]` ne capture aucun local englobant, tandis que les noms globaux restent disponibles via une recherche de nom normale :

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## Captures de référence

VeroScript prend en charge les captures de référence. `[&]` capture les variables locales englobantes qu'il utilise par référence, et `[&name]` capture uniquement la variable nommée par référence. Une capture par défaut peut être combinée avec des captures de valeurs explicites :

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

Les formulaires de capture disponibles sont :

- `[&]` : capture des locaux englobants utilisés par référence.
- `[&name]` : capture `name` par référence.
- `[=]` ou un lambda `function(...)` ordinaire : capture utilisée en englobant les locaux par valeur.
- `[=, &name]` : capture par valeur par défaut, sauf `name`.
- `[&, name]` : capture par référence par défaut, sauf `name`.
- `[]` : ne capture aucun local englobant.

Un local capturé par référence est promu vers un stockage partagé sécurisé. Une fermeture peut donc survivre à la fonction qui l'a créée sans conserver une référence vers une case de pile devenue invalide ; la portée englobante, une autre fermeture sur la même liaison et la fermeture actuelle observent le même état mutable. Si la liaison englobante est const, la capture de référence conserve sa nature en lecture seule.

Les noms dans une liste de capture explicite doivent faire référence aux liaisons englobantes autorisées. Une capture ne peut pas répéter de manière redondante un formulaire déjà couvert par une capture par défaut. Une liste de capture vide ne peut pas accéder à un local englobant ; les globaux et les membres d'objet restent soumis à leur propre recherche de nom et à leur propre sémantique de gestion.

## Renvoyer un lambda à partir d'une fonction membre

Une fonction membre peut renvoyer une valeur de fonction qui capture l'état actuel de l'objet :

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

Ici, `value` est un membre d'objet plutôt qu'un local ordinaire hors de portée. La fermeture suit toujours la sémantique du handle de classe et de la durée de vie de l'objet ; la capture de référence modifie la manière dont une liaison est accessible, et non la question de savoir si un objet arbitraire est automatiquement copié en tant que valeur.

Si un lambda utilise directement le récepteur, celui-ci suit la sémantique de son
type. Un récepteur de classe est conservé comme handle partagé et observe les
modifications ultérieures de ses champs ; un récepteur de structure est capturé
comme un instantané de valeur. Les lambdas imbriqués conservent ou copient le
récepteur déjà capturé selon la même règle. Un lambda sans capture ne peut pas
utiliser `this`.

## Relation avec les références

Les règles d'appel pour les paramètres de référence, les retours de référence et les sources de référence sécurisées sont couvertes dans [Références et sémantique de référence](references.md). La capture de référence est une règle de clôture et ne peut pas être remplacée par une capture de valeur ordinaire.

Consultez l'entrée Lambda dans l'[index des exemples](../examples.md) pour un exemple complet.

## Captures initialisées, mutable et variables statiques locales

Avec `auto`, indiquez explicitement le type de retour de la Lambda, par exemple `() mutable -> void { ... }`. Un type de fonction attendu tel que `function() -> void` permet d’omettre la flèche de retour de la Lambda. La règle existante de typage contextuel reste applicable ; `mutable` ne déduit pas le type de retour à partir du corps.

Pour une constante statique locale, `static const int value = 42;` et `const static int value = 42;` sont équivalents. Préférez `static const` pour une écriture uniforme.

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

Les initialiseurs des captures sont évalués une seule fois, de gauche à droite, dans la portée externe ; les nouveaux noms ne sont visibles que dans le corps. `[&name = expression]` exige une lvalue conservable et préserve const. Les variables globales et statiques restent des accès directs ; une capture initialisée permet de prendre leur instantané.

Une variable statique locale est initialisée au premier passage et partagée entre les appels. Les fermetures issues de la même expression lambda partagent ses statiques. Après un échec correctement nettoyé, une nouvelle tentative est possible ; une initialisation récursive échoue. Les états, sessions et workers sont isolés ; effacer un état réinitialise ses statiques. Les références statiques ne sont pas prises en charge.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/lambdas.md) · [fr-FR](lambdas.md) · [ja-JP](../../ja-JP/language-basics/lambdas.md) · [ko-KR](../../ko-KR/language-basics/lambdas.md) · [zh-CN](../../zh-CN/language-basics/lambdas.md) · [zh-TW](../../zh-TW/language-basics/lambdas.md)
