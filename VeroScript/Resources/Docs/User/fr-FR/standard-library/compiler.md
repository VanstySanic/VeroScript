# Extensions de compilation en script

<a id="source-extension-activation"></a>

Une extension fournie par l’hôte ou l’application peut traiter les annotations et générer des déclarations avant les vérifications ordinaires. Activez les sources avec import compiler au début du fichier d’entrée, après les commentaires éventuels et les autres imports de compilation. Un import ordinaire n’exécute pas l’enregistrement.

## Extension descriptive

Enregistrez le premier fichier sous notes.vs et le second sous main.vs. Cette extension décrit seulement @Note ; elle ne génère aucun comportement métier.

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

Le programme affiche A descriptive annotation. La bibliothèque du Runtime doit contenir compiler.vs ; core.vs ne réexporte pas cette entrée réservée à la compilation.

## Extensions empaquetées et limites

compiler-package produit une extension .vslib sans exécuter l’enregistrement. Pour la consommer, créez consumer.vs avec le même programme métier mais sans import compiler, puis choisissez notes.vslib explicitement. N’activez pas simultanément les copies source et paquet.

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

Voir [Outil de ligne de commande — Extensions de compilation en script](../../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md#cli-compiler-options).

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[Annotations](../language-basics/annotations.md)

Le budget total par défaut est de 1 000 000 d’instructions VM et de 2 000 ms. Zéro désactive le budget correspondant ; ces limites ne peuvent pas interrompre un rappel natif bloquant.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md) · [en-US](../../en-US/standard-library/compiler.md) · [fr-FR](compiler.md) · [ja-JP](../../ja-JP/standard-library/compiler.md) · [ko-KR](../../ko-KR/standard-library/compiler.md) · [zh-CN](../../zh-CN/standard-library/compiler.md) · [zh-TW](../../zh-TW/standard-library/compiler.md)
