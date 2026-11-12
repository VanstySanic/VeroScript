# Littéraux

Un littéral est une valeur écrite directement dans le code source. VeroScript prend en charge les littéraux entiers, à virgule flottante, caractère, chaîne, chaîne large, booléen et `nullptr`.

## Littéraux entiers

Un entier sans préfixe est décimal. `0x`/`0X` sélectionne l'hexadécimal et `0b`/`0B` le binaire :

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

Les suffixes peuvent rendre explicites la plage ou le signe :

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

Les suffixes entiers pris en charge incluent les formes insensibles à la casse de `u`, `l`, `ul`, `lu`, `i64` et `u64`. `l`/`L` sélectionne le `long` 64 bits de la langue ; `i64`/`u64` sélectionne directement un littéral 64 bits signé/non signé. Les littéraux sans suffixe et `u` utilisent le chemin 32 bits. Le littéral doit être représentable par son type cible.

Un trait de soulignement peut séparer deux chiffres valides pour la base courante, y compris dans la fraction et l'exposant : `1_000`, `0xFF_FF`, `0b1010_0101` et `1_2.5_0e1` sont valides. Il ne peut pas toucher un préfixe de base, un point décimal, le marqueur ou le signe d'exposant, un suffixe ou un autre trait de soulignement. Un zéro initial ne sélectionne pas l'octal ; `010` vaut dix en décimal.

## Littéraux à virgule flottante

Un nombre avec un point décimal ou un exposant est analysé comme un littéral à virgule flottante :

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

La partie entière peut être omise lorsqu'un point décimal est immédiatement suivi d'un chiffre décimal. `.5`, `.5f` et `.5e2` sont donc des littéraux à virgule flottante valides et représentent respectivement `0.5` en `double`, `0.5` en `float` et `50.0` en `double`. Un point `.` isolé reste une ponctuation d'accès à un membre et les suffixes entiers tels que `i64` ne s'appliquent pas à cette forme.

L'exposant utilise `e` ou `E` et peut inclure `+` ou `-`. Un suffixe `f` ou `F` identifie un littéral `float` ; un littéral sans suffixe est un `double`.

Les littéraux flottants décimaux utilisent `.` indépendamment de la locale hôte. Un suffixe `f`/`F` arrondit directement en binary32 ; sans suffixe, en binary64. Débordement et valeurs hors de la plage binary64 acceptée sont des erreurs de compilation. Une valeur représentable en `double` mais trop petite pour `float` peut devenir zéro, comme `1e-100f` ; cette règle des littéraux ne signifie pas que les API de conversion de texte acceptent le sous-dépassement vers zéro.

## Littéraux caractère

Un littéral caractère a le type statique `char`, l'entier signé 8 bits du langage :

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

Un caractère direct doit être un octet ASCII. `\xNN` écrit exactement un motif de 8 bits ; `\x80` à `\xFF` ont donc une valeur `char` négative. Les formes vides, multi-caractères, non ASCII, Unicode ou larges comme `L'A'` sont refusées.

## Chaînes et chaînes larges

Les chaînes ordinaires utilisent des guillemets doubles. Une chaîne large ajoute `L` avant le guillemet d'ouverture :

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

Les littéraux de chaîne prennent en charge les caractères d'échappement, y compris les sauts de ligne et les guillemets :

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| Échappement | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`, `\"`, `\'` | barre oblique ou guillemet échappé | idem | idem |
| `\0`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v` | un octet | une unité UTF-16 | un octet |
| `\xNN` | exactement un octet brut | `U+00NN` | un motif de 8 bits |
| `\uNNNN`, `\UNNNNNNNN` | scalaire Unicode encodé en UTF-8 | scalaire Unicode encodé en UTF-16 | non pris en charge |

Un échappement inconnu ou incomplet est une erreur de compilation. Les échappements Unicode refusent les substituts et les valeurs supérieures à `U+10FFFF`. `string` est une séquence d'octets avec longueur : les textes source et API utilisent UTF-8, mais `\0` et `\xNN` peuvent produire un NUL incorporé ou des octets arbitraires. Une valeur `string` n'est donc pas forcément un UTF-8 valide. `wstring` est une séquence d'unités UTF-16 avec longueur. Une conversion UTF-8/UTF-16 explicite signale une entrée invalide sans la remplacer silencieusement.

Les littéraux de chaîne ne peuvent pas s'étendre sur les lignes source. `string` et `wstring` sont de types différents et ne peuvent pas être mélangés directement ; utilisez une conversion explicite si nécessaire.

Les initialisations entre accolades des tableaux, maps, sets et types explicites peuvent contenir une virgule finale, par exemple `{1, 2,}` ou `{'A': 1,}`. Une initialisation vide reste `{}`. Une virgule initiale, une double virgule ou une virgule finale dans les arguments d'une fonction est refusée.

## Booléens et nuls

Les littéraux booléens sont `true` et `false` :

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr` représente un handle d’objet nul ou une valeur d’exécution nulle. Il est couramment utilisé avec des objets de classe et des valeurs d'exécution nullables :

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/literals.md) · [fr-FR](literals.md) · [ja-JP](../../ja-JP/language-basics/literals.md) · [ko-KR](../../ko-KR/language-basics/literals.md) · [zh-CN](../../zh-CN/language-basics/literals.md) · [zh-TW](../../zh-TW/language-basics/literals.md)
