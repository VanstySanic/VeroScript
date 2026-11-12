# Maintenance de l’intégration des éditeurs

Ce guide définit la frontière commune entre clients d’éditeur et hôtes natifs d’exécution/protocole. L’implémentation et les formats des ressources restent dans les guides des modules ci-dessous.

Les clients éditeur gèrent configuration, transport et présentation IDE ; sémantique, LSP et DAP relèvent de Tooling/Core natifs. Les catalogues sont des métadonnées, pas des bindings exécutables. Les tests de domaine utilisent des fixtures protocole : ils ne valident ni les points sûrs VM ni un hôte application/UE. Installez le pont Session avant exécution ; l’hôte sérialise son contexte unique et possède l’annulation. `jitCapability` est un contrat de service ; Run peut transmettre un chemin de module compatible explicite ou hériter du choix de l’environnement de modules CLI (auto par défaut si non configuré). LSP/Check/debug VM local ne chargent pas JIT.

Maintenez ensemble les six langues UI/documentaires ; préservez options, identifiants de messages et valeurs de protocole. Vérifiez les paramètres avec le formateur réel du client. User décrit les opérations ; Developer décrit architecture, compilation et publication. Vérifiez les compatibilités SDK, Runtime et plateformes d’après les versions choisies et les résultats réels.

## Implémentations propres aux clients

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/fr-FR/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/fr-FR/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/fr-FR/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/fr-FR/index.md)

## Contrats natifs communs

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/fr-FR/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/session-debugging.md)

<a id="acceptance-evidence"></a>

Pour chaque validation, consignez OS/architecture, versions de l’éditeur et du Runtime, identité complète des sources, empreintes des paquets, résultats réels et cas ignorés. L’exécution native exige sa propre validation avec le Runtime correspondant. Les résultats historiques ne valent que pour leurs artefacts et leur environnement consignés.

<a id="editor-icon-inputs"></a>

## Ressources d’icônes communes

Le contour maintenu et les familles produites sont définis dans `Resources/Tools/Assets/generate-icons.cjs`. L’image produit utilise `#60A5FA`, les marques de fichiers `#5D75AE`, et les glyphes des éditeurs conservent leurs couleurs de thème et leur alignement. La génération complète met à jour les SVG, PNG, ICO et ICNS du produit et des fichiers, ainsi que les SVG d’éditeur. `--brand-only` met à jour la famille produit. La [procédure de génération](../../../Assets/icons/README.md#regeneration) précise Node.js, `sharp` et `--sharp-module`. Le packaging Runtime consomme les fichiers maintenus.

Chaque construction d’extension VS Code actualise son PNG d’affichage depuis celui du produit. Les SVG JetBrains portant le V proviennent du générateur commun. Gradle place les icônes de liste de plugins dans `META-INF` et conserve les autres ressources sous `Icons`. Préservez les chemins, suffixes de thème et références de manifeste dans toutes les langues UI. Les détails de copie et de packaging appartiennent aux guides des modules.

VS Code empaquette le README racine du module comme page Détails, tandis que JetBrains utilise la description et le fournisseur du descripteur de plugin. Gardez une présentation concise et un lien vers le sélecteur documentaire en six langues. Les manuels publics et procédures de maintenance conservent leurs originaux et rôles de publication.

<!-- docs-navigation -->

---

[Maintenance documentaire](index.md)

[en-US](../en-US/editors.md) · [fr-FR](editors.md) · [ja-JP](../ja-JP/editors.md) · [ko-KR](../ko-KR/editors.md) · [zh-CN](../zh-CN/editors.md) · [zh-TW](../zh-TW/editors.md)
