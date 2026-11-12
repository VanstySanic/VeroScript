# Formats et dialectes Schema

Dans Draft 2020-12, format est une annotation par défaut. FSchemaOptions::bAssertFormats ou schema_options.set_assert_formats(true) impose les assertions dans toutes les ressources compilées ; un nom inconnu échoue alors à la compilation. Elles concernent les strings seulement. Un écart est une invalidité normale ; annulation/allocation/limites signifient validation incomplète. Aucun DNS, réseau, envoi ou lecture de ressource, aucune modification des chaînes.

Les 19 formats sont implémentés : date-time/date/time suivent RFC 3339, offsets et fraction non vide, avec contrôle de position de seconde intercalaire après conversion d’offset, sans bulletin externe ; duration suit annexe A (semaines, ordre, composants intermédiaires). email/idn-email contrôlent SMTP, guillemets/échappements/longueurs/IP, et partie locale UTF-8 internationale. hostname/idn-hostname contrôlent longueurs, A-label/Punycode/NFC/IDNA contexte/bidi, avec UTS #46 non transitionnel ; sans point racine final. ipv4/ipv6 sont stricts, sans zéro IPv4 initial. uri/uri-reference : RFC 3986 ; iri/iri-reference : RFC 3987 et positions privées restreintes. uuid : hex avec tirets sans URN ; uri-template : RFC 6570 ; json-pointer/relative-json-pointer : formes string sans décodage fragment ; regex : Unicode ECMAScript.

IDNA utilise Unicode 17 / python-idna 3.12, compilé en C++, sans Python d’exécution. Entrée de mappage 4096 octets, 256 points de code par label et 64 Kio temporaires de normalisation ; dépassement : Limit. DNS conserve 63 octets A-label/label et 253 au total. Annulation active.

Une méta-Schema personnalisée peut déclarer format-assertion et être sélectionnée par $schema. Vocabulaire connu facultatif : sémantique entière ; inconnu obligatoire : refus ; mots-clés inconnus facultatifs : annotations. Seul $vocabulary de la méta-Schema directement sélectionnée s’applique : pas d’héritage par $ref ni de choix de dialecte par $vocabulary de la Schema métier. Les ressources embarquées héritent sauf leur propre $schema ; documents récupérés sans $schema : Draft 2020-12 du module. Les méta-Schema cycliques restent validées et bornées. bAssertFormats est un remplacement explicite. meta/format-assertion est intégré ; un format inconnu reste permis en mode annotation.

```VeroScript
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
	json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/schema-formats.md) · [fr-FR](schema-formats.md) · [ja-JP](../../ja-JP/Reference/schema-formats.md) · [ko-KR](../../ko-KR/Reference/schema-formats.md) · [zh-CN](../../zh-CN/Reference/schema-formats.md) · [zh-TW](../../zh-TW/Reference/schema-formats.md)
