// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "model.vs";
int main() {
 ImportedRecord source=ImportedRecord();
 json::value data=json::converter<ImportedRecord>::write(ref source);
 ImportedRecord copy=json::converter<ImportedRecord>::read(data);
 assert(copy.id==17);
 json::schema schema=json::compile_schema(json::converter<ImportedRecord>::schema());assert(schema.is_valid(data));
 return 0;
}
