# Maintenance JSON

La couche de données possède DOM/texte/flux. Schema utilise jsoncons 1.10.0 avec son correctif privé et la bibliothèque autonome regex/Unicode QuickJS-ng 0.17.0, sans moteur JavaScript. Préservez l’adaptation exacte des nombres. La conversion C++ reste indépendante de Core, celle du script appartient au processeur du module. Les modules de structures simples utilisent un support réduit ; imports, génériques, héritage, annotations, membres exécutables ou valeurs initiales complexes imposent le support complet. Préservez ordre, portée et identité des types fermés.

Utilisez l’entrée CMake du module. VEROSCRIPT_BUILD_JSON intègre la racine, VEROSCRIPT_JSON_BUILD_BINDINGS contrôle les enregistrements statiques/extensions dynamiques. Un SDK Core installé compatible permet les liaisons sans reconstruire Core. VeroScript::Json est la cible native, COMPONENTS Bindings ajoute VeroScript::JsonBindings. Le composant Bindings complète données/licences ; modules dynamiques sous lib/veroscript/modules. L’identité d’implémentation est nécessaire, la version seule ne suffit pas.

Voir [Réseau: Organisation des sources — Construction et enregistrement des modules natifs](../../../../../VeroScriptNetwork/Resources/Docs/Developer/fr-FR/index.md#network-execution-assembly).

Quand l’exécution est autorisée, vérifiez données natives, enregistrements statiques/dynamiques, modules sources et bibliothèques binaires séparément. VEROSCRIPT_JSON_SCHEMA_CORPUS sélectionne le corpus épinglé et les formats ; les formats inconnus sont testés en mode annotation. VEROSCRIPT_JSON_NORMALIZATION_CORPUS sélectionne NormalizationTest.txt Unicode 17, VEROSCRIPT_JSON_CORPUS JSONTestSuite. Les aides explicites Resources/Tools/Tests téléchargent les données, jamais une compilation normale. Enregistrez identités et résultats réels ; les anciens nombres ne certifient pas le code actuel, autres hôtes, JIT/AOT, mobiles ou navigateur.

Maintenez les six json.json, IDs/arguments et l’installation share/VeroScriptJson/Resources/Localization. Le profil json remplace les entrées précédentes, sans superposition cumulative. Générez les tables via Resources/Tools/Unicode/generate_schema_unicode.py avec les entrées épinglées ; Python ne devient pas une dépendance d’exécution. Préservez notices et inventaires originaux.

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/fr-FR/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)
