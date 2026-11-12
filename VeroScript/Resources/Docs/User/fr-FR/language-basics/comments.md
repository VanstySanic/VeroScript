# Commentaires

Les commentaires ajoutent des notes à un fichier source. VeroScript prend en charge les commentaires sur une seule ligne et les commentaires en bloc.

## Commentaires sur une seule ligne

Un commentaire sur une seule ligne commence par `//` et se termine à la fin de la ligne actuelle :

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## Commentaires en bloc

Un commentaire de bloc commence par `/*` et se termine par `*/`. Il peut s'étendre sur plusieurs lignes :

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

Les commentaires ne sont pas des instructions exécutables. Un commentaire de bloc non terminé provoque une erreur lexicale. L’hôte peut conserver tous les commentaires, seulement ceux de documentation, ou aucun ; les métadonnées source peuvent donc exposer commentaires et documentation. Une extension de compilation activée peut utiliser ces informations pour générer du code. La conservation des annotations est indépendante.

## Documentation source pour les outils d’édition

L’aide au survol de Tooling lit les commentaires contigus placés immédiatement avant une déclaration source. Les éléments de complétion des macros préprocesseur définies dans le source peuvent aussi porter leurs commentaires précédents ; la documentation de complétion hôte provient des métadonnées de binding. Utilisez `//` (y compris `///`) ou un bloc `/* ... */`, sans ligne vide entre le commentaire et la déclaration. Il s’agit d’une convention de Tooling ; pour le compilateur, ces commentaires restent des commentaires lexicaux ordinaires.

`@param nom texte` documente un paramètre. `@return texte` et `@returns texte` documentent la valeur renvoyée. L’aide au survol affiche ces tags avec des libellés issus du catalogue de langue actif. Les tags inconnus restent visibles comme texte ordinaire ; la complétion des macros conserve le commentaire tel qu'il est écrit.

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/comments.md) · [fr-FR](comments.md) · [ja-JP](../../ja-JP/language-basics/comments.md) · [ko-KR](../../ko-KR/language-basics/comments.md) · [zh-CN](../../zh-CN/language-basics/comments.md) · [zh-TW](../../zh-TW/language-basics/comments.md)
