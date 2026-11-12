// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

@JsonConverter(type="ImportedTable")
@JsonFactory(type="ImportedTable")
export class ImportedBase {public:int marker=12;};
export class ImportedChild : public ImportedBase {public:bool enabled=true;};
@JsonPolymorphic(base="ImportedBase",variants=(child="ImportedChild"))
export class ImportedTable {};
