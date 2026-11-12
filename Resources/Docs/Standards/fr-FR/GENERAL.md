# Normes générales de code

Portée : [développement et contributions au projet VeroScript](README.md).

Ce fichier contient seulement les règles communes à tous les langages. L’indentation, les accolades, la disposition des types et la syntaxe propres à un langage suivent sa norme.

## Structure et niveaux

- Regroupez le code par responsabilité. Un bloc exprime un concept facile à nommer : interfaces héritées, types imbriqués, méthodes publiques, auxiliaires privés ou données membres.
- Séparez les blocs par une ligne vide. N’utilisez pas des lignes vides répétées, de longs séparateurs ou des titres de région sans sens pour remplacer une structure claire.
- Les portées, espaces de noms, types, méthodes, conditions et boucles doivent montrer leur imbrication réelle ; ne l’aplatissez pas pour réduire le nombre de lignes.
- En l’absence de règle, préférez le style cohérent le plus proche du code plutôt que d’en introduire un nouveau dans une modification.

## Indentation

- Les sources, tests, exemples et scripts de construction/outillage propres au projet et maintenus manuellement utilisent une tabulation réelle par niveau logique, affichée sur quatre colonnes par défaut. Ne remplacez pas les niveaux par des espaces seuls.
- Les tabulations expriment les niveaux. Les continuations peuvent utiliser des espaces d’alignement là où la syntaxe l’autorise, sans mélanger les deux pour l’indentation des blocs.
- La syntaxe du langage et du format est prioritaire. [YAML impose des espaces pour les niveaux](https://yaml.org/spec/1.2.2/#61-indentation-spaces), exception explicite à cette règle ; voir [PYTHON.md](PYTHON.md) pour les règles d’analyse Python.
- Conservez la structure Markdown ; les blocs de code suivent leur norme de langage. Ne convertissez pas mécaniquement chaînes, entrées de test, textes tiers ou résultats générés ; ajustez au besoin les sources générées via le modèle de génération du module concerné.

## Fins de ligne des fichiers

- Les fichiers texte propres au dépôt utilisent LF (`\n`) : sources, code de test, exemples, documentation, configuration et scripts de construction/outillage. La même règle s’applique à l’extraction et à l’édition sous Windows, Linux et macOS. L’indentation par tabulations ne change pas.
- Gardez `.editorconfig` et `.gitattributes` cohérents. Les générateurs qui maintiennent des fichiers du dépôt utilisent explicitement LF. Convertissez uniquement les fins de ligne physiques ; conservez l’encodage et les BOM, notamment le BOM UTF-8 requis par Windows PowerShell 5.1. Ne réécrivez ni les échappements `\r\n`, ni les séparateurs de protocole, les entrées utilisateur ou les données littérales ; ne réduisez pas la prise en charge des entrées CRLF par le langage ou les outils.
- Ne convertissez pas mécaniquement les textes tiers, fichiers de licence/outillage importés, ressources binaires ou données de test sensibles aux octets. Préservez leur contenu et leurs propres règles Git de fins de ligne ou les règles existantes. Les guides de licence rédigés pour le projet utilisent LF. Un répertoire nommé Tests ne dispense pas le code de test ordinaire.
- Les lanceurs Windows `.bat` et `.cmd` utilisent CRLF. Préservez les octets existants des autres littéraux multilignes ou données de test qui dépendent des fins de ligne physiques, avec des exceptions justifiées par chemins précis dans Git et la configuration de l’éditeur. Retirez une exception seulement après avoir vérifié que les fins de ligne d’origine ne sont plus nécessaires. Ne modifiez pas les entrées de test, le contenu des sorties ou le comportement d’exécution pour respecter LF.

## Nommage

- Les noms doivent exprimer une responsabilité, un état ou une valeur. N’utilisez pas des noms d’étape tels que `New`, `Old`, `Temp`, `Final` ou `v2`.
- Un concept a un seul nom stable dans un module. Les noms de protocole aux frontières de langages gardent leur orthographe contractuelle.
- Les noms booléens devraient exprimer un sens affirmatif pour rendre les conditions directement lisibles.
- Toute nouvelle abréviation doit être compréhensible localement. Conservez la casse et l’orthographe des abréviations publiques établies.

## Commentaires

- Tous les commentaires des sources propres au projet et maintenues manuellement doivent être en anglais : commentaires de ligne, de bloc, de documentation et de région. Les citations de protocoles externes, licences tierces et contenus générés ne sont pas traduits au titre de cette règle.
- Expliquez le **pourquoi**, les invariants, frontières, règles de propriété, contraintes de threads, compatibilités ou choix non évidents ; ne décrivez pas ligne par ligne du code évident.
- Commentez les types, méthodes et variables membres selon les besoins. Priorité aux surfaces publiques, états complexes, ressources possédées et interfaces faciles à mal utiliser ; évitez les répétitions pour les membres simples suffisamment nommés.
- Utilisez la syntaxe habituelle du langage pour une explication sur une ligne, par exemple `//` en famille C et `#` en Python. Utilisez les commentaires de documentation habituels pour des explications continues, les paramètres/retours ou une documentation structurée.
- Réservez les commentaires de bloc multiligne aux explications à lire ensemble, licences ou textes intégrés ; n’y enveloppez pas du code ordinaire.
- Gardez les commentaires près du code décrit. Mettez à jour ou supprimez les commentaires obsolètes lors des modifications.

## Mention de copyright en tête de fichier

Le modèle ci-dessous concerne uniquement le code de ce projet avec cette attribution ; le `LICENSE` racine donne le titulaire exact. Les projets externes indiquent les véritables titulaires de leur propre code. Copier ou distribuer le code de ce projet exige toujours de conserver les mentions de copyright et le texte de licence existants selon les licences applicables.

- Les sources, tests, exemples et scripts de construction/outillage propres au projet, maintenus manuellement et sous licence MIT utilisent l’en-tête SPDX de deux lignes ci-dessous. Ajoutez-le aux nouveaux fichiers ; remplacez les anciennes mentions propres au projet lors d’une migration coordonnée ou de modifications connexes.
- Remplacez `<copyright-holder>` dans les modèles par le titulaire exact indiqué dans le `LICENSE` racine avant utilisation. Omettez les années et les URL dans les en-têtes source. Conservez cet ordre des deux lignes ; ne traduisez ni les noms de champs, ni le titulaire, ni l’identifiant de licence.
- La famille C, C#, VeroScript, TypeScript/JavaScript, Java et Kotlin utilisent `//` :

```cpp
// SPDX-FileCopyrightText: <copyright-holder>
// SPDX-License-Identifier: MIT
```

- Python, CMake, PowerShell et les scripts Shell utilisent `#` :

```python
# SPDX-FileCopyrightText: <copyright-holder>
# SPDX-License-Identifier: MIT
```

- Les sources XML, XAML et Avalonia `.axaml` utilisent deux lignes distinctes de commentaires XML :

```xml
<!-- SPDX-FileCopyrightText: <copyright-holder> -->
<!-- SPDX-License-Identifier: MIT -->
```

- Placez l’en-tête avant la première ligne source ordinaire, suivi d’une ligne vide. Les directives dont la position est imposée, telles que shebang, déclaration d’encodage, `#requires` de PowerShell ou déclaration XML, restent en premier ; les deux lignes suivent.
- Pour les sources et déclarations de types générées, faites produire l’en-tête par le modèle de génération concerné selon les besoins ; ne modifiez pas manuellement le résultat généré pour satisfaire ce format.
- Conservez les mentions de copyright et de licence des sources tierces ou importées. Ne les remplacez pas par le nom du projet ou l’identifiant MIT.
- Les documents Markdown, configurations JSON/YAML, images et autres ressources n’exigent pas un en-tête de code dans chaque fichier.
- Les deux lignes SPDX ne remplacent pas le texte complet de licence. Conservez l’année de copyright et le texte MIT intégral dans le `LICENSE` racine, en conservant son titulaire original ; les distributions conservent les mentions complètes de copyright et d’autorisation exigées par la licence.

## Sauts de ligne et lisibilité

- Les sauts de ligne expriment les niveaux sémantiques, groupes de paramètres et sens de lecture, plutôt qu’une largeur mécanique.
- Décomposez les paramètres, paramètres de template, initialiseurs ou conditions longs en unités logiques ; les lignes de continuation doivent clairement appartenir à la même expression.
- Aucune ligne vide n’est obligatoire après chaque accolade ouvrante ou avant chaque accolade fermante. Décidez selon le premier et le dernier bloc logique.
- Une ligne porte une intention principale. Les exceptions pour gardes simples, fonctions vides ou raccourcis idiomatiques exigent une règle explicite du langage ; sinon, pas de raccourci.
- Lorsque la syntaxe du langage le permet, placez `else`, `else if` (`elseif` en PowerShell), `catch`,
  `finally` et le `while` final de `do…while` sur une nouvelle ligne, alignés sur l’accolade fermante
  précédente ; n’utilisez jamais une forme jointe telle que `} else`. Pour un corps de bloc, placez aussi
  l’accolade ouvrante sur sa propre ligne alignée, puis les instructions et l’accolade fermante sur des lignes
  distinctes. Ne réécrivez pas mécaniquement les chaînes, entrées de test ou textes tiers pour satisfaire le
  formatage ; préservez la continuation des lignes dans les macros.

- Les formateurs, modèles générant du code source et exemples de documentation actuelle suivent aussi cette disposition ; les entrées de test conservent leur objectif de couverture.

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

## Principes de modification

- Ne modifiez que ce que la tâche exige ; ne supprimez, n’écrasez ni ne réordonnez les modifications de l’utilisateur sans rapport avec elle.
- Tout changement de comportement, interface publique, sérialisation ou frontière de langages doit mettre à jour déclarations, implémentations, appelants et tests ensemble.
- Le refus, la récupération et le repli doivent être observables par l’appelant ; ne présentez pas un échec comme un succès.
- Validez et nettoyez entrées externes, chemins, arguments de processus et cycles de vie des ressources à la frontière qui en est responsable.


## Éditeurs et plateformes embarquées

Core possède la sémantique du langage, les services d’exécution et les contrats génériques VM/JIT. La boucle d’interprétation appartient à `Modules/Execution/VeroScriptBytecodeVm` ; compilation JIT, IR, émission native et dépendances tierces relèvent de `Modules/Execution`. Un backend consomme les contrats publics et conserve le module pour les instances, workers et code retenu. Vérifiez l’ABI avant la fabrique ; un échec conserve le backend précédent. Ne changez pas la sélection ou la configuration du backend pendant l’exécution.

Les clients éditeur possèdent paramètres et présentation protocole. Les adaptateurs Mobile/Android/iOS consomment l’ABI C publique, fournissent les ressources explicitement et documentent l’acceptation réelle. Respectez thread créateur, callbacks et annulation/reset ; les quotas ne constituent pas un sandbox complet. Kotlin/Java suivent les conventions du module pour le nommage et la structure, et la règle commune d’indentation de ce fichier ; Objective-C/Objective-C++ suivent les règles de propriété plateforme et les frontières natives. Gardez stables les identifiants protocole.

User décrit tâches, comportement et API publique ; Developer décrit architecture, builds, tests et publication ; les versions et preuves antérieures restent dans le contrôle de version ou les sorties ignorées, hors de la documentation publiée. Actualisez les six langues lors des changements. Traduisez validation UI et diagnostics via catalogues, en conservant IDs, clés et paramètres. Vérifiez liens et publications All/User ; tests plateforme/IDE ignorés ne justifient pas une certification.
