# Conversion C++

Incluez VeroScriptJsonMapping.h et liez VeroScript::Json, sans Core. Déclarez explicitement champs/propriétés dans TObjectMapping<T>, puis TryEncode/TryDecode. La sortie change uniquement au succès. Le décodage prépare les conversions avant construction normale et affectation ; une fabrique shared_ptr<T> permet les types sans constructeur par défaut. Les effets externes des constructeurs/accesseurs/convertisseurs ne sont pas annulés.

Noms UTF-8 et NUL acceptés ; chemins échappés selon RFC 6901. Les champs inconnus échouent sauf bIgnoreUnknownFields. FFieldOptions règle required/ignore/serialize/deserialize ; un champ de sortie seule présent en entrée échoue, un champ ignoré est sauté. Doublons et directions contradictoires échouent à l’enregistrement. Ajoutez explicitement les champs de base ; pas d’énumération automatique des statiques/propriétés. Seuls les accesseurs Property enregistrés sont exécutés.

Booléens, entiers jusqu’à 64 bits, flottants finis et chaînes UTF-8 possédées gardent leur type JSON. Les entiers ne passent pas par double. bExactFloatingPoint interdit l’arrondi, sinon l’arrondi contrôlé est permis. vector/list/deque/tableaux fixes deviennent des tableaux, avec longueur vérifiée. set refuse les doublons, multiset les conserve ; l’ordre suit le conteneur. Les maps uniques à clé string deviennent des objets, les autres maps/multimaps demandent bMapAsPairs. optional/shared_ptr/unique_ptr acceptent null, les valeurs ordinaires non. RegisterEnum utilise les noms sauf bEnumsAsNumbers et refuse encore les valeurs inconnues. Un type non pris en charge échoue sans objet vide de remplacement.

RegisterConverter<T> fournit encode/decode/Schema typés. Le FMappingContext récursif partage chemin, budgets et annulation. Chaque appel fige un instantané ; les copies du registre partagent les enregistrements, les remplacements n’affectent pas l’appel courant. Owner doit couvrir code, destructeurs et RTTI. Les shared_ptr retournés et imbriqués, copies/alias compris, conservent les convertisseurs de l’appel après destruction du registre/résultat extérieur. Sortir un objet par valeur ou utiliser un autre bloc de contrôle demande son propre Owner. Le premier Owner RTTI de la clé reste jusqu’à libération du registre/instantanés. Callbacks/fabriques sont détruits avant Owners et hors verrou, même lors d’un échec d’insertion.

Un cycle du chemin actif donne json.cycle ; une référence partagée entre branches est répétée par valeur, sans reconstruction d’identité. TryMappingSchema<T>(Input/Output,...) reprend directions, $defs/$ref, bornes entières, longueurs et enums. Une Schema de convertisseur absente échoue. Invariants de construction, comparaisons particulières, unicité des clés en paires et conversion flottante exacte restent vérifiés au décodage. FMappingOptions::Limits et FWork emprunté bornent octets JSON compacts, profondeur, nœuds, texte et nombres. La première erreur/son chemin est conservé ; annuler ne valide aucune sortie.

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/native-mapping.md) · [fr-FR](native-mapping.md) · [ja-JP](../../ja-JP/Reference/native-mapping.md) · [ko-KR](../../ko-KR/Reference/native-mapping.md) · [zh-CN](../../zh-CN/Reference/native-mapping.md) · [zh-TW](../../zh-TW/Reference/native-mapping.md)
