# Interface graphique

Gui fournit les arbres de widgets, les constructeurs, les attributs, les événements et ML/CSS. RmlUi est une implémentation de mise en page et de rendu sélectionnée séparément.

- [Framework et intégration RmlUi](Reference/Framework.md)
- [Balisage et styles](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/fr-FR/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/fr-FR/index.md)

`gui-compile` exige la prise en charge Application et produit le conteneur ZIP `.vsapp` standard. Celui-ci conserve le descripteur de démarrage, l’image GUI compilée, ses métadonnées de ressources et les fichiers explicitement intégrés. Les commandes GUI et les commandes CLI générales de vérification, d’exécution et inspect lisent le même conteneur. Un Runtime avec scripts GUI peut aussi l’exécuter. Recompilez les anciens fichiers GUI `.vsapp` bruts. Les backends suivent la politique de sélection de l’environnement installé ; l’hôte, le projet et les options peuvent la remplacer. Les éditeurs ne fixent pas RmlUi.

[Tutoriels: Collections, navigation et dialogues](../../../../../../../Resources/Docs/Tutorials/fr-FR/toolchain/24-gui-collections.md)

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
