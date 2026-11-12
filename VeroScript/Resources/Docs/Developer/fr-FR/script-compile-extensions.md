# Extensions de compilation en script

Les extensions sont des modules VeroScript ordinaires utilisant `std::compiler` pour enregistrer documentation d’annotations et processeurs. Core les charge avant le traitement des sources métier ; l’exécution utilise ensuite le bytecode généré.

## Chargement

Au début de l’entrée métier, après commentaires, espaces ou imports compiler précédents :

```vs
import compiler "builder.vs";
```

Le chemin est littéral. Un import ordinaire fournit du code auxiliaire ; import compiler exécute l’enregistrement et décrit ses dépendances. Les cycles sont rejetés. Les modules métier suivants utilisent seulement les extensions déjà chargées par l’entrée ou l’hôte. Une extension ne dépend pas de sa propre syntaxe encore non enregistrée.

L’hôte définit `FCompileOptions::ScriptCompileExtensions.Roots` (Path et RegistrationEntry facultatif). Loader, Session C++ et Codegen partagent la configuration et les règles existantes d’overlays, imports et stdlib. bAllowSourceImports=false désactive l’activation par source, sans supprimer les racines configurées.

## Enregistrement et services

L’extension importe compiler.vs et exporte sa propre entrée :

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

La fabrique sans argument retourne `fn(std::compiler::context,std::compiler::output)->void`. Chaque tâche crée un état VM neuf, enregistre et crée les fabriques une fois. Les rappels conservent l’état entre phases. Processeurs natifs et script partagent ordre et contrôle des conflits. Une description d’annotation seule n’exécute rien.

Context expose phase, événement, module, déclarations, paramètres, valeurs par défaut, modificateurs, génériques, annotations, commentaires et positions. Output fournit add_declaration, edit_source, replace_declaration, complete_body, replace_body, set_metadata, report_diagnostic et submit_operation. Les opérations complètes transportent métadonnées enfants et origines ; les validations du cœur restent obligatoires.

Les services registration, context et output sont valides pendant leur appel. Les valeurs copiées en lecture seule peuvent être conservées ; les scopes périmés ne modifient pas les lots suivants. Les plages UTF-8 ne coupent pas de caractères. Les génériques sont traités une fois puis substitués normalement ; implémentations utilisateur, overrides et corps par défaut suivent le pipeline commun.

Initialisation, enregistrement, fabriques et rappels partagent budget total et annulation. La VM de compilation interdit console, accès direct fichiers/environnement, tâches, changement de dossier, adresses natives et horloge. Les bindings supplémentaires sont explicites ; le non-déterminisme non déclaré empêche la réutilisation du cache. Ce n’est pas un bac à sable complet pour code natif non fiable.

Configuration et read_dependency retournent des valeurs possédées. Les chemins de ressources sont relatifs à la racine active. Sources, paquets, configuration, contrat de services et dépendances présentes ou absentes participent à la validation du cache.

## Paquets et intégration

CompileScriptCompileExtensionSource compile sans exécuter l’enregistrement. BuildPackage produit une Library avec CompileExtensionManifest copié dans les options. Le chargement contrôle entrée, version, empreinte de services, bindings et dépendances avant initialisation. Les paquets métier génériques rejouent leurs opérations sans exécuter l’extension du producteur.

C++ utilise ScriptCompileExtensions ; C utilise vs_interop_session_set_script_compile_options ; C# SetScriptCompileExtensions ; Python set_script_compile_extensions. Modifiez une session au repos sur son thread créateur. CLI : --compiler-extension, --compiler-config, --no-compiler-imports, --compiler-instructions, --compiler-timeout et `compiler-package source.vs output.vslib`.

Outils et AOT source utilisent le même programme préparé. Les exports autonomes ne conservent pas la VM de traitement. JIT exécute le programme métier ; les extensions utilisent toujours VM. Le cache revalide configuration et dépendances avec la clé sources/imports de l’hôte.

Exemples : [builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs), [main.vs](../../../Examples/ScriptCompileExtensions/main.vs).

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Language: [en-US](../en-US/script-compile-extensions.md) · [fr-FR](script-compile-extensions.md) · [ja-JP](../ja-JP/script-compile-extensions.md) · [ko-KR](../ko-KR/script-compile-extensions.md) · [zh-CN](../zh-CN/script-compile-extensions.md) · [zh-TW](../zh-TW/script-compile-extensions.md)
