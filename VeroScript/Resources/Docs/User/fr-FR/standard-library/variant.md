# variant — Boîte de valeur dynamique

variant est un type du Core sans import requis. std::variant et variant désignent le même type. La boîte conserve une valeur et son type source canonique, ou reste vide.

Une destination variant connue permet un boxing implicite. La lecture exige cast<T> ou try_cast<T> ; variant_is<T> teste le type exact sans conversion. `optional<T>` et `try_cast` sont également intégrés et ne nécessitent aucun import de la bibliothèque standard. `cast<T>` sur une boîte vide échoue ; `try_cast<T>` sur une boîte vide ou une conversion incompatible retourne un optional vide. Les échecs de copie, annulations et limites restent des erreurs.

Une copie partage la boîte immuable. Les struct de script sont copiées à la mise en boîte et à la lecture ; les classes et conteneurs gardent leur sémantique de référence. Le type déclaré statique est conservé, y compris une classe de base et un null typé. Ce dernier est présent et distinct du vide. Un nullptr brut exige une variable typée.

Seule égalité entre variant est définie : même type canonique et égalité habituelle des valeurs. Aucun calcul dynamique, recherche de membre, appel, déballage implicite, ordre ou hachage. Les clés variant, le transfert channel et les arguments/résultats/captures/globaux de worker contenant variant sont refusés ; les valeurs locales restent possibles. Les valeurs array/map peuvent contenir variant.

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

La VM fournit toute la sémantique. Mixed/asmjit revient à la VM pour les instructions non prises en charge. Le bytecode portable et les paquets conservent les déclarations et instructions. Les AOT autonomes C++, C# et Python refusent explicitement les dépendances variant avant toute sortie. Le débogueur expose $present, $type et $value en lecture seule ; type<variant>().kind() renvoie "variant".

## SDK

Voir [SDK d'interopérabilité — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#exported-value-lifetime).

Si un objet exporté survit à un appel hôte et que ses méthodes ou son destructeur utilisent les variables globales du script, utiliser le mode de session persistante existant : C++ SetHotReloadEnabled(true), C# HotReloadEnabled ou Python hot_reload_enabled=True. Une exécution temporaire libère son état global à la fin de l’appel.

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/variant.md) · [fr-FR](variant.md) · [ja-JP](../../ja-JP/standard-library/variant.md) · [ko-KR](../../ko-KR/standard-library/variant.md) · [zh-CN](../../zh-CN/standard-library/variant.md) · [zh-TW](../../zh-TW/standard-library/variant.md)
