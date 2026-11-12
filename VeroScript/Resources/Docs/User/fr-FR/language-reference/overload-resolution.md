# Résolution de surcharge

Lorsque plusieurs fonctions, méthodes ou opérateurs partagent un nom dans une même portée, le compilateur sélectionne un candidat appelable dans le contexte d'appel.

## Fonctions candidates

La résolution des surcharges collecte d’abord les candidats par nom et formulaire d’appel, puis vérifie :

- si le nombre d'arguments correspond ;
- si les types d'arguments satisfont aux règles de conversion de paramètres ;
- si les marqueurs `ref`, `const ref` ou `&` correspondent à la déclaration ;
- si les arguments de type générique peuvent être déduits et si toutes les contraintes `where` sont valables ;
- si les arguments par défaut peuvent remplir les positions finales omises.

Le type de retour n'est normalement pas suffisant pour distinguer les surcharges de même nom :

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

Lorsque des arguments sont fournis, le compilateur sélectionne le meilleur candidat correspondant en utilisant ses types et ses conversions disponibles. Si aucun candidat ne correspond ou si plusieurs candidats correspondent de manière égale, la compilation génère un diagnostic.

Les conversions implicites sont prises en compte lors de la recherche des candidats, notamment les conversions numériques compatibles et les constructeurs de conversion à un seul paramètre qui ne sont pas marqués `explicit`. Un constructeur `explicit` n'est pas candidat pour une affectation ou une conversion d'argument ordinaire ; demandez-le avec une construction directe ou `cast<T>`. Le compilateur n'enchaîne pas plusieurs conversions définies par l'utilisateur.

## Paramètres de référence

Les marqueurs de référence font partie de la correspondance des paramètres et doivent être écrits sur le site d'appel :

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

N'omettez pas le marqueur de référence et n'utilisez pas une référence en lecture seule comme inscriptible. Les marqueurs explicites rendent les surcharges de références sans ambiguïté ; `ref T` et `ref expression` restent des orthographes de compatibilité prises en charge.

## Surcharges génériques

Un candidat générique n'est viable que lorsque ses arguments de type peuvent être déterminés, que ses contraintes sont valables et que ses paramètres peuvent accepter les arguments :

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

Fournissez des arguments de type explicites ou ajustez les paramètres lorsque l'inférence est ambiguë.

## Surcharges des opérateurs

Les surcharges d'opérateurs suivent les mêmes règles de correspondance des candidats. Un opérateur membre inclut l'objet actuel implicite, tandis qu'un opérateur non membre utilise des opérandes explicites ; ses types de paramètres et de retour doivent satisfaire aux règles d’implémentation de l’opérateur. Voir [Opérateurs](../language-basics/operators.md) pour les déclarations.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-reference/overload-resolution.md) · [fr-FR](overload-resolution.md) · [ja-JP](../../ja-JP/language-reference/overload-resolution.md) · [ko-KR](../../ko-KR/language-reference/overload-resolution.md) · [zh-CN](../../zh-CN/language-reference/overload-resolution.md) · [zh-TW](../../zh-TW/language-reference/overload-resolution.md)
