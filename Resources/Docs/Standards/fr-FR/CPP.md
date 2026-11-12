# Normes de code C++

Portée : [développement et contributions au projet VeroScript](README.md).

Ce fichier est la norme C++ actuelle faisant autorité. Sauf mention explicite **Recommandé** ou **Selon les besoins**, ses règles de mise en forme et de structure sont obligatoires.

## 1. Indentation, portées et accolades

- Utilisez une tabulation par niveau logique. Espaces de noms, types, méthodes, conditions, boucles, lambdas et portées locales ajoutent chacun un niveau.
- Toute accolade ouvrante occupe sa propre ligne et s’aligne sur sa déclaration ou instruction de contrôle :

  ```cpp
  void Process(const FRequest& Request)
  {
  	if (Request.bEnabled)
  	{
  		Execute(Request);
  	}
  }
  ```

- Aucune ligne vide n’est obligatoire immédiatement après l’ouverture ou avant la fermeture ; les lignes vides séparent seulement les blocs logiques.
- Les accolades fermantes s’alignent sur les ouvrantes. Placez `else`, `else if`, `catch` et le `while` final de `do` sur une nouvelle ligne alignée sur l’accolade fermante correspondante ; chaque accolade ouvrante de clause occupe aussi sa propre ligne :

  ```cpp
  if (bPrimary)
  {
  	HandlePrimary();
  }
  else
  {
  	HandleFallback();
  }
  ```

- Toute branche de contrôle dotée d’un corps utilise des accolades et une portée indépendante : `if`, `else`, `for`, `while`, `do`, `for` par plage et chaque `switch case`.
- La seule branche pouvant omettre les accolades est une **garde sur une ligne dont le corps consiste uniquement en un `return` direct** :

  ```cpp
  if (!bInitialized) return;
  ```

L’exception concerne seulement `return;` ou `return <expression>;`. Elle ne se combine pas avec une chaîne `else`, du nettoyage, de la journalisation, une affectation, des conditions de macro ou une logique imbriquée. `break`, `continue`, `throw`, `co_return` et les branches à plusieurs instructions exigent une portée avec accolades.

## 2. Disposition générale des types

Organisez le contenu dans cet ordre. N’émettez pas de blocs vides sans contenu.

1. Relations et alias tels que `friend`, `using` et `typedef`, selon les besoins, en tête ;
2. macros nécessaires telles que `DECLARE_XXX` ;
3. régions d’accès dans l’ordre fixe `public`, `protected`, `private` ;
4. méthodes `override` héritées en premier dans chaque région ;
5. types imbriqués ;
6. méthodes propres au type ;
7. variables membres en dernier.

Squelette recommandé :

```cpp
class FExample : public IBase, protected ISecondary
{
	friend class FExampleFactory;
	using FBase = IBase;

	DECLARE_EXAMPLE_TYPE(FExample)

public:
	//~ Begin IBase Interface
	virtual void Initialize() override;
	virtual void Shutdown() override;
	//~ End IBase Interface

	enum class EState
	{
		Idle,
		Running,
	};

	struct FOptions
	{
		bool bEnabled = false;
	};

	void Start(const FOptions& Options);

protected:
	//~ Begin ISecondary Interface
	virtual bool CanStart() const override;
	//~ End ISecondary Interface

	void NotifyStarted();

private:
	void ResetState();

	EState State = EState::Idle;
	FOptions Options;
};
```

### 2.1 Régions d’accès

- L’ordre `public`, `protected`, `private` est obligatoire.
- Gardez si possible un seul bloc continu par niveau d’accès. Répétez les spécificateurs seulement si le langage, une expansion de macro ou une frontière sémantique claire le nécessite, sans rompre l’ordre général.
- Placez les variables membres à la fin de leur région d’accès réelle ; ne mélangez pas données, méthodes et types imbriqués.

### 2.2 Méthodes héritées

- Dans chaque région, les méthodes `override` précèdent les nouvelles méthodes du type.
- Avec plusieurs bases, regroupez selon l’ordre gauche-droite de la liste d’héritage. Pour chaque base, respectez l’ordre des déclarations de son interface.
- Utilisez des marqueurs appariés pour plusieurs méthodes héritées, une base peu évidente ou une proximité confuse avec les méthodes propres au type :

  ```cpp
  //~ Begin ISerializable Interface
  virtual bool Serialize(FArchive& Archive) override;
  virtual bool Deserialize(FArchive& Archive) override;
  //~ End ISerializable Interface
  ```

- `Begin` et `End` doivent être appariés avec exactement le même nom de base directe ou d’interface. Les marqueurs peuvent être omis pour un unique `override` évident ; ne les ajoutez pas mécaniquement à chaque fonction.

### 2.3 Types imbriqués

Dans une région d’accès, la priorité fixe est :

1. `enum`, `enum class` ;
2. `struct` ;
3. autres déclarations de types, par exemple `union`, alias locaux ou objets fonctions ;
4. `class`.

- Séparez les groupes de types par une ligne vide. Au sein d’une catégorie, suivez les dépendances et un ordre de noms lisible.
- Ne laissez pas sans raison dans cette région les `using`/`typedef` ordinaires qui appartiennent en tête du type. Un alias dépendant d’un type imbriqué précédent peut occuper la position « autres déclarations ».
- Préférez les déclarations anticipées pour résoudre les dépendances de définition complète qui contredisent cet ordre. Si l’écart est inévitable, expliquez la dépendance de type complet à proximité.

## 3. En-têtes, déclarations anticipées et implémentations

### 3.1 En-têtes et implémentations selon les besoins

- Chaque `.h` C++ propre au projet commence par l’en-tête SPDX de copyright et de licence sur deux lignes, une ligne vide, `#pragma once`, une autre ligne vide, puis les includes ou déclarations :

  ```cpp
  // SPDX-FileCopyrightText: <copyright-holder>
  // SPDX-License-Identifier: MIT

  #pragma once

  #include <cstdint>
  ```

- Un `.inl` est un fragment d’implémentation inclus, pas un `.h`. Il commence aussi par l’en-tête SPDX de copyright et de licence sur deux lignes et une ligne vide, mais n’utilise par défaut ni `#pragma once` ni `.cpp` associé. Appliquez `#pragma once` comme à un `.h` seulement après avoir confirmé qu’il s’agit d’un contenu de type en-tête à inclure une seule fois par unité de traduction.
- Un `.h` n’exige pas de `.cpp` de même nom de base. Organisez un `.cpp` par responsabilité seulement si l’implémentation ou la construction nécessite une unité de traduction distincte ; ne créez pas de fichier vide pour le nombre de fichiers ou la symétrie des répertoires.
- Les en-têtes contenant seulement déclarations, définitions de types, constantes, templates ou définitions `inline` peuvent rester seuls. Conservez les définitions qui doivent être visibles dans les en-têtes au lieu de les déplacer pour séparer les fichiers.
- Si un `.cpp` est nécessaire, placez-le dans la zone d’implémentation du même module et incluez explicitement les en-têtes implémentés. Sans couches `Public/` et `Private/`, les fichiers partagent normalement un répertoire ; sinon, utilisez un sous-répertoire sémantique adapté de `Private/`, sans imposer les mêmes chemins que `Public/`.
- Un `.cpp` réservé à l’implémentation peut ne pas avoir de `.h` de même nom.
- À la suppression d’un `.cpp` vide existant, vérifiez ses rôles effectifs de construction, enregistrement, instanciation explicite et plateforme, et mettez à jour les références de construction ensemble.
- Les fichiers tiers et générés conservent leur organisation applicable.

### 3.2 Ordre des déclarations et définitions

- Les méthodes déclarées dans le `.h` et nécessitant une implémentation dans le `.cpp` y sont définies dans l’ordre de déclaration.
- Les groupes conservent aussi leur ordre : implémentations d’interfaces héritées et méthodes publiques/protégées/privées propres au type.
- Les auxiliaires d’espace de noms anonyme, constantes locales au fichier et types réservés à l’implémentation peuvent précéder les méthodes ; ils ne changent pas l’ordre relatif des définitions correspondant à l’en-tête.
- À l’ajout d’une déclaration, vérifiez sa position de définition au lieu d’ajouter systématiquement toute implémentation en fin de fichier.

### 3.3 Déclarations anticipées et plusieurs types de premier niveau

- Toutes les déclarations anticipées suivent `enum` → `struct` → autres déclarations → `class`.
- Une déclaration anticipée d’énumération utilise une forme opaque valide : `enum class EName;` ou un type sous-jacent explicite tel que `enum EName : std::uint8_t;`. Ne déclarez pas `enum EName;` sans type sous-jacent pour une énumération non limitée à une portée.
- Un en-tête peut définir plusieurs types de premier niveau. Quand les dépendances de type complet le permettent, appliquez le même ordre `enum` → `struct` → autres déclarations → `class`.
- Les « autres déclarations » comprennent `union`, les alias `using`/`typedef` et les alias de types fonctions. Les templates de classe suivent `class`, ceux de structure suivent `struct` ; ne classez pas tous les `template` parmi les « autres ».
- Si une dépendance de type complet impose un autre ordre, essayez d’abord les déclarations anticipées ; à défaut, privilégiez la dépendance et expliquez-la brièvement à proximité.

### 3.4 Valeurs initiales et champs de bits booléens

- Si un membre nécessite une valeur initiale et que son état par défaut s’exprime directement et clairement à la déclaration, donnez cette valeur à cet endroit.
- Les valeurs dépendant d’arguments du constructeur, d’un état d’exécution ou de l’initialisation d’autres membres vont dans la liste d’initialisation du constructeur. Ne créez pas de valeur fixe trompeuse pour satisfaire cette règle.
- Préférez un champ de 1 bit pour les indicateurs booléens adaptés au stockage compact, sans besoin d’adresse, de référence, d’opérations atomiques ou de disposition binaire externe stable. Exemples : `uint8 bEnabled : 1;` ou `std::uint8_t bEnabled : 1;`. La largeur est `: 1` ; `uint8` est le type de stockage.
- Le minimum actuel est C++17. Les champs de bits C++17 ne peuvent pas utiliser d’initialisation par défaut au sein de la classe. Initialisez les états non nuls ou explicitement requis dans la liste du constructeur. N’écrivez pas `uint8 bEnabled : 1 = true;` avant l’adoption d’un standard autorisant cette syntaxe.

## 4. `switch` et branches exécutables

- Tout `switch` contient `default`.
- Les étiquettes `case` et `default` sont au niveau du contenu de `switch`. Les accolades de leur corps sont indentées d’un niveau supplémentaire, et les instructions encore d’un niveau :

```cpp
switch (Kind)
{
case EKind::First:
	{
		HandleFirst();
		break;
	}
case EKind::Second:
	{
		HandleSecond();
		break;
	}
default:
	{
		HandleUnknown();
		break;
	}
}
```

- Un `case` dont le seul chemin termine directement la méthode avec `return` ou `return <expression>` peut utiliser l’exception de garde de la section 1. N’y mélangez pas journalisation, nettoyage, déclarations locales, conditions ou autres instructions. Tous les autres corps `case` et `default` utilisent une portée indépendante avec accolades.
- Un `default` sans action métier doit quand même conserver sa portée et un `break` explicite. Ajoutez une courte explication en anglais seulement si la raison de ne rien faire n’est pas évidente dans le contexte.
- Ne dépendez pas d’une poursuite implicite vers le cas suivant. Utilisez un marqueur explicite admis par le projet et expliquez la poursuite intentionnelle.
- Toutes les branches utilisent des accolades, hors l’exception stricte de garde avec `return` direct de la section 1.

## 5. Macros du préprocesseur

- Indentez `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` et `#endif` au **même niveau logique** que le code entouré. Une condition du préprocesseur n’ajoute pas de niveau.
- Le code entouré conserve ses niveaux et sa lisibilité si la condition est supprimée :

  ```cpp
  void Initialize()
  {
  	#if WITH_DIAGNOSTICS
  	RegisterDiagnostics();
  	#endif

  	StartRuntime();
  }
  ```

- Les macros conditionnelles doivent couvrir autant que possible des blocs sémantiques entiers ; ne découpez pas une déclaration ou une expression en fragments difficiles à lire.
- Les macros appariées ont la même indentation. Ajoutez selon les besoins un bref commentaire de condition à `#endif` pour les blocs longs.
- Disposez les déclarations produites par macro selon l’ordre des types défini ici.

## 6. Commentaires et régions

- Utilisez `//` pour une explication sur une ligne immédiatement au-dessus de la déclaration ou du bloc. Les courts commentaires de fin de ligne ne doivent pas nuire à la lisibilité.
- Utilisez `/** ... */` pour les sémantiques d’API multiligne, propriété, threads, échecs ou invariants complexes.
- Ne répétez pas un nom évident, par exemple `// Gets the name` avant `GetName()`.
- Pour un membre à expliquer, privilégiez unité, propriété, validité, contraintes de threads, invalidation du cache ou relations avec les autres champs.
- `//~ Begin <Name> Interface` / `//~ End <Name> Interface` servent uniquement aux blocs d’interfaces héritées, pas aux titres ordinaires de méthodes.
- Les autres grands groupes peuvent avoir de courts commentaires explicatifs, mais préférez la disposition fixe pour rendre la structure évidente.

## 7. Autres recommandations

- Aucun ordre ou regroupement d’includes n’est actuellement imposé dans les `.cpp` ou `.h`. Respectez lisibilité locale et contraintes de construction. Les déclarations anticipées suivent toujours la section 3.3.
- Utilisez `nullptr` pour les pointeurs nuls et des conversions explicites quand une information peut être perdue.
- Expliquez clairement dans les commentaires de type ou méthode la propriété des ressources, les objets potentiellement nuls et les rappels entre threads.
- Exprimez fonctions vides, implémentations par défaut et paramètres volontairement ignorés de la manière la plus courte et lisible. Ne créez pas de variables locales ou portées vides inutiles pour satisfaire une forme.

## 8. Sujets à préciser par le responsable

Aucune règle obligatoire n’existe encore pour les détails de nommage, templates/concepts, exceptions, pointeurs intelligents, frameworks de test, outils de formatage ou largeur de ligne. Le responsable les définira ensuite. L’ordre des includes est explicitement non imposé actuellement.
