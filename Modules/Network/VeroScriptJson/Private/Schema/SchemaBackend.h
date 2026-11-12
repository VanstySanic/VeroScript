// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "SchemaSemantics.h"

// These hooks affect only this module's private Schema instantiations.
#define VEROSCRIPT_JSON_SCHEMA_POLICY ::VeroScript::Json::Detail::FSchemaSemantics
#define VEROSCRIPT_JSON_SCHEMA_STEP ::VeroScript::Json::Detail::FSchemaStep
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#undef VEROSCRIPT_JSON_SCHEMA_STEP
#undef VEROSCRIPT_JSON_SCHEMA_POLICY
