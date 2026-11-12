# Règles de développement et de contribution au projet VeroScript

Ces règles sont consultables publiquement et **s’appliquent uniquement au développement du projet VeroScript et aux contributions à ce projet**. Elles ne constituent ni une spécification du langage, ni une exigence d’utilisation du SDK, ni une condition supplémentaire de la licence MIT. Utiliser, intégrer ou modifier VeroScript dans un autre projet n’impose pas ces conventions de code ni la désignation du titulaire de ce projet comme titulaire des droits sur votre propre code. Les mentions de copyright et de licence existantes restent soumises à la [licence MIT](../../../../LICENSE) et aux licences tierces applicables.

Standards reste dans le dépôt source et figure comme documentation de développement dans la publication complète `All`. La documentation `User`, la documentation utilisateur de Resources, les archives Runtime/SDK/extensions et les installateurs ne contiennent pas Standards.

Ces documents définissent les **normes de code et de publication maintenues manuellement** pour ce projet. Les règles de code couvrent les sources propres au dépôt et entretenues à la main : cœur VeroScript, outils, extensions de langage et autres modules natifs ou applicatifs. [PACKAGING.md](PACKAGING.md) régit les collections officielles et leur validation.

La seule source de maintenance est `Resources/Docs/Standards/`. `zh-CN` fait autorité pour définir les règles ; les autres langues sont des traductions équivalentes qui ne doivent ni ajouter ni modifier de règle. Les règles suivent les décisions explicites du responsable du projet, et non des déductions tirées du code existant, des usages tiers ou des valeurs par défaut des outils. Les anciennes règles restent dans le contrôle de version pour comparaison ; seules les normes actuelles approuvées s’appliquent au code nouveau ou substantiellement modifié.

Les normes restent en construction. Respectez directement les règles confirmées, sans transformer les sujets non définis en obligations. Ajoutez chaque nouvelle règle au fichier le plus proche de son langage ou de son sujet.

## Niveaux d’exigence

- **Obligatoire** : le code nouveau ou substantiellement modifié doit respecter la règle.
- **Recommandé** : choix par défaut ; un écart pour une contrainte du langage, la compatibilité ou la lisibilité doit rester cohérent localement et être expliqué.
- **Selon les besoins** : utiliser seulement si cela améliore la compréhension de la structure, la maintenance ou la compréhension par l’appelant ; éviter les modèles mécaniques et répétitifs.
- **Provisoire** : valeur recommandée actuelle, que le responsable peut compléter ou remplacer.

## Sommaire

| Fichier | Sujet | État |
| --- | --- | --- |
| [GENERAL.md](GENERAL.md) | Structure, commentaires, sauts de ligne et principes de modification communs | Établi |
| [CPP.md](CPP.md) | Format C++, disposition des types, ordre déclaration/définition, macros et switch | Confirmé |
| [CSHARP.md](CSHARP.md) | Adaptation C# des principes structurels C++ et format provisoire | Provisoire, relu |
| [PYTHON.md](PYTHON.md) | Disposition des modules, types et commentaires Python | Provisoire, relu |
| [WEB.md](WEB.md) | Disposition des modules et types TypeScript/JavaScript | Provisoire, relu |
| [VEROSCRIPT.md](VEROSCRIPT.md) | Disposition des sources et types VeroScript | Provisoire, relu |
| [PACKAGING.md](PACKAGING.md) | Packaging, artefacts partagés, profils cibles, validation et reconstruction propre | Adopté ; disposition et contrats statiques implémentés |

## Champ d’application et migration

- Les règles de code couvrent les sources propres au dépôt, maintenues manuellement, et leurs commentaires. Le packaging et la publication suivent [PACKAGING.md](PACKAGING.md).
- `ThirdParty/`, les répertoires et fichiers générés, ainsi que les entrées brutes servant à tester les analyseurs sont exemptés de migration obligatoire.
- Tout nouveau fichier respecte entièrement sa norme. Pour un fichier existant, privilégiez le code ajouté ou lié à la modification ; ne reformatez pas massivement du code sans rapport lors d’une modification fonctionnelle.
- Les contrats valides du langage, de l’ABI, des protocoles ou de la construction priment sur la mise en forme. Préservez le contrat et consignez l’exception dans la norme concernée.

## Maintenance

Les modifications de règle doivent être limitées et explicites : règle, champ, exemples nécessaires et exceptions. Ne transformez pas une écriture fortuite d’un fichier historique en norme globale.

- Modifiez d’abord l’original `zh-CN`, puis les cinq traductions dans la même modification. Les niveaux, champs, exceptions et états provisoires restent équivalents.
- Maintenez les textes dans leur langue ici. Relisez les exemples quand une règle change ; ne déduisez pas de nouvelles règles du code existant.
- Maintenez les règles dans `Resources/Docs/Standards/<locale>` : `zh-CN` définit les règles et les cinq autres langues restent équivalentes. Après modification, exécutez `python Resources/Tools/Packaging/veroscript_docs.py check` et `check-collection`. Les règles originales figurent uniquement dans `All` comme documentation de développement, pas dans `User` ni dans les distributions fonctionnelles.
- Ne traduisez pas le code, les API, les champs de protocole, les commandes ni les deux lignes de l’en-tête SPDX. Les commentaires des exemples restent en anglais.
