// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/reexport.vs";
import "Fixtures/default_mapping.vs";
int main() {
 Leaf::Plain input=Leaf::Plain();
 json::value encoded=json::converter<Leaf::Plain>::write(ref input);
 Leaf::Plain decoded=json::converter<Leaf::Plain>::read(encoded);
 assert(decoded.count==37 && decoded.enabled && decoded.label=="leaf");
 JsonDefault defaults=JsonDefault();assert(defaults.count==19);
 JsonDefault restored=json::converter<JsonDefault>::read(json::parse("{}"));assert(restored.count==19);
 json::schema schema=json::compile_schema(json::converter<Leaf::Plain>::schema());
 assert(schema.is_valid(encoded));
 return 0;
}
