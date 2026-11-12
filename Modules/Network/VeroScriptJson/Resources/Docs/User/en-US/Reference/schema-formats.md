# Schema formats and dialects

Draft 2020-12 format is annotation-only by default. Set FSchemaOptions::bAssertFormats or schema_options.set_assert_formats(true) to assert all compiled resources; an unknown format then fails compilation. Assertions apply only to strings. A mismatch is ordinary invalidity; cancellation, allocation or limits mean incomplete validation. No DNS, network, delivery or resource access occurs and strings are not modified.

All 19 formats are implemented: date-time/date/time check RFC 3339 syntax, offsets and nonempty fractional seconds, including offset-adjusted leap-second position without consulting bulletins; duration checks RFC 3339 appendix A weeks/order/intermediate components. email/idn-email check SMTP quoting/escapes/length/IP literals, with UTF-8 local parts for the international form. hostname/idn-hostname check labels/length, A-label/Punycode/NFC/IDNA context/bidi; international mapping is nontransitional UTS #46. Hostnames omit the final root dot. ipv4/ipv6 use strict syntax, IPv4 rejects leading zeros. uri/uri-reference use RFC 3986; iri/iri-reference use RFC 3987 Unicode ranges and restrict private-use positions. uuid accepts hyphenated case-insensitive hex, no URN wrapper; uri-template uses RFC 6570; json-pointer/relative-json-pointer are string forms, not fragment decoding; regex uses Unicode ECMAScript.

IDNA uses Unicode 17 / python-idna 3.12 data compiled into C++; no Python runtime is needed. Mapping input is bounded to 4096 bytes, each mapped label to 256 code points, normalization scratch to 64 KiB; exceeding work limits reports Limit. DNS A-label limits remain 63 bytes/label and 253 total. Cancellation remains active.

A custom meta-Schema may declare the format-assertion vocabulary and be selected through $schema. A known optional vocabulary keeps full semantics; unknown required vocabularies fail, unknown optional keywords remain annotations. Only the directly selected meta-Schema’s $vocabulary applies: $ref does not inherit it, and a business Schema’s own $vocabulary does not select a dialect. Embedded resources inherit unless their own $schema changes it; separately retrieved documents without $schema use this module’s Draft 2020-12 default. Self/cyclic meta-Schemas still undergo validation and budgets. bAssertFormats is an explicit override. The built-in meta/format-assertion resource is included. Unknown formats remain allowed in default annotation mode.

```VeroScript
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
	json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](schema-formats.md) · [fr-FR](../../fr-FR/Reference/schema-formats.md) · [ja-JP](../../ja-JP/Reference/schema-formats.md) · [ko-KR](../../ko-KR/Reference/schema-formats.md) · [zh-CN](../../zh-CN/Reference/schema-formats.md) · [zh-TW](../../zh-TW/Reference/schema-formats.md)
