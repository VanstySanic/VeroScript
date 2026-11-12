// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScript.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptAnnotation.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptCodegen.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptConsole.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptExternalDependency.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptResources.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptExternalSerialization.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitFrameServices.h"
#include "VeroScriptJitValueServices.h"
#include "VeroScriptJitCallServices.h"
#include "VeroScriptJitScalarServices.h"
#include "VeroScriptJitRuntimeServices.h"
#include "VeroScriptLexer.h"
#include "VeroScriptLocale.h"
#include "VeroScriptI18n.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptMethodBinding.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptMutation.h"
#include "VeroScriptMemory.h"
#include "VeroScriptNative.h"
#include "VeroScriptObject.h"
#include "VeroScriptPackage.h"
#include "VeroScriptParser.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptPath.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptRun.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptTypes.h"
#include "VeroScriptValue.h"
#include "VeroScriptVersion.h"
#include "VeroScriptVm.h"

#include <cstddef>
#include <type_traits>

constexpr VeroScript::FJitPolicy DefaultPolicy;
static_assert(VeroScript::GetHostPlatformInfo().Platform[0] != '\0');
static_assert(!VEROSCRIPT_PLATFORM_IOS || (!VeroScript::GetHostPlatformInfo().bAllowsNativeJit
	&& !VeroScript::GetHostPlatformInfo().bAllowsDynamicJitLibraries));
constexpr VeroScript::FJitPolicy PositionalPolicy{3, 17, 19, 23};
constexpr VeroScript::FJitPolicy DisabledPolicy{3, 17, 19, 23, false};
static_assert(DefaultPolicy.MixedHotCallThreshold == 1
	&& DefaultPolicy.MaxCompileAttempts == 4096
	&& DefaultPolicy.MaxCachedEntries == 4096
	&& DefaultPolicy.EagerCompileBudget == 4096 && DefaultPolicy.bReuseSessionCode
	&& !DefaultPolicy.bEnableControlledMixed && DefaultPolicy.Diagnostics.MaxFunctions == 0
	&& !DefaultPolicy.Diagnostics.bMeasureTime);
static_assert(PositionalPolicy.MixedHotCallThreshold == 3
	&& PositionalPolicy.MaxCompileAttempts == 17
	&& PositionalPolicy.MaxCachedEntries == 19
	&& PositionalPolicy.EagerCompileBudget == 23 && PositionalPolicy.bReuseSessionCode);
static_assert(!DisabledPolicy.bReuseSessionCode);
static_assert(std::is_standard_layout_v<VeroScript::FJitPolicy>
	&& std::is_trivially_copyable_v<VeroScript::FJitPolicy>);
static_assert(sizeof(std::size_t) != 8 || (sizeof(VeroScript::FJitPolicy) == 64
	&& offsetof(VeroScript::FJitPolicy, MaxCompileAttempts) == 8
	&& offsetof(VeroScript::FJitPolicy, MaxCachedEntries) == 16
	&& offsetof(VeroScript::FJitPolicy, EagerCompileBudget) == 24
	&& offsetof(VeroScript::FJitPolicy, bEnableControlledMixed) == 32
	&& offsetof(VeroScript::FJitPolicy, Diagnostics) == 40));

int main()
{
	return 0;
}
