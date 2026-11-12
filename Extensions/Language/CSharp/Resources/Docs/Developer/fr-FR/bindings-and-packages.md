# Liaisons, paquets et compatibilité C#

## Modèle de binding hôte

Les définitions managées représentent fonctions, types, membres, opérateurs, annotations, valeurs enum, documentation, paramètres de type et callbacks de protocole hôte. Un protocole de type hôte peut inclure clone, égalité, hash et factories de sous-classe. Les modèles de type hôte générique sont instanciés par le contrat de callback Interop et doivent conserver l’identité de type utilisée par le catalogue.

Le catalogue généré est une métadonnée. Les callbacks d’enregistrement relient les objets/fonctions managés vivants ; un catalogue seul n’invoque aucune méthode C#.

## Paquets

Les options de construction d’un paquet comprennent type, nom, version, métadonnées hôte et politique d’informations debug. Un paquet est lu depuis des bytes, expose ses métadonnées et ses bytes signés canoniques, puis est explicitement libéré. Son chargement ne consomme le module compilé que si la session accepte le paquet et la forme type/entrée.

## Compatibilité API

Le contrat API contrôlé constitue la frontière de compatibilité pour types managés publics, constructeurs, propriétés, valeurs enum, signatures callback et frameworks cibles. Les changements de layout natif sont revus avec `VeroScriptInterop.h`. Une modification de wrapper reste incomplète tant que tests managés, contrat API public, vérification de paquet et consommateur ABI natif ne sont pas cohérents.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/bindings-and-packages.md) · [fr-FR](bindings-and-packages.md) · [ja-JP](../ja-JP/bindings-and-packages.md) · [ko-KR](../ko-KR/bindings-and-packages.md) · [zh-CN](../zh-CN/bindings-and-packages.md) · [zh-TW](../zh-TW/bindings-and-packages.md)
