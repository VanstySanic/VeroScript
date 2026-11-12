# Diagnostics et localisation

Pilotez le programme avec FError::Code/Id() et error.message_id(), jamais avec le texte brut. Resources/Localization/<locale>/json.json fournit six langues. Catégories, chemins présentés et erreurs de liaison sont traduits ; détails du moteur et du convertisseur utilisateur conservés.

FScriptError.MessageId/MessageArgs se rendent via le service de diagnostic hôte. Erreur JSON : deux arguments, détail brut et JSON Pointer d’instance (vide à la racine). json.mapping.declaration : un détail. Catalogue/langue absent : anglais de référence, sans changer catégorie/chemin. Une allocation de présentation échouée garde l’erreur initiale.

L’erreur non levante `json::error` expose `message_id`, `message` et `instance_pointer` selon les mêmes règles d’arguments. Le SDK C++ indépendant ne lie pas Core et ne gère pas de langue globale. Montez le répertoire installé `share/VeroScriptJson/Resources/Localization` dans le résolveur de ressources de l’hôte ; conservez `json.json` lors d’une fusion avec les catalogues d’autres modules.

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
