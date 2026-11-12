// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

/**
 * Convenience umbrella for the high-level VeroScript core APIs.
 * Low-level AST/parser/compiler/VM/debugger APIs keep dedicated headers so
 * consumers do not pay for those large interfaces unless they use them.
 * For C++ embedding (Host/Session/Engine), include VeroScriptCpp.h / link VeroScriptCpp.
 * For source generation, include the selected backend header and link its AOT module.
 */
#include "VeroScriptCodegen.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptI18n.h"
#include "VeroScriptLexer.h"
#include "VeroScriptNative.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptResources.h"
#include "VeroScriptPackage.h"
#include "VeroScriptRun.h"
#include "VeroScriptVersion.h"
