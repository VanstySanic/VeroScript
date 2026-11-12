# 08 : Chaînes, fichiers et chemins

Ce didacticiel réalise une petite tâche : lire un fichier texte, nettoyer son contenu et écrire le résultat dans un autre fichier. Il utilise les modules chaîne, E/S et chemin ensemble.

## Étape 1 : préparer le répertoire de travail

Créez `input.txt` à côté du script :

```text
  VeroScript
  is practical
```

Créez `clean_text.vs` :

```VeroScript
import "io.vs";
import "path.vs";
import "string.vs";

int main()
{
	string input_path = "input.txt";
	string output_path = std::filesystem::join_path("output", "clean.txt");

	if (!std::filesystem::is_file(input_path))
	{
		print("missing: " + input_path);
		return 1;
	}

	std::filesystem::mkdir(std::filesystem::dirname(output_path));
	string content = std::io::read_file(input_path);
	string cleaned = std::strings::trim(content);
	std::io::write_file(output_path, cleaned + "\n");

	print("wrote " + output_path);
	return 0;
}
```

`io.vs` gère les fichiers et les répertoires, `path.vs` joint les chemins et `string.vs` nettoie le texte. Garder ces responsabilités séparées évite la logique de chemin écrite à la main et les aides de chaîne répétées.

## Étape 2 : exécutez et inspectez le résultat

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

Les espaces ASCII en début et en fin du fichier de sortie sont supprimés. `trim` retire uniquement aux extrémités les octets `0x09` à `0x0D` et l'espace (`0x20`) ; il conserve les octets intérieurs et non ASCII. `is_file` suit les liens symboliques et indique si l'entrée résout vers un fichier ordinaire. `read_file` et `write_file` résolvent les chemins relatifs à partir du répertoire de travail actuel, faites donc attention à l'endroit où la commande du terminal est exécutée.

## Étape 3 : traiter les lignes individuelles

Lorsque chaque ligne nécessite un traitement séparé, divisez et joignez le texte :

```VeroScript
import "io.vs";
import "string.vs";

int main()
{
	string content = std::io::read_file("input.txt");
	array<string> lines = std::strings::split(content, "\n");
	array<string> cleaned = array<string>();

	for (string line : lines)
	{
		if (std::strings::trim(line) != "")
		{
			cleaned.push(std::strings::trim(line));
		}
	}

	std::io::write_file(
		"clean-lines.txt",
		std::strings::join(cleaned, "\n") + "\n");
	return 0;
}
```

Le texte Windows peut utiliser `\r\n`. Si l'entrée peut provenir de plusieurs plates-formes, gérez explicitement `\r` au lieu de supposer une seule convention de fin de ligne.

## Étape 4 : formater le résultat

Après traitement du dossier, un résumé lisible est souvent utile. `format` utilise des champs d'accolades et renvoie une chaîne ; la forme multi-arguments de `print` utilise le même dialecte :

```VeroScript
import "string.vs";

int main()
{
	string path = "output/clean.txt";
	int line_count = 2;
	string summary = format("wrote {}, lines={}", path, line_count);
	print(summary);
	printf("status=%s%%\n", "100");
	return 0;
}
```

Utilisez `formatf` ou `printf` lorsque des conversions de style printf sont requises. Ne placez pas la syntaxe `%d` ou `%.2f` dans une chaîne `format`. Voir [Comportement du langage de chaîne](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/strings.md#formatage) pour les champs, la largeur, la précision et les conversions.

## Exercices

1. Modifiez l'extension de sortie en `.clean.txt` avec `std::filesystem::change_extension`.
2. Imprimez les fichiers `std::filesystem::basename`, `dirname` et `extension` du fichier d'entrée.
3. Si la sortie existe, ajoutez un enregistrement de traitement avec `std::io::append_file`.
4. Générez le résumé avec `format`, puis essayez d'utiliser `formatf` pour imprimer une durée avec deux décimales.
5. Supprimez `output/clean.txt`, vérifiez `output` avec `std::filesystem::is_directory`, puis supprimez ce répertoire vide avec `std::filesystem::remove_dir`.

Comparez [`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs). Les API complètes se trouvent dans [I/O](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/io.md), [Paths](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/path.md) et [Strings](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/string.md).

Suivant : [Concurrence et événements](09-concurrency-and-events.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/standard-library/08-strings-files-and-paths.md) · [fr-FR](08-strings-files-and-paths.md) · [ja-JP](../../ja-JP/standard-library/08-strings-files-and-paths.md) · [ko-KR](../../ko-KR/standard-library/08-strings-files-and-paths.md) · [zh-CN](../../zh-CN/standard-library/08-strings-files-and-paths.md) · [zh-TW](../../zh-TW/standard-library/08-strings-files-and-paths.md)
