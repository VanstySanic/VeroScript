# HTTP: Organisation des sources

Http maintient les contrats des requêtes/réponses et des façades client/serveur, les liaisons du protocole et sa configuration de paquet. Ses points d’intégration `Internal/` relient le client Curl et le serveur Boost ; les contrats de transport communs restent dans Network.

La propriété commune des contextes/opérations, la composition du registre, les frontières SDK et les options de compilation sont décrites dans [Network](../../../../../VeroScriptNetwork/Resources/Docs/Developer/fr-FR/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [Utilisateur](../../User/fr-FR/index.md)

<!-- docs-navigation -->

---

[User](../../User/fr-FR/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
