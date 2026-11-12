// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptPath.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include "VeroScriptAst.h"
#include "Processing/VeroScriptSourceMetadataIO.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptBytecodeIOTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(
		const std::string& Source, const bool bAllowInternalBuiltins = false)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "<test>");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return nullptr;
		}

		VeroScript::VeroScriptParser Parser(Tokens, "<test>");
		Parser.SetAllowInternalBuiltins(bAllowInternalBuiltins);
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			std::cerr << Parser.GetError().Message << '\n';
			return nullptr;
		}

		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (Compiler.HasError() || !Module)
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}
}

int main()
{
	{
		using namespace VeroScript;
		FCompileGenerationOperation Op;
		Op.Kind=ECompileOperation::ReplaceDeclaration;
		Op.Phase=ECompileProcessingPhase::Types;
		Op.Scope.ModuleId="wire.vs";
		Op.ProcessorId="wire.processor";
		Op.GenerationId="wire.generation";
		Op.TargetId="original.type";
		Op.InputDigest="target.proof";
		Op.Source="class Value{};";
		std::vector<std::uint8_t> Wire;
		std::string WireError;
		if(!EncodeCompileOperations({Op},Wire,WireError))return Fail("encode replacement operation fixture");
		// Magic and list count precede the first operation kind. Preserve the
		// target while corrupting only that kind into an addition.
		Wire[16]=static_cast<std::uint8_t>(ECompileOperation::AddDeclaration);
		std::vector<FCompileGenerationOperation> Decoded;
		if (!Expect(!DecodeCompileOperations(Wire.data(), Wire.size(), Decoded, WireError) &&
					WireError.find("declaration addition cannot carry a replacement target") !=
						std::string::npos,
				"wire addition cannot carry a replacement target"))
		{
			return EXIT_FAILURE;
		}

		Op.Kind = ECompileOperation::AddDeclaration;
		if(!Expect(!EncodeCompileOperations({Op},Wire,WireError) && Wire.empty(),"encoder rejects addition with replacement target"))return EXIT_FAILURE;
		Op.TargetId.clear();
		Op.InputDigest.clear();
		if (!Expect(EncodeCompileOperations({Op}, Wire, WireError) &&
					DecodeCompileOperations(Wire.data(), Wire.size(), Decoded, WireError) &&
					Decoded.size() == 1 && Decoded.front().TargetId.empty(),
				"ordinary declaration addition still round-trips"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		FCompileGenerationOperation Op;
		Op.Kind=ECompileOperation::EditSource;
		Op.Phase=ECompileProcessingPhase::RawSource;
		Op.Scope.ModuleId="wire.vs";
		Op.ProcessorId="wire.processor";
		Op.GenerationId="wire.generation";
		Op.Source=std::string(128,' ');
		Op.SourceMappings.push_back({64,96,{}});
		std::vector<std::uint8_t> Wire;
		std::string WireError;
		if(!EncodeCompileOperations({Op},Wire,WireError))return Fail("encode source mapping boundary fixture");
		std::size_t MappingOffset=Wire.size();
		for (std::size_t I = 0; I + 16 <= Wire.size(); ++I)
		{
			if (Bytecode::ReadU64LE(Wire.data() + I) == 64 && Bytecode::ReadU64LE(Wire.data() + I + 8) == 96)
			{
				MappingOffset = I;
				break;
			}
		}

		if (MappingOffset == Wire.size()) return Fail("locate source mapping boundary fixture");
		// Both offsets would wrap to valid small values on a 32-bit consumer.
		for (std::size_t I = 0; I < 8; ++I)
		{
			Wire[MappingOffset + I] = static_cast<std::uint8_t>(((std::uint64_t{1} << 32) + 64) >> (I * 8));
			Wire[MappingOffset+8+I]=static_cast<std::uint8_t>(((std::uint64_t{1}<<32)+96)>>(I*8));
		}
		std::vector<FCompileGenerationOperation> Decoded;
		if(!Expect(!DecodeCompileOperations(Wire.data(),Wire.size(),Decoded,WireError),"oversized wire offsets are rejected before native-width conversion"))return EXIT_FAILURE;
	}
	const std::string Source =
		"@asset(primary) export const int ReflectedGlobal = 7, ReflectedNext = ReflectedGlobal + 1;\n"
		"@route(api) export int reflected_free(int value) { return value; }\n"
		"class C { public: mutable int cache; const int id = 7; int n; }\n"
		"public static class StaticMeta { int value = 4; const int locked = 5; int get() { return StaticMeta::value; } }\n"
		"public static struct StaticRecord { int value = 9; int get() { return StaticRecord::value; } }\n"
		"class DeletedApi { public: @hidden() int removed(int value) = delete; }\n"
		"class ReflectionBase { protected: int secret; public: virtual int run() = 0; }\n"
		"class ReflectionChild : private ReflectionBase { public: using ReflectionBase::secret; int run() override { return secret; } }\n"
		"@hidden() void removed_free() = delete;\n"
		"enum class Color : int64 { Green = 9223372036854775807i64 }\n"
		"struct S {\n"
		"    int x;\n"
		"    unsigned int u = 1U;\n"
		"    char i8 = 12;\n"
		"    unsigned char u8 = 255U;\n"
		"    short i16 = 1234;\n"
		"    unsigned short u16 = 65535U;\n"
		"    float f = 16777217.f;\n"
		"    wstring w = L\"hi\";\n"
		"    void set() { this.x = 3; }\n"
		"}\n"
		"int8 narrow8(int value) { return value; }\n"
		"float narrowf(double value) { return value; }\n"
		"void main() {\n"
		"    C c = nullptr;\n"
		"    print(c == nullptr);\n"
		"    auto s = S();\n"
		"    s.set();\n"
		"    print(s.x);\n"
		"    print(s.u + 2U);\n"
		"    print(s.i8); print(s.u8); print(s.i16); print(s.u16); print(s.f);\n"
		"    print(to_string(s.w));\n"
		"    print(2147483648U);\n"
		"    print(9223372036854775807i64);\n"
		"    print(18446744073709551615u64);\n"
		"    print(Color.Green);\n"
		"    print(narrow8(7));\n"
		"    print(narrowf(1.25));\n"
		"    double explicit_cast = cast<double>(7);\n"
		"    auto checked_optional = try_cast<int>(explicit_cast);\n"
		"    assert(checked_optional.has_value() && checked_optional.get() == 7, \"optional round-trip\");\n"
		"    string raw = r\"\\n\";\n"
		"    print(ReflectedNext); print(len(raw));\n"
		"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Original = CompileSource(Source);
	if (!Expect(static_cast<bool>(Original), "failed to compile test source"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Original->Version == VeroScript::VEROSCRIPT_BC_VERSION, "BC version should be current"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
			Original->Version == VeroScript::FSemanticVersion{0, 1, 1},
			"initial-release BC version should be exactly 0.1.1"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!Original->WStringPool.empty(), "compiled module should contain wstring pool entries"))
	{
		return EXIT_FAILURE;
	}
	bool bMutableFieldCompiled = false;
	bool bConstFieldCompiled = false;
	bool bStaticConstFieldCompiled = false;
	bool bStaticClassCompiled = false;
	bool bStaticStructCompiled = false;
	for (const VeroScript::FBytecodeTypeDesc& Type : Original->Types)
	{
		bStaticClassCompiled = bStaticClassCompiled
			|| (Type.bIsStatic && Type.Fields.empty() && Type.Name.find("StaticMeta") != std::string::npos);
		bStaticStructCompiled = bStaticStructCompiled
			|| (Type.bIsStatic && Type.Fields.empty() && Type.Name.find("StaticRecord") != std::string::npos);
		for (const VeroScript::FBytecodeFieldDesc& Field : Type.Fields)
		{
			bMutableFieldCompiled = bMutableFieldCompiled
				|| (Field.Name == "cache" && Field.bIsMutable);
			bConstFieldCompiled = bConstFieldCompiled
				|| (Field.Name == "id" && Field.bIsConst);
		}
		if (Type.Name.find("StaticMeta") != std::string::npos)
		{
			for (const VeroScript::FBytecodeFieldDesc& Field : Type.StaticFields)
			{
				bStaticConstFieldCompiled |= Field.Name == "locked" && Field.bIsConst;
			}
		}
	}
	if (!Expect(bMutableFieldCompiled, "mutable field flag should be compiled"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bConstFieldCompiled, "const field flag should be compiled"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bStaticConstFieldCompiled, "static const field flag should be compiled"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bStaticClassCompiled, "static class metadata should be compiled without instance fields"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bStaticStructCompiled, "static struct metadata should be compiled without instance fields"))
	{
		return EXIT_FAILURE;
	}
	bool bHasU32 = false;
	bool bHasI64 = false;
	bool bHasU64 = false;
	bool bHasEnum = false;
	bool bHasWStr = false;
	bool bHasF32 = false;
	bool bHasI8 = false;
	bool bHasU8 = false;
	bool bHasI16 = false;
	bool bHasU16 = false;
	for (const VeroScript::FBytecodeConstant& Constant : Original->Constants)
	{
		if (Constant.Kind == VeroScript::EConstantKind::U32)
		{
			bHasU32 = true;
		}
		if (Constant.Kind == VeroScript::EConstantKind::WStr)
		{
			bHasWStr = true;
		}
		bHasI64 = bHasI64 || Constant.Kind == VeroScript::EConstantKind::I64;
		bHasU64 = bHasU64 || Constant.Kind == VeroScript::EConstantKind::U64;
		bHasEnum = bHasEnum || Constant.Kind == VeroScript::EConstantKind::Enum;
		bHasF32 = bHasF32 || Constant.Kind == VeroScript::EConstantKind::F32;
		bHasI8 = bHasI8 || Constant.Kind == VeroScript::EConstantKind::I8;
		bHasU8 = bHasU8 || Constant.Kind == VeroScript::EConstantKind::U8;
		bHasI16 = bHasI16 || Constant.Kind == VeroScript::EConstantKind::I16;
		bHasU16 = bHasU16 || Constant.Kind == VeroScript::EConstantKind::U16;
	}
	for (const VeroScript::FBytecodeTypeDesc& TypeDesc : Original->Types)
	{
		for (const VeroScript::FBytecodeFieldDesc& Field : TypeDesc.Fields)
		{
			if (Field.bHasDefault)
			{
				if (Field.DefaultValue.Kind == VeroScript::EConstantKind::U32)
				{
					bHasU32 = true;
				}
				if (Field.DefaultValue.Kind == VeroScript::EConstantKind::WStr)
				{
					bHasWStr = true;
				}
				bHasF32 = bHasF32 || Field.DefaultValue.Kind == VeroScript::EConstantKind::F32;
				bHasI8 = bHasI8 || Field.DefaultValue.Kind == VeroScript::EConstantKind::I8;
				bHasU8 = bHasU8 || Field.DefaultValue.Kind == VeroScript::EConstantKind::U8;
				bHasI16 = bHasI16 || Field.DefaultValue.Kind == VeroScript::EConstantKind::I16;
				bHasU16 = bHasU16 || Field.DefaultValue.Kind == VeroScript::EConstantKind::U16;
			}
		}
	}
	if (!Expect(bHasU32, "compiled module should contain U32 constants"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bHasWStr, "compiled module should contain WStr constants"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bHasI64 && bHasU64 && bHasEnum,
			"compiled module should contain I64, U64, and typed enum constants"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
			bHasF32 && bHasI8 && bHasU8 && bHasI16 && bHasU16,
			"compiled module should retain every primitive-width constant kind"))
	{
		std::cerr << "f32=" << bHasF32 << " i8=" << bHasI8 << " u8=" << bHasU8
			<< " i16=" << bHasI16 << " u16=" << bHasU16 << '\n';
		return EXIT_FAILURE;
	}

	const std::uint64_t Fingerprint = 0xC0FFEE1234567890ull;
	{
		std::unique_ptr<VeroScript::FBytecodeModule> InvalidBuiltinArity =
			CompileSource("void main() { assert(true, \"detail\"); }\n");
		if (!Expect(
				static_cast<bool>(InvalidBuiltinArity),
				"assert source should compile for builtin arity validation"))
		{
			return EXIT_FAILURE;
		}
		bool bMutatedAssert = false;
		for (VeroScript::FBytecodeFunction& Function : InvalidBuiltinArity->Functions)
		{
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Opcode = static_cast<VeroScript::EOpcode>(Function.Code[Ip]);
				if (Opcode == VeroScript::EOpcode::CallBuiltin
					&& Function.Code[Ip + 1]
						== static_cast<std::uint8_t>(VeroScript::EBuiltinId::Assert))
				{
					Function.Code[Ip + 2] = 3;
					bMutatedAssert = true;
					break;
				}
				Ip += VeroScript::Bytecode::InstructionSize(Opcode);
			}
			if (bMutatedAssert)
			{
				break;
			}
		}
		if (!Expect(bMutatedAssert, "compiled assert should contain CallBuiltin"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					*InvalidBuiltinArity,
					Fingerprint,
					Bytes,
					Error)
					&& Error.find("invalid argument count for builtin 'assert'")
						!= std::string::npos,
				"bytecode validation should reject invalid assert arity"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		const std::string UserCache = VeroScript::Runtime::GetUserCacheDirectory();
		if (!UserCache.empty())
		{
			const std::filesystem::path CachePath(UserCache);
			if (!Expect(
					CachePath.filename() == "cache"
						&& CachePath.parent_path().filename() == "VeroScript",
					"platform user cache should end in VeroScript/cache"))
			{
				std::cerr << UserCache << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	#if !defined(_WIN32) && !defined(__APPLE__)
	{
		const auto SavedXdgCache = VeroScript::Environment::GetUtf8("XDG_CACHE_HOME");
		const bool bRootCache = VeroScript::Environment::SetUtf8("XDG_CACHE_HOME", "/")
			&& VeroScript::Runtime::GetUserCacheDirectory() == "/VeroScript/cache";
		const bool bLiteralCache = VeroScript::Environment::SetUtf8(
			"XDG_CACHE_HOME", "/tmp/vero\\cache/")
			&& VeroScript::Runtime::GetUserCacheDirectory() == "/tmp/vero\\cache/VeroScript/cache";
		const bool bRestored = SavedXdgCache
			? VeroScript::Environment::SetUtf8("XDG_CACHE_HOME", *SavedXdgCache)
			: VeroScript::Environment::UnsetUtf8("XDG_CACHE_HOME");
		if (!Expect(bRestored && bRootCache && bLiteralCache,
			"XDG cache preserves the filesystem root and literal POSIX backslashes"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	{
		const std::string Resolved = VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
			"examples/hello.vs");
		const std::filesystem::path Expected =
			std::filesystem::path("examples") / ".verocache" / "hello.vs.vsbc";
		if (!Expect(std::filesystem::path(Resolved) == Expected, "GetCachePathForScript should use .verocache"))
		{
			std::cerr << Resolved << '\n';
			return EXIT_FAILURE;
		}
		const std::string Custom = VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
			"examples/hello.vs",
			"D:/cache");
		const std::filesystem::path CustomPath(Custom);
		if (!Expect(
				CustomPath.filename() == "hello.vs.vsbc"
					&& CustomPath.parent_path().parent_path() == std::filesystem::path("D:/cache")
					&& CustomPath.parent_path().filename().string().size() == 16,
				"GetCachePathForScript custom root uses path-hash subdirectory"))
		{
			std::cerr << Custom << '\n';
			return EXIT_FAILURE;
		}
		const std::string CustomOther = VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
			"other/hello.vs",
			"D:/cache");
		if (!Expect(
				Custom != CustomOther,
				"custom cache root disambiguates same basename from different dirs"))
		{
			std::cerr << Custom << " vs " << CustomOther << '\n';
			return EXIT_FAILURE;
		}
	}
	const VeroScript::Tests::FScopedTemporaryDirectory TemporaryDirectory(
		"VeroScript-bytecode-io-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
		"failed to create unique temporary directory"))
	{
		return EXIT_FAILURE;
	}
	const std::filesystem::path CacheRoot = TemporaryDirectory.Get() / "cache";
	std::error_code RemoveError;
	std::filesystem::remove_all(CacheRoot, RemoveError);
	const std::filesystem::path CachePath = CacheRoot / ".verocache" / "io-test.vsbc";

	{
		const std::string MissingPath =
			(CacheRoot / "missing-filesystem-entry").generic_string();
		auto FsModule = CompileSource(
			"void main() { assert(!__is_file(\"" + MissingPath + "\")); "
			"assert(!__is_directory(\"" +
				MissingPath + "\")); "
			"assert(!__remove_dir(\"" +
				MissingPath + "\")); }",
			true);
		if (!Expect(static_cast<bool>(FsModule),
			"filesystem builtins compile for bytecode roundtrip"))
		{
			return EXIT_FAILURE;
		}
		const std::filesystem::path FsCache = CacheRoot / ".verocache" / "fs-builtins.vsbc";
		if (!Expect(VeroScript::VeroScriptBytecodeIO::WriteFile(
				FsCache.string(), *FsModule, Fingerprint),
			"filesystem builtins serialize"))
		{
			return EXIT_FAILURE;
		}
		std::string FsReadError;
		auto FsLoaded = VeroScript::VeroScriptBytecodeIO::ReadFile(
			FsCache.string(), Fingerprint, FsReadError);
		if (!Expect(static_cast<bool>(FsLoaded), "filesystem builtins deserialize"))
		{
			std::cerr << FsReadError << '\n';
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(Vm.Execute(*FsLoaded, VeroScript::ERunMode::Script),
			"filesystem builtins execute after bytecode roundtrip"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!Expect(
			VeroScript::VeroScriptBytecodeIO::WriteFile(CachePath.string(), *Original, Fingerprint),
			"failed to write bytecode cache"))
	{
		return EXIT_FAILURE;
	}

	std::string ReadError;
	{
		auto Probe = CompileSource("struct ReadProbe {}; void main(){}");
		if (!Expect(bool(Probe) && !Probe->Types.empty(), "compile annotation diagnostic probe"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FDeclarationAnnotation Annotation;
		Annotation.Name = "@BytecodeReadDiagnosticProbe";
		Probe->Types.front().DeclMeta.Annotations.push_back(Annotation);
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!Expect(VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Probe, 0, Bytes, Error),
				"serialize annotation diagnostic probe"))
		{
			return EXIT_FAILURE;
		}

		const auto Name =
			std::search(Bytes.begin(), Bytes.end(), Annotation.Name.begin(), Annotation.Name.end());
		if (!Expect(Name != Bytes.end(), "locate structured annotation name"))
		{
			return EXIT_FAILURE;
		}

		Bytes[static_cast<std::size_t>(Name - Bytes.begin())] = 0xFF;
		if (!Expect(!VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
						VeroScript::FByteView(Bytes), std::nullopt, Error) &&
					!Error.empty(),
				"malformed structured annotations must produce a diagnostic"))
		{
			return EXIT_FAILURE;
		}
	}
	std::unique_ptr<VeroScript::FBytecodeModule> Loaded =
		VeroScript::VeroScriptBytecodeIO::ReadFile(CachePath.string(), Fingerprint, ReadError);
	if (!Expect(static_cast<bool>(Loaded), "failed to read bytecode cache"))
	{
		std::cerr << ReadError << '\n';
		return EXIT_FAILURE;
	}
	{
		std::vector<std::uint8_t> LegacyLayout;
		std::string Error;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					*Original, Fingerprint, LegacyLayout, Error)
					&& LegacyLayout.size() >= 4,
				"current bytecode layout should serialize"))
		{
			return EXIT_FAILURE;
		}
		LegacyLayout[0] = 'V';
		LegacyLayout[1] = 'R';
		LegacyLayout[2] = 'S';
		for (const std::uint8_t Layout : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
		{
			LegacyLayout[3] = Layout; // Reject former development markers, even at the same semantic version.
			if (!Expect(
					!VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
						VeroScript::FByteView(LegacyLayout), Fingerprint, Error)
						&& Error.find("magic") != std::string::npos,
				"old same-version bytecode layout must be rejected"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		std::vector<std::uint8_t> CurrentBytes;
		std::string Error;
		if (!Expect(VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
				*Original, Fingerprint, CurrentBytes, Error) &&
					CurrentBytes.size() >= 10,
			"current bytecode version should serialize"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::VEROSCRIPT_BC_VERSION == VeroScript::VEROSCRIPT_VERSION
			&& CurrentBytes[0] == 'V' && CurrentBytes[1] == 'S' && CurrentBytes[2] == 'B'
			&& CurrentBytes[3] == 'C'
			&& CurrentBytes[4] == 0 && CurrentBytes[5] == 0
			&& CurrentBytes[6] == 1 && CurrentBytes[7] == 0
			&& CurrentBytes[8] == 1 && CurrentBytes[9] == 0,
			"fixed VSBC identifier and product-aligned 0.1.1 format header"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::FSemanticVersion UnsupportedVersion : {
				 VeroScript::FSemanticVersion{1, 0, 0}, VeroScript::FSemanticVersion{0, 1, 0}})
		{
			std::vector<std::uint8_t> UnsupportedBytes = CurrentBytes;
			const std::uint16_t Parts[] = {
				UnsupportedVersion.Major, UnsupportedVersion.Minor, UnsupportedVersion.Patch};
			for (std::size_t Index = 0; Index < 3; ++Index)
			{
				UnsupportedBytes[4 + Index * 2] = static_cast<std::uint8_t>(Parts[Index]);
				UnsupportedBytes[5 + Index * 2] = static_cast<std::uint8_t>(Parts[Index] >> 8);
			}
			if (!Expect(!VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
					VeroScript::FByteView(UnsupportedBytes), Fingerprint, Error)
					&& Error.find("version mismatch") != std::string::npos,
				"bytecode from an unsupported development version must be rejected"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	if (!Expect(
			Loaded->WStringPool.size() == Original->WStringPool.size(),
			"loaded wstring pool size mismatch"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
		!Loaded->WStringPool.empty() && Loaded->WStringPool.front() == Original->WStringPool.front(),
		"loaded wstring pool content mismatch"))
	{
		return EXIT_FAILURE;
	}
	const bool bReflectionRoundTrip =
		Loaded->EntryModulePath == Original->EntryModulePath
		&& Loaded->GlobalMetadata.size() == Loaded->GlobalNames.size();
	if (!bReflectionRoundTrip)
	{
		std::cerr << "entry path: '" << Original->EntryModulePath << "' -> '"
			<< Loaded->EntryModulePath << "', globals=" << Loaded->GlobalNames.size()
			<< ", metadata=" << Loaded->GlobalMetadata.size() << '\n';
	}
	if (!Expect(
		bReflectionRoundTrip,
		"module path and global reflection table must round-trip"))
	{
		return EXIT_FAILURE;
	}
	bool bProtectedField = false;
	bool bPrivateBase = false;
	bool bPublicUsing = false;
	bool bPublicAbstractMethod = false;
	for (const VeroScript::FBytecodeTypeDesc& Type : Loaded->Types)
	{
		if (Type.Name.find("ReflectionBase") != std::string::npos)
		{
			for (const VeroScript::FBytecodeFieldDesc& Field : Type.Fields)
			{
				bProtectedField |= Field.Name == "secret"
					&& Field.Access == VeroScript::EMemberAccess::Protected;
			}
		}
		if (Type.Name.find("ReflectionChild") != std::string::npos)
		{
			bPrivateBase = Type.BaseInheritanceAccess.size() == 1
				&& Type.BaseInheritanceAccess[0] == VeroScript::EMemberAccess::Private;
			for (const VeroScript::FBytecodeTypeDesc::FUsingMember& Using : Type.UsingMembers)
			{
				bPublicUsing |= Using.BaseTypeName == "ReflectionBase"
					&& Using.MemberName == "secret"
					&& Using.Access == VeroScript::EMemberAccess::Public;
			}
		}
	}
	for (const VeroScript::FBytecodeFunction& Function : Loaded->Functions)
	{
		bPublicAbstractMethod |= Function.Name.find("ReflectionBase::run")
			!= std::string::npos
			&& Function.bIsAbstract
			&& Function.Access == VeroScript::EMemberAccess::Public;
	}
	if (!Expect(bProtectedField && bPrivateBase && bPublicUsing
			&& bPublicAbstractMethod,
		"executable reflection access metadata must round-trip"))
	{
		return EXIT_FAILURE;
	}
	const bool bStaticClassRoundTrip = std::any_of(
		Loaded->Types.begin(),
		Loaded->Types.end(),
		[](const VeroScript::FBytecodeTypeDesc& Type)
		{
			return Type.bIsStatic && Type.Fields.empty()
					&& Type.Name.find("StaticMeta") != std::string::npos;
		});
	if (!Expect(bStaticClassRoundTrip, "static class metadata must round-trip"))
	{
		return EXIT_FAILURE;
	}
	const bool bStaticStructRoundTrip = std::any_of(
		Loaded->Types.begin(),
		Loaded->Types.end(),
		[](const VeroScript::FBytecodeTypeDesc& Type)
		{
			return Type.bIsStatic && Type.Fields.empty()
					&& Type.Name.find("StaticRecord") != std::string::npos;
		});
	if (!Expect(bStaticStructRoundTrip, "static struct metadata must round-trip"))
	{
		return EXIT_FAILURE;
	}
	bool bMutableFieldRoundTrip = false;
	bool bConstFieldRoundTrip = false;
	bool bStaticConstFieldRoundTrip = false;
	std::string MutableFieldOwner;
	for (const VeroScript::FBytecodeTypeDesc& Type : Loaded->Types)
	{
		for (const VeroScript::FBytecodeFieldDesc& Field : Type.Fields)
		{
			bMutableFieldRoundTrip = bMutableFieldRoundTrip
				|| (Field.Name == "cache" && Field.bIsMutable);
			bConstFieldRoundTrip = bConstFieldRoundTrip
				|| (Field.Name == "id" && Field.bIsConst);
			if (Field.Name == "cache" && Field.bIsMutable)
			{
				MutableFieldOwner = Type.Name;
			}
		}
		if (Type.Name.find("StaticMeta") != std::string::npos)
		{
			for (const VeroScript::FBytecodeFieldDesc& Field : Type.StaticFields)
			{
				bStaticConstFieldRoundTrip |= Field.Name == "locked" && Field.bIsConst;
			}
		}
	}
	if (!Expect(bMutableFieldRoundTrip, "mutable field flag should round-trip"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bConstFieldRoundTrip, "const field flag should round-trip"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(bStaticConstFieldRoundTrip, "static const field flag should round-trip"))
	{
		return EXIT_FAILURE;
	}
	std::vector<VeroScript::FLoadedFieldInfo> MutableFields;
	const bool bMutableFieldReflected =
		!MutableFieldOwner.empty()
		&& VeroScript::ModuleMeta::EnumerateFields(
			*Loaded, MutableFieldOwner, MutableFields, /*bDeclaredOnly=*/true)
		&& std::any_of(
			MutableFields.begin(),
			MutableFields.end(),
			[](const VeroScript::FLoadedFieldInfo& Field)
			{
				return Field.Name == "cache" && Field.bIsMutable && !Field.bIsStatic;
			});
	if (!Expect(bMutableFieldReflected, "mutable field should be exposed by reflection"))
	{
		return EXIT_FAILURE;
	}
	bool bFoundReflectedGlobal = false;
	bool bFoundReflectedNext = false;
	for (std::size_t Index = 0; Index < Loaded->GlobalNames.size(); ++Index)
	{
		const bool bIsReflectedGlobal = Loaded->GlobalNames[Index] == "ReflectedGlobal";
		const bool bIsReflectedNext = Loaded->GlobalNames[Index] == "ReflectedNext";
		if (!bIsReflectedGlobal && !bIsReflectedNext)
		{
			continue;
		}
		const VeroScript::FBytecodeGlobalMeta& Meta = Loaded->GlobalMetadata[Index];
		bool bHasAssetAnnotation = false;
		const bool bHasExpectedMeta = Meta.ModulePath == "<test>"
			&& Meta.DeclaredTypeName == "int"
			&& Meta.bConst && Meta.bExported && !Meta.bStaticMember;
		for (const auto& Pair : Meta.DeclMeta)
		{
			bHasAssetAnnotation = bHasAssetAnnotation
				|| (Pair.first == "__annot" && Pair.second == "@asset");
		}
		if (bIsReflectedGlobal)
		{
			bFoundReflectedGlobal = bHasExpectedMeta && bHasAssetAnnotation;
		}
		else
		{
			bFoundReflectedNext = bHasExpectedMeta && bHasAssetAnnotation;
		}
	}
	if (!Expect(
			bFoundReflectedGlobal && bFoundReflectedNext,
			"grouped global declaration metadata must round-trip"))
	{
		return EXIT_FAILURE;
	}
	std::vector<VeroScript::FFreeFunctionInfo> ReflectedFunctions;
	VeroScript::ModuleMeta::EnumerateFreeFunctions(
		*Loaded, ReflectedFunctions, false);
	bool bFoundReflectedFunction = false;
	for (const VeroScript::FFreeFunctionInfo& Function : ReflectedFunctions)
	{
		bFoundReflectedFunction = bFoundReflectedFunction
			|| (Function.Name == "reflected_free"
				&& Function.ModulePath == "<test>"
				&& Function.bExported);
	}
	if (!Expect(
		bFoundReflectedFunction,
		"free-function module identity must round-trip without debug metadata"))
	{
		return EXIT_FAILURE;
	}
	std::size_t DeletedFunctionCount = 0;
	for (const VeroScript::FBytecodeFunction& Function : Loaded->Functions)
	{
		if (!Function.bDeleted)
		{
			continue;
		}
		++DeletedFunctionCount;
		bool bHasAnnotation = false;
		for (const auto& Meta : Function.DeclMeta)
		{
			bHasAnnotation = bHasAnnotation
				|| (Meta.first == "__annot" && Meta.second == "@hidden");
		}
		if (!Expect(
			Function.Code.empty() && bHasAnnotation,
			"deleted bytecode declarations must preserve metadata without code"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!Expect(
		DeletedFunctionCount == 2,
		"deleted method and free function flags must round-trip through bytecode"))
	{
		return EXIT_FAILURE;
	}

	std::ostringstream Output;
	VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
	if (!Expect(Vm.Execute(*Loaded, VeroScript::ERunMode::Script), "failed to execute loaded bytecode"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
			Output.str()
				== "true\n3\n3\n12\n255\n1234\n65535\n16777216\nhi\n"
					"2147483648\n9223372036854775807\n"
					"18446744073709551615\nColor(9223372036854775807)\n7\n1.25\n8\n2\n",
			"loaded bytecode produced unexpected output"))
	{
		std::cerr << Output.str();
		return EXIT_FAILURE;
	}

	{
		const std::string ReferenceSource =
			"int Value = 4;\n"
			"int& value_ref() { return Value; }\n"
			"const int& read_ref() { return Value; }\n"
			"void main() { value_ref() = 6; print(read_ref()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> ReferenceModule =
			CompileSource(ReferenceSource);
		if (!Expect(
				static_cast<bool>(ReferenceModule),
				"reference-return source should compile for bytecode roundtrip"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> ReferenceBytes;
		std::string ReferenceError;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					*ReferenceModule,
					Fingerprint,
					ReferenceBytes,
					ReferenceError),
				"reference-return bytecode should serialize"))
		{
			std::cerr << ReferenceError << '\n';
			return EXIT_FAILURE;
		}
		std::unique_ptr<VeroScript::FBytecodeModule> ReferenceLoaded =
			VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
				VeroScript::FByteView(ReferenceBytes),
				Fingerprint,
				ReferenceError);
		if (!Expect(
				static_cast<bool>(ReferenceLoaded),
				"reference-return bytecode should deserialize"))
		{
			std::cerr << ReferenceError << '\n';
			return EXIT_FAILURE;
		}
		std::size_t ReferenceFunctionCount = 0;
		for (const VeroScript::FBytecodeFunction& Function : ReferenceLoaded->Functions)
		{
			if (!Function.bIsMethod && Function.bReturnsRef)
			{
				++ReferenceFunctionCount;
			}
		}
		if (!Expect(
				ReferenceFunctionCount == 2,
				"mutable and const reference-return flags should roundtrip"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream ReferenceOutput;
		VeroScript::VeroScriptVm ReferenceVm(ReferenceOutput);
		VeroScript::Test::Configure(ReferenceVm);
		if (!Expect(
				ReferenceVm.Execute(*ReferenceLoaded, VeroScript::ERunMode::Script)
					&& ReferenceOutput.str() == "6\n",
				"deserialized reference-return bytecode should execute"))
		{
			std::cerr << ReferenceVm.GetError().Message << ReferenceOutput.str();
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidReferenceSignature = *ReferenceModule;
		for (VeroScript::FBytecodeFunction& Function : InvalidReferenceSignature.Functions)
		{
			if (Function.bReturnsRef)
			{
				Function.bReturnsRef = false;
				break;
			}
		}
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidReferenceSignature,
					ReferenceError),
				"reference-return flag/signature mismatch should be rejected"))
		{
			return EXIT_FAILURE;
		}
	}
	const auto Color = std::find_if(
		Loaded->Enums.begin(), Loaded->Enums.end(),
		[](const auto& Enum)
		{
			return Enum.Name == "Color" || (Enum.Name.size() > 6 && Enum.Name.compare(Enum.Name.size() - 6, 6,
																					  "\x1f"
																					  "Color") == 0);
		});
	if (!Expect(
			Color != Loaded->Enums.end()
				&& Color->UnderlyingTypeName == "int64"
				&& Color->Members.size() == 1
				&& Color->Members[0].Value
					== std::numeric_limits<std::int64_t>::max(),
			"enum underlying type and full-width member value should round-trip"))
	{
		return EXIT_FAILURE;
	}

	std::string StaleError;
	const std::unique_ptr<VeroScript::FBytecodeModule> Stale =
		VeroScript::VeroScriptBytecodeIO::ReadFile(CachePath.string(), Fingerprint + 1, StaleError);
	if (!Expect(!Stale, "stale fingerprint should reject cache"))
	{
		return EXIT_FAILURE;
	}

	std::filesystem::remove_all(CacheRoot, RemoveError);

	{
		const std::string InheritSource =
			"class Animal {\n"
			"    public:\n"
			"    virtual string Speak() { return \"a\"; }\n"
			"}\n"
			"class Dog : public Animal{\n"
			"    public:\n"
			"    string Speak() { return \"d\"; }\n"
			"}\n"
			"void main() {\n"
			"    Animal a = Dog();\n"
			"    print(a.Speak());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> InheritModule = CompileSource(InheritSource);
		if (!Expect(static_cast<bool>(InheritModule), "failed to compile inheritance source"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t DogIndex = 0xFFFF;
		std::uint16_t AnimalIndex = 0xFFFF;
		bool bFoundVirtual = false;
		const auto EndsWithTypeName = [](const std::string& Name, const char* Simple) -> bool
		{
			if (Name == Simple)
			{
				return true;
			}
			const std::string Suffix = std::string(1, '\x1F') + Simple;
			return Name.size() >= Suffix.size()
				&& Name.compare(Name.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
		};
		for (std::size_t Index = 0; Index < InheritModule->Types.size(); ++Index)
		{
			const VeroScript::FBytecodeTypeDesc& TypeDesc = InheritModule->Types[Index];
			if (EndsWithTypeName(TypeDesc.Name, "Animal"))
			{
				AnimalIndex = static_cast<std::uint16_t>(Index);
			}
			if (EndsWithTypeName(TypeDesc.Name, "Dog"))
			{
				DogIndex = static_cast<std::uint16_t>(Index);
			}
		}
		if (!Expect(AnimalIndex != 0xFFFF && DogIndex != 0xFFFF, "expected Animal and Dog types"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				InheritModule->Types[DogIndex].BaseTypeIndices.size() == 1
					&& InheritModule->Types[DogIndex].BaseTypeIndices[0] == AnimalIndex,
				"Dog BaseTypeIndices should point at Animal"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::FBytecodeFunction& Function : InheritModule->Functions)
		{
			if (Function.bIsVirtual)
			{
				bFoundVirtual = true;
			}
		}
		if (!Expect(bFoundVirtual, "expected virtual method flag on bytecode"))
		{
			return EXIT_FAILURE;
		}

		const std::filesystem::path InheritCache = CacheRoot / ".verocache" / "inherit.vsbc";
		std::filesystem::create_directories(InheritCache.parent_path());
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::WriteFile(
					InheritCache.string(),
						*InheritModule,
					Fingerprint),
				"failed to write inheritance bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string InheritReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> InheritLoaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				InheritCache.string(),
				Fingerprint,
				InheritReadError);
		if (!Expect(static_cast<bool>(InheritLoaded), "failed to read inheritance bytecode"))
		{
			std::cerr << InheritReadError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				InheritLoaded->Types[DogIndex].BaseTypeIndices.size() == 1
					&& InheritLoaded->Types[DogIndex].BaseTypeIndices[0] == AnimalIndex,
				"loaded Dog BaseTypeIndices mismatch"))
		{
			return EXIT_FAILURE;
		}
		bool bLoadedVirtual = false;
		for (const VeroScript::FBytecodeFunction& Function : InheritLoaded->Functions)
		{
			if (Function.bIsVirtual)
			{
				bLoadedVirtual = true;
			}
		}
		if (!Expect(bLoadedVirtual, "loaded bytecode lost bIsVirtual"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream InheritOutput;
		VeroScript::VeroScriptVm InheritVm(InheritOutput);
		VeroScript::Test::Configure(InheritVm);
		if (!Expect(
				InheritVm.Execute(*InheritLoaded, VeroScript::ERunMode::Script),
				"failed to execute inheritance bytecode"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(InheritOutput.str() == "d\n", "inheritance bytecode output mismatch"))
		{
			std::cerr << InheritOutput.str();
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(CacheRoot, RemoveError);
	}

	{
		const std::string InterfaceSource =
			"interface IGreet {\n"
			"    virtual void greet() = 0;\n"
			"}\n"
			"class Person : public IGreet{\n"
			"    public:\n"
			"    void greet() { print(\"hi\"); }\n"
			"}\n"
			"void main() {\n"
			"    IGreet g = Person();\n"
			"    g.greet();\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> InterfaceModule = CompileSource(InterfaceSource);
		if (!Expect(static_cast<bool>(InterfaceModule), "failed to compile interface BC22 source"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				InterfaceModule->Version == VeroScript::VEROSCRIPT_BC_VERSION,
				"interface module BC version"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(InterfaceModule->Interfaces.size() == 1, "expected one interface in table"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				InterfaceModule->Interfaces[0].BaseInterfaceIndices.empty(),
				"IGreet should have no parent interface"))
		{
			return EXIT_FAILURE;
		}
		bool bHasImplemented = false;
		const auto EndsWithTypeName = [](const std::string& Name, const char* Simple) -> bool
		{
			if (Name == Simple)
			{
				return true;
			}
			const std::string Suffix = std::string(1, '\x1F') + Simple;
			return Name.size() >= Suffix.size()
				&& Name.compare(Name.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
		};
		for (const VeroScript::FBytecodeTypeDesc& TypeDesc : InterfaceModule->Types)
		{
			if (EndsWithTypeName(TypeDesc.Name, "Person")
				&& !TypeDesc.ImplementedInterfaceIndices.empty())
			{
				bHasImplemented = true;
				if (!Expect(
						TypeDesc.ImplementedInterfaceIndices.front() == 0,
						"Person should implement interface index 0"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		if (!Expect(bHasImplemented, "Person missing ImplementedInterfaceIndices"))
		{
			return EXIT_FAILURE;
		}
		bool bHasPureGreet = false;
		for (const VeroScript::FBytecodeFunction& Function : InterfaceModule->Functions)
		{
			if (Function.Name.find("IGreet::greet#") != std::string::npos)
			{
				bHasPureGreet = Function.bIsAbstract && Function.bIsVirtual;
			}
		}
		if (!Expect(bHasPureGreet, "pure interface method flags missing"))
		{
			return EXIT_FAILURE;
		}

		std::filesystem::remove_all(CacheRoot, RemoveError);
		std::filesystem::create_directories(CacheRoot / ".verocache");
		const std::filesystem::path InterfaceCache = CacheRoot / ".verocache" / "iface.vsbc";
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::WriteFile(
					InterfaceCache.string(),
						*InterfaceModule,
					Fingerprint),
				"failed to write interface bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string InterfaceReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> InterfaceLoaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				InterfaceCache.string(),
				Fingerprint,
				InterfaceReadError);
		if (!Expect(static_cast<bool>(InterfaceLoaded), "failed to read interface bytecode"))
		{
			std::cerr << InterfaceReadError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				InterfaceLoaded->Interfaces.size() == InterfaceModule->Interfaces.size(),
				"loaded Interfaces size mismatch"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				InterfaceLoaded->Interfaces[0].Name == InterfaceModule->Interfaces[0].Name,
				"loaded interface name mismatch"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				InterfaceLoaded->Types.size() == InterfaceModule->Types.size(),
				"loaded Types size mismatch"))
		{
			return EXIT_FAILURE;
		}
		for (std::size_t Index = 0; Index < InterfaceLoaded->Types.size(); ++Index)
		{
			if (!Expect(
					InterfaceLoaded->Types[Index].ImplementedInterfaceIndices
						== InterfaceModule->Types[Index].ImplementedInterfaceIndices,
					"loaded ImplementedInterfaceIndices mismatch"))
			{
				return EXIT_FAILURE;
			}
		}
		std::ostringstream InterfaceOutput;
		VeroScript::VeroScriptVm InterfaceVm(InterfaceOutput);
		VeroScript::Test::Configure(InterfaceVm);
		if (!Expect(
				InterfaceVm.Execute(*InterfaceLoaded, VeroScript::ERunMode::Script),
				"failed to execute interface bytecode"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(InterfaceOutput.str() == "hi\n", "interface BC22 roundtrip output"))
		{
			std::cerr << InterfaceOutput.str();
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(CacheRoot, RemoveError);
	}

	{
		const std::string PureDestructorSource =
			"class AbstractOwner { public: virtual ~AbstractOwner() = 0; }\n"
			"AbstractOwner::~AbstractOwner() {}\n"
			"void main() { print(0); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> PureDestructorModule =
			CompileSource(PureDestructorSource);
		if (!Expect(
				static_cast<bool>(PureDestructorModule),
				"failed to compile pure virtual destructor bytecode"))
		{
			return EXIT_FAILURE;
		}
		bool bFoundAbstractOwner = false;
		for (const VeroScript::FBytecodeTypeDesc& Type : PureDestructorModule->Types)
		{
			if (Type.Name == "AbstractOwner"
				|| (Type.Name.size() >= 13
					&& Type.Name.compare(Type.Name.size() - 13, 13, "AbstractOwner") == 0))
			{
				bFoundAbstractOwner = Type.bDestructorVirtual
					&& Type.bDestructorPureVirtual && Type.bIsAbstract;
			}
		}
		if (!Expect(
				bFoundAbstractOwner,
				"pure virtual destructor must retain virtual abstract type metadata"))
		{
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> PureDestructorBytes;
		std::string PureDestructorError;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					*PureDestructorModule,
					Fingerprint,
					PureDestructorBytes,
					PureDestructorError),
				"pure virtual destructor bytecode serialization"))
		{
			std::cerr << PureDestructorError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const std::string MultiParentSource =
			"interface IA { virtual void a() = 0; }\n"
			"interface IB { virtual void b() = 0; }\n"
			"interface IC : IA, IB {\n"
			"    void a() { print(\"a\"); }\n"
			"    void b() { print(\"b\"); }\n"
			"}\n"
			"class C : public IC {}\n"
			"void main() {\n"
			"    IC x = C();\n"
			"    x.a();\n"
			"    x.b();\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> MultiModule = CompileSource(MultiParentSource);
		if (!Expect(static_cast<bool>(MultiModule), "failed to compile multi-parent interface BC"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(MultiModule->Interfaces.size() == 3, "expected IA, IB, IC in Interfaces table"))
		{
			return EXIT_FAILURE;
		}
		std::size_t IcIndex = static_cast<std::size_t>(-1);
		for (std::size_t Index = 0; Index < MultiModule->Interfaces.size(); ++Index)
		{
			const std::string& Name = MultiModule->Interfaces[Index].Name;
			const bool bLeafIc = Name == "IC"
				|| (Name.size() >= 3 && Name.compare(Name.size() - 3, 3, "\x1FIC") == 0);
			if (bLeafIc)
			{
				IcIndex = Index;
				break;
			}
		}
		if (!Expect(IcIndex != static_cast<std::size_t>(-1), "IC interface index"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				MultiModule->Interfaces[IcIndex].BaseInterfaceIndices.size() == 2,
				"IC should list two parent indices"))
		{
			return EXIT_FAILURE;
		}

		std::filesystem::remove_all(CacheRoot, RemoveError);
		std::filesystem::create_directories(CacheRoot / ".verocache");
		const std::filesystem::path MultiCache = CacheRoot / ".verocache" / "multi_iface.vsbc";
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::WriteFile(
					MultiCache.string(),
						*MultiModule,
					Fingerprint),
				"failed to write multi-parent interface bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string MultiReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> MultiLoaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				MultiCache.string(),
				Fingerprint,
				MultiReadError);
		if (!Expect(static_cast<bool>(MultiLoaded), "failed to read multi-parent interface bytecode"))
		{
			std::cerr << MultiReadError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				MultiLoaded->Interfaces[IcIndex].BaseInterfaceIndices
					== MultiModule->Interfaces[IcIndex].BaseInterfaceIndices,
				"multi-parent BaseInterfaceIndices roundtrip"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream MultiOutput;
		VeroScript::VeroScriptVm MultiVm(MultiOutput);
		VeroScript::Test::Configure(MultiVm);
		if (!Expect(
				MultiVm.Execute(*MultiLoaded, VeroScript::ERunMode::Script),
				"failed to execute multi-parent interface bytecode"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(MultiOutput.str() == "a\nb\n", "multi-parent BC33 roundtrip output"))
		{
			std::cerr << MultiOutput.str();
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(CacheRoot, RemoveError);
	}

	{
		// DebugLines / LocalNames / DebugFiles survive .vsbc roundtrip.
		const std::string DebugSource =
			"void main() {\n"
			"    int x = 0;\n"
			"    do { x++; } while (x < 2);\n"
			"    char value =\n"
			"        'A';\n"
			"    string bytes = \"\\x00\\xFF\";\n"
			"    print(x);\n"
			"    print(to_int(value));\n"
			"    print(len(bytes));\n"
			"}\n";
		VeroScript::VeroScriptLexer DebugLexer(DebugSource, "debug_cache.vs");
		const std::vector<VeroScript::FToken>& DebugTokens = DebugLexer.Tokenize();
		VeroScript::VeroScriptParser DebugParser(DebugTokens, "debug_cache.vs");
		std::unique_ptr<VeroScript::FProgram> DebugProgram = DebugParser.ParseProgram();
		VeroScript::VeroScriptCompiler DebugCompiler;
		std::unique_ptr<VeroScript::FBytecodeModule> DebugModule =
			DebugProgram ? DebugCompiler.Compile(*DebugProgram) : nullptr;
		if (!Expect(static_cast<bool>(DebugModule), "failed to compile debug-cache source"))
		{
			return EXIT_FAILURE;
		}
		const std::string EmbeddedBytes("\0\xFF", 2);
		if (!Expect(
				std::find(
					DebugModule->StringPool.begin(),
					DebugModule->StringPool.end(),
					EmbeddedBytes) != DebugModule->StringPool.end(),
				"escaped bytes must retain embedded NUL in the constant pool"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FBytecodeFunction* MainFn = nullptr;
		for (const VeroScript::FBytecodeFunction& Function : DebugModule->Functions)
		{
			const std::size_t UnitSep = Function.Name.rfind('\x1F');
			const std::string Leaf =
				UnitSep == std::string::npos ? Function.Name : Function.Name.substr(UnitSep + 1);
			if (Leaf == "main" || Leaf.rfind("main#", 0) == 0)
			{
				MainFn = &Function;
				break;
			}
		}
		if (!Expect(MainFn != nullptr, "debug-cache main function"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!MainFn->DebugLines.empty(), "compile should emit DebugLines"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!MainFn->LocalNames.empty(), "compile should emit LocalNames"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!DebugModule->DebugFiles.empty(), "compile should emit DebugFiles"))
		{
			return EXIT_FAILURE;
		}
		std::size_t NonZeroDebug = 0;
		for (const VeroScript::FDebugLine& Line : MainFn->DebugLines)
		{
			if (Line.Line != 0)
			{
				++NonZeroDebug;
			}
		}
		if (!Expect(NonZeroDebug > 0, "expected non-zero DebugLine entries"))
		{
			return EXIT_FAILURE;
		}
		bool bFoundDeclarationAnchor = false;
		for (std::size_t Ip = 0; Ip < MainFn->Code.size(); ++Ip)
		{
			bFoundDeclarationAnchor = bFoundDeclarationAnchor
				|| (static_cast<VeroScript::EOpcode>(MainFn->Code[Ip])
						== VeroScript::EOpcode::Nop
					&& Ip < MainFn->DebugLines.size()
					&& MainFn->DebugLines[Ip].Line == 4);
		}
		if (!Expect(bFoundDeclarationAnchor,
				"multiline declaration pause point survives bytecode compilation"))
		{
			return EXIT_FAILURE;
		}

		std::filesystem::remove_all(CacheRoot, RemoveError);
		std::filesystem::create_directories(CacheRoot / ".verocache");
		const std::filesystem::path DebugCache = CacheRoot / ".verocache" / "debug.vsbc";
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::WriteFile(
					DebugCache.string(),
						*DebugModule,
					Fingerprint),
				"failed to write debug .vsbc"))
		{
			return EXIT_FAILURE;
		}
		std::string DebugReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> DebugLoaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				DebugCache.string(),
				Fingerprint,
				DebugReadError);
		if (!Expect(static_cast<bool>(DebugLoaded), "failed to read debug .vsbc"))
		{
			std::cerr << DebugReadError << '\n';
			return EXIT_FAILURE;
		}
		const VeroScript::FBytecodeFunction* LoadedMain = nullptr;
		for (const VeroScript::FBytecodeFunction& Function : DebugLoaded->Functions)
		{
			const std::size_t UnitSep = Function.Name.rfind('\x1F');
			const std::string Leaf =
				UnitSep == std::string::npos ? Function.Name : Function.Name.substr(UnitSep + 1);
			if (Leaf == "main" || Leaf.rfind("main#", 0) == 0)
			{
				LoadedMain = &Function;
				break;
			}
		}
		if (!Expect(LoadedMain != nullptr, "loaded debug-cache main"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(LoadedMain->Code == MainFn->Code,
				"multiline declaration bytecode roundtrip"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				LoadedMain->DebugLines.size() == MainFn->DebugLines.size(),
				"DebugLines size roundtrip"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(LoadedMain->LocalNames == MainFn->LocalNames, "LocalNames roundtrip"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(DebugLoaded->DebugFiles == DebugModule->DebugFiles, "DebugFiles roundtrip"))
		{
			return EXIT_FAILURE;
		}
		bool bLineMatch = true;
		for (std::size_t Index = 0; Index < MainFn->DebugLines.size(); ++Index)
		{
			const auto& Left = MainFn->DebugLines[Index];
			const auto& Right = LoadedMain->DebugLines[Index];
			if (Left.Line != Right.Line
				|| Left.Column != Right.Column
				|| Left.FileIndex != Right.FileIndex)
			{
				bLineMatch = false;
				break;
			}
		}
		if (!Expect(bLineMatch, "DebugLines content roundtrip"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream DebugOutput;
		VeroScript::VeroScriptVm DebugVm(DebugOutput);
		VeroScript::Test::Configure(DebugVm);
		if (!Expect(
				DebugVm.Execute(*DebugLoaded, VeroScript::ERunMode::Script)
					&& DebugOutput.str() == "2\n65\n2\n",
				"do-while, character constants, and embedded bytes survive bytecode roundtrip"))
		{
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(CacheRoot, RemoveError);
	}

	// Closure capture slot names/types survive disk roundtrip.
	{
		std::unique_ptr<VeroScript::FBytecodeModule> CaptureModule = CompileSource(
			"fn()->int make() {\n"
			"    int count = 42;\n"
			"    return [&count]() -> int { count++; return count; };\n"
			"}\n"
			"void main() { fn()->int f = make(); print(f()); }\n");
		if (!Expect(static_cast<bool>(CaptureModule), "compile closure capture metadata"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FBytecodeFunction* CaptureFunction = nullptr;
		for (const VeroScript::FBytecodeFunction& Function : CaptureModule->Functions)
		{
			if (!Function.Captures.empty())
			{
				CaptureFunction = &Function;
				break;
			}
		}
		if (!Expect(
				CaptureFunction != nullptr
					&& CaptureFunction->Captures == std::vector<VeroScript::FBytecodeCapture>{{"count", "int", VeroScript::EClosureCaptureMode::Reference, true}},
				"compiled closure capture schema"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FBytecodeModule InvalidCaptureModule = *CaptureModule;
		for (VeroScript::FBytecodeFunction& Function : InvalidCaptureModule.Functions)
		{
			if (!Function.Captures.empty())
			{
				Function.Captures[0].TypeName = "ref ";
				break;
			}
		}
		std::string InvalidCaptureError;
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidCaptureModule, InvalidCaptureError),
				"malformed reference capture type must be rejected"))
		{
			return EXIT_FAILURE;
		}
		const std::filesystem::path CaptureRoot = TemporaryDirectory.Get() / "captures";
		std::error_code CaptureRemoveError;
		std::filesystem::remove_all(CaptureRoot, CaptureRemoveError);
		std::filesystem::create_directories(CaptureRoot);
		const std::filesystem::path CapturePath = CaptureRoot / "capture.vsbc";
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::WriteFile(
					CapturePath.string(), *CaptureModule, Fingerprint),
				"write closure capture bytecode"))
		{
			return EXIT_FAILURE;
		}
		std::string CaptureReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> CaptureLoaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				CapturePath.string(), Fingerprint, CaptureReadError);
		bool bFoundRoundtripCapture = false;
		if (CaptureLoaded)
		{
			for (const VeroScript::FBytecodeFunction& Function : CaptureLoaded->Functions)
			{
				bFoundRoundtripCapture =
					bFoundRoundtripCapture
					|| (Function.Captures == std::vector<VeroScript::FBytecodeCapture>{{"count", "int", VeroScript::EClosureCaptureMode::Reference, true}});
			}
		}
		if (!Expect(
				CaptureLoaded && bFoundRoundtripCapture,
				"closure capture schema bytecode roundtrip"))
		{
			std::cerr << CaptureReadError << '\n';
			return EXIT_FAILURE;
		}
		std::ostringstream CaptureOutput;
		std::istringstream CaptureInput;
		std::ostringstream CaptureErrors;
		VeroScript::VeroScriptVm CaptureVm(CaptureOutput, CaptureInput, CaptureErrors);
		VeroScript::Test::Configure(CaptureVm);
		if (!Expect(
				CaptureVm.Execute(*CaptureLoaded, VeroScript::ERunMode::Script)
					&& CaptureOutput.str() == "43\n",
				"roundtripped reference capture executes with heap-cell lifetime"))
		{
			std::cerr << CaptureErrors.str() << '\n';
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(CaptureRoot, CaptureRemoveError);
	}

	// Structurally valid bytecode must not read operands belonging to its caller.
	for (const std::string_view FrameSource : {
			 "int identity(int value) { return value; } "
		"int probe() { return 0; } "
		"int combine(int first, int second, int third) { return first + second + third; } "
		"void main() { print(combine(1000, 100, probe())); }",
		"int identity(int value) { return value; } "
		"int combine(int first, int second, int third) { return first + second + third; } "
		"class Runner { public: int probe() { return 0; } "
		"int run() { return combine(1000, 100, this.probe()); } }; "
		"void main() { print(Runner().run()); }",
		"int identity(int value) { return value; } int probe() { return 0; } "
		"int combine(int first, bool second, int third) { "
		"if (second) return first + 100 + third; return first + third; } "
		"void main() { print(combine(1000, true, probe())); }"})
	{
		const auto FrameModule = CompileSource(std::string(FrameSource));
		if (!Expect(static_cast<bool>(FrameModule), "compile operand frame isolation fixture"))
		{
			return EXIT_FAILURE;
		}
		std::size_t ProbeIndex = FrameModule->Functions.size();
		std::size_t IdentityIndex = FrameModule->Functions.size();
		for (std::size_t Index = 0; Index < FrameModule->Functions.size(); ++Index)
		{
			if (FrameModule->Functions[Index].Name.find("probe") != std::string::npos)
			{
				ProbeIndex = Index;
			}
			if (FrameModule->Functions[Index].Name.find("identity") != std::string::npos)
			{
				IdentityIndex = Index;
			}
		}
		if (!Expect(ProbeIndex < FrameModule->Functions.size()
				&& IdentityIndex < FrameModule->Functions.size(), "find operand isolation callees"))
		{
			return EXIT_FAILURE;
		}
		using VeroScript::EOpcode;
		const auto Op = [](const EOpcode Opcode)
		{
			return static_cast<std::uint8_t>(Opcode);
		};
		const auto AppendConstant = [&](std::vector<std::uint8_t>& Code, const std::int32_t Value)
		{
			const auto It = std::find_if(FrameModule->Constants.begin(), FrameModule->Constants.end(),
				[&](const auto& Constant)
				{
					return Constant.Kind == VeroScript::EConstantKind::I32 && Constant.I32 == Value;
				});
			if (It == FrameModule->Constants.end())
			{
				return false;
			}
			Code.push_back(Op(EOpcode::ConstI32));
			VeroScript::Bytecode::WriteU16LE(Code,
				static_cast<std::uint16_t>(It - FrameModule->Constants.begin()));
			return true;
		};
		std::vector<std::vector<std::uint8_t>> Prefixes = {
			{Op(EOpcode::Dup), Op(EOpcode::Pop)},
			{Op(EOpcode::Pop)},
			{Op(EOpcode::Lt), Op(EOpcode::Pop)},
			{},
		};
		if (!Expect(AppendConstant(Prefixes[1], 100)
				&& AppendConstant(Prefixes[2], 1000) && AppendConstant(Prefixes[2], 100)
				&& AppendConstant(Prefixes[3], 0), "find operand fixture constants"))
		{
			return EXIT_FAILURE;
		}
		Prefixes[3].push_back(Op(EOpcode::Lt));
		Prefixes[3].push_back(Op(EOpcode::Pop));
		if (!Expect(AppendConstant(Prefixes[3], 100), "restore operand fixture constant"))
		{
			return EXIT_FAILURE;
		}
		// An identity-call rewrite must still reject an absent argument. Replacing
		// Call with Nop before validating this function can erase that underflow.
		Prefixes.push_back({Op(EOpcode::Call)});
		VeroScript::Bytecode::WriteU16LE(Prefixes.back(), static_cast<std::uint16_t>(IdentityIndex));
		Prefixes.back().push_back(1);
		Prefixes.push_back({Op(EOpcode::JmpIfFalse), 0, 0, 0, 0, Op(EOpcode::ConstBool), 1});
		for (const auto& Prefix : Prefixes)
		{
			VeroScript::FBytecodeModule Candidate = *FrameModule;
			auto& Probe = Candidate.Functions[ProbeIndex];
			Probe.Code.insert(Probe.Code.begin(), Prefix.begin(), Prefix.end());
			Probe.DebugLines.clear();
			std::vector<std::uint8_t> Bytes;
			std::string FrameError;
			if (!Expect(VeroScript::VeroScriptBytecodeIO::SerializeBytecode(Candidate, 0, Bytes, FrameError),
					"serialize structurally valid operand isolation fixture"))
			{
				return EXIT_FAILURE;
			}
			const auto FrameLoaded = VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
				VeroScript::FByteView(Bytes), std::nullopt, FrameError);
			if (!Expect(static_cast<bool>(FrameLoaded), "reload operand isolation fixture"))
			{
				return EXIT_FAILURE;
			}
			for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed})
			{
				for (int Mode = 0; Mode < 3; ++Mode)
				{
					std::ostringstream FrameOutput;
					VeroScript::VeroScriptVm FrameVm(FrameOutput);
		VeroScript::Test::Configure(FrameVm);
					FrameVm.SetExecutionBackend(Backend);
					VeroScript::VeroScriptDebugger FrameDebugger;
					FrameDebugger.SetRunMode(VeroScript::EDebugRunMode::Continue);
					if (Mode == 1)
					{
						FrameVm.SetDebugger(&FrameDebugger);
					}
					if (Mode == 2)
					{
						VeroScript::FExecutionLimits Limits;
						Limits.MaxInstructions = 1000;
						FrameVm.SetExecutionLimits(Limits);
					}
					const bool bExecuted = FrameVm.Execute(*FrameLoaded, VeroScript::ERunMode::Script);
					if (!Expect(!bExecuted
							&& FrameVm.HasError() && FrameOutput.str().empty()
							&& FrameVm.GetError().Message.find("stack") != std::string::npos,
							"callee operand underflow must not consume or duplicate caller values"))
					{
						std::cerr << "prefix=" << static_cast<int>(Prefix.front())
							<< " backend=" << static_cast<int>(Backend) << " mode=" << Mode
							<< " executed=" << bExecuted << " output=" << FrameOutput.str()
							<< " error=" << FrameVm.GetError().Message << '\n';
						return EXIT_FAILURE;
					}
					if (!Expect(FrameVm.Execute(*FrameModule, VeroScript::ERunMode::Script)
							&& !FrameVm.HasError() && FrameOutput.str() == "1100\n",
							"VM recovers after rejected callee operand underflow"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
	}

	// Public structural validation rejects malformed host-built/cache bytecode
	// before either the VM or JIT can observe it.
	{
		std::string ValidationError;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::ValidateModule(
					*Original, ValidationError),
				"compiled module should pass structural validation"))
		{
			std::cerr << ValidationError << '\n';
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule HashFnPathMain = *Original;
		auto& HashFnMain = HashFnPathMain.Functions[
			static_cast<std::size_t>(HashFnPathMain.MainFunctionIndex)];
		const std::string OldMainName = HashFnMain.Name;
		const std::size_t OldIdentitySeparator = OldMainName.find('\x1F');
		const std::string MainIdentity = OldIdentitySeparator == std::string::npos
			? OldMainName
			: OldMainName.substr(OldIdentitySeparator + 1);
		HashFnMain.Name = "folder<#fn#name.vs\x1F" + MainIdentity;
		HashFnPathMain.FunctionIndices.erase(OldMainName);
		HashFnPathMain.FunctionIndices.emplace(
			HashFnMain.Name,
			static_cast<std::uint16_t>(HashFnPathMain.MainFunctionIndex));
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::ValidateModule(
					HashFnPathMain, ValidationError),
				"#fn in the entry module path must not corrupt main linkage parsing"))
		{
			std::cerr << ValidationError << '\n';
			return EXIT_FAILURE;
		}
		std::vector<std::uint8_t> CompleteBytes;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					*Original, Fingerprint, CompleteBytes, ValidationError),
				"compiled module should serialize for truncation sweep"))
		{
			std::cerr << ValidationError << '\n';
			return EXIT_FAILURE;
		}
		for (std::size_t TruncatedSize = 0;
			TruncatedSize < CompleteBytes.size();
			++TruncatedSize)
		{
			if (VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
					VeroScript::FByteView(CompleteBytes.data(), TruncatedSize),
					std::nullopt,
					ValidationError))
			{
				std::cerr << "accepted truncated bytecode at " << TruncatedSize
					<< " of " << CompleteBytes.size() << " bytes\n";
				return EXIT_FAILURE;
			}
		}

		VeroScript::FBytecodeModule InvalidMainSignature = *Original;
		auto& InvalidMain = InvalidMainSignature.Functions[
			static_cast<std::size_t>(InvalidMainSignature.MainFunctionIndex)];
		InvalidMain.ClosureTypeName = "fn()->double";
		InvalidMain.bReturnsVoid = false;
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidMainSignature, ValidationError)
					&& ValidationError.find("invalid main signature") != std::string::npos,
				"invalid main return type should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidTryCast = *Original;
		bool bFoundTryCast = false;
		for (const VeroScript::FBytecodeFunction& Function : InvalidTryCast.Functions)
		{
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const VeroScript::EOpcode Opcode =
					static_cast<VeroScript::EOpcode>(Function.Code[Ip]);
				if (Opcode == VeroScript::EOpcode::TryCast)
				{
					const std::uint16_t OptionalTypeIndex =
						VeroScript::Bytecode::ReadU16LE(Function.Code.data() + Ip + 3);
					if (OptionalTypeIndex < InvalidTryCast.Types.size())
					{
						InvalidTryCast.Types[OptionalTypeIndex].Name = "tampered_optional";
						bFoundTryCast = true;
					}
					break;
				}
				Ip += VeroScript::Bytecode::InstructionSize(Opcode);
			}
			if (bFoundTryCast)
			{
				break;
			}
		}
		if (!Expect(
				bFoundTryCast
					&& !VeroScript::VeroScriptBytecodeIO::ValidateModule(
						InvalidTryCast, ValidationError)
					&& ValidationError.find("try_cast requires matching builtin optional")
						!= std::string::npos,
				"try_cast bytecode must reference matching builtin optional<T> metadata"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidOpcode = *Original;
		InvalidOpcode.Functions.front().Code = {0xFFu};
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidOpcode, ValidationError),
				"invalid opcode should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidJump = *Original;
		InvalidJump.Functions.front().Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::Jmp),
			1, 0, 0, 0};
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidJump, ValidationError),
				"jump outside instruction boundaries should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidLocal = *Original;
		InvalidLocal.Functions.front().Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal),
			0xFFu, 0xFFu};
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidLocal, ValidationError),
				"invalid local operand should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidConstantKind;
		VeroScript::FBytecodeConstant FloatingConstant;
		FloatingConstant.Kind = VeroScript::EConstantKind::F64;
		FloatingConstant.F64 = 1.0;
		InvalidConstantKind.Constants.push_back(FloatingConstant);
		VeroScript::FBytecodeFunction ConstantFunction;
		ConstantFunction.Name = "bad_constant";
		ConstantFunction.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::ConstI32),
			0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Pop),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		InvalidConstantKind.Functions.push_back(std::move(ConstantFunction));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidConstantKind, ValidationError),
				"constant opcode/kind mismatch should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule BoolModule;
		VeroScript::FBytecodeConstant BoolConstant;
		BoolConstant.Kind = VeroScript::EConstantKind::Bool;
		BoolConstant.Bool = true;
		BoolModule.Constants.push_back(BoolConstant);
		VeroScript::FBytecodeFunction BoolFunction;
		BoolFunction.Name = "bool_encoding";
		BoolFunction.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		BoolModule.Functions.push_back(std::move(BoolFunction));
		std::vector<std::uint8_t> BoolBytes;
		if (!Expect(
				VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
					BoolModule, 0, BoolBytes, ValidationError),
				"bool encoding fixture should serialize"))
		{
			return EXIT_FAILURE;
		}
		// Header (18), empty string/wstring counts (8), constant count (4),
		// and constant kind (1) precede the encoded bool byte.
		constexpr std::size_t BoolValueOffset = 31;
		if (!Expect(
				BoolBytes.size() > BoolValueOffset,
				"bool encoding fixture layout should contain its value"))
		{
			return EXIT_FAILURE;
		}
		BoolBytes[BoolValueOffset] = 2;
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
					VeroScript::FByteView(BoolBytes),
					std::nullopt,
					ValidationError),
				"noncanonical bool byte must be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidStaticPayload;
		VeroScript::FBytecodeTypeDesc StaticOwner;
		StaticOwner.Name = "StaticOwner";
		VeroScript::FBytecodeFieldDesc StaticField;
		StaticField.Name = "value";
		StaticField.TypeName = "int";
		StaticField.bHasDefault = true;
		StaticField.DefaultValue.Kind = VeroScript::EConstantKind::I32;
		StaticOwner.StaticFields.push_back(std::move(StaticField));
		InvalidStaticPayload.Types.push_back(std::move(StaticOwner));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidStaticPayload, ValidationError),
				"nonserialized static-field initializer payload must be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidMutableStatic;
		VeroScript::FBytecodeTypeDesc MutableStaticOwner;
		MutableStaticOwner.Name = "MutableStaticOwner";
		VeroScript::FBytecodeFieldDesc MutableStaticField;
		MutableStaticField.Name = "value";
		MutableStaticField.TypeName = "int";
		MutableStaticField.bIsMutable = true;
		MutableStaticOwner.StaticFields.push_back(std::move(MutableStaticField));
		InvalidMutableStatic.Types.push_back(std::move(MutableStaticOwner));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidMutableStatic, ValidationError)
					&& ValidationError.find("static field cannot be mutable")
						!= std::string::npos,
				"mutable static-field metadata must be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule InvalidMutableInterface;
		VeroScript::FBytecodeInterfaceDesc MutableInterface;
		MutableInterface.Name = "IMutable";
		VeroScript::FBytecodeFieldDesc MutableInterfaceField;
		MutableInterfaceField.Name = "value";
		MutableInterfaceField.TypeName = "int";
		MutableInterfaceField.bIsMutable = true;
		MutableInterface.Fields.push_back(std::move(MutableInterfaceField));
		InvalidMutableInterface.Interfaces.push_back(std::move(MutableInterface));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					InvalidMutableInterface, ValidationError)
					&& ValidationError.find("interface field cannot be mutable")
						!= std::string::npos,
				"mutable interface-field metadata must be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule CyclicTypes;
		VeroScript::FBytecodeTypeDesc TypeA;
		TypeA.Name = "A";
		TypeA.BaseTypeIndices = {1};
		VeroScript::FBytecodeTypeDesc TypeB;
		TypeB.Name = "B";
		TypeB.BaseTypeIndices = {0};
		CyclicTypes.Types.push_back(std::move(TypeA));
		CyclicTypes.Types.push_back(std::move(TypeB));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					CyclicTypes, ValidationError),
				"cyclic type inheritance should be rejected"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FBytecodeModule CyclicInterfaces;
		VeroScript::FBytecodeInterfaceDesc InterfaceA;
		InterfaceA.Name = "IA";
		InterfaceA.BaseInterfaceIndices = {1};
		VeroScript::FBytecodeInterfaceDesc InterfaceB;
		InterfaceB.Name = "IB";
		InterfaceB.BaseInterfaceIndices = {0};
		CyclicInterfaces.Interfaces.push_back(std::move(InterfaceA));
		CyclicInterfaces.Interfaces.push_back(std::move(InterfaceB));
		if (!Expect(
				!VeroScript::VeroScriptBytecodeIO::ValidateModule(
					CyclicInterfaces, ValidationError),
				"cyclic interface inheritance should be rejected"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript;
		FBytecodeModule Intervals;
		FBytecodeFunction Function;
		Function.Name = "exception_intervals";
		Function.LocalCount = 1;
		Function.Code.assign(32, static_cast<std::uint8_t>(EOpcode::Nop));
		Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		Intervals.Functions.push_back(std::move(Function));
		const auto Region = [](std::uint32_t Begin, std::uint32_t End)
		{
			FBytecodeExceptionRegion Result;
			Result.BeginOffset = Begin;
			Result.EndOffset = End;
			Result.Catches.push_back({"", 0, 0, 32});
			return Result;
		};
		auto& Regions = Intervals.Functions[0].ExceptionRegions;
		// Deliberately unordered, including shared starts/ends and touching ranges.
		Regions = {Region(12, 16), Region(2, 4), Region(0, 8),
			Region(16, 20), Region(2, 8), Region(0, 12)};
		std::string Error;
		if (!Expect(VeroScriptBytecodeIO::ValidateModule(Intervals, Error),
				"unordered strictly nested or disjoint exception ranges are valid")
			|| !Expect(Regions.front().BeginOffset == 12,
				"interval validation preserves exception dispatch order"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		for (const auto& Invalid : {Region(0, 8), Region(3, 9)})
		{
			Regions.push_back(Invalid);
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Intervals, Error)
					&& Error.find("strictly nested or disjoint") != std::string::npos,
					"duplicate and crossing exception ranges are rejected"))
			{
				return EXIT_FAILURE;
			}
			Regions.pop_back();
		}
		// Compare against the original pairwise contract for every ordered triple
		// of small non-empty ranges. Covers duplicate, crossing and shared endpoints
		// independently of the sorted-stack implementation.
		std::vector<FBytecodeExceptionRegion> SmallRanges;
		for (std::uint32_t Begin = 0; Begin < 7; ++Begin)
		{
			for (std::uint32_t End = Begin + 1; End <= 7; ++End)
			{
				SmallRanges.push_back(Region(Begin, End));
			}
		}
		const auto Compatible = [](const auto& Left, const auto& Right)
		{
			const bool bDisjoint = Left.EndOffset <= Right.BeginOffset
				|| Right.EndOffset <= Left.BeginOffset;
			const bool bContains = (Left.BeginOffset <= Right.BeginOffset
				&& Left.EndOffset >= Right.EndOffset)
				|| (Right.BeginOffset <= Left.BeginOffset
					&& Right.EndOffset >= Left.EndOffset);
			const bool bIdentical = Left.BeginOffset == Right.BeginOffset
				&& Left.EndOffset == Right.EndOffset;
			return bDisjoint || (bContains && !bIdentical);
		};
		for (const auto& First : SmallRanges)
		{
			for (const auto& Second : SmallRanges)
			{
				for (const auto& Third : SmallRanges)
				{
					Regions = {First, Second, Third};
					const bool bExpected = Compatible(First, Second)
						&& Compatible(First, Third) && Compatible(Second, Third);
					if (!Expect(VeroScriptBytecodeIO::ValidateModule(Intervals, Error) == bExpected,
							"exception interval sweep matches pairwise validation"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
		constexpr std::uint32_t RegionCount = 32768;
		auto& LargeFunction = Intervals.Functions[0];
		LargeFunction.Code.assign(3 * RegionCount, static_cast<std::uint8_t>(EOpcode::Nop));
		LargeFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::EndFinally));
		LargeFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
		Regions.clear();
		for (std::uint32_t Index = RegionCount; Index > 0; --Index)
		{
			auto Entry = Region(2 * (Index - 1), 2 * (Index - 1) + 1);
			Entry.Catches[0].HandlerOffset = Entry.EndOffset;
			Entry.FinallyOffset = 2 * RegionCount;
			Entry.FinallyEndOffset = 3 * RegionCount + 1;
			Regions.push_back(std::move(Entry));
		}
		std::vector<std::uint8_t> Bytes;
		if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(Intervals, 0, Bytes, Error)
				&& VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error),
				"large exception metadata validates and round-trips without pairwise scans"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	// All public paths are UTF-8, including bytecode cache I/O on Windows.
	{
		const std::filesystem::path Utf8Root = TemporaryDirectory.Get()
			/ VeroScript::PathUtil::FromUtf8(
				"veroscript-\xE4\xB8\xAD\xE6\x96\x87-path");
		std::error_code Utf8Error;
		std::filesystem::remove_all(Utf8Root, Utf8Error);
		std::filesystem::create_directories(Utf8Root, Utf8Error);
		const std::filesystem::path Utf8Cache =
			Utf8Root / VeroScript::PathUtil::FromUtf8(
				"\xE7\xBC\x93\xE5\xAD\x98.vsbc");
		const std::string Utf8CacheName =
			VeroScript::PathUtil::ToUtf8(Utf8Cache);
		if (!Expect(
				!Utf8Error
					&& VeroScript::VeroScriptBytecodeIO::WriteFile(
						Utf8CacheName, *Original, Fingerprint),
				"write bytecode through UTF-8 path"))
		{
			return EXIT_FAILURE;
		}
		std::string Utf8ReadError;
		std::unique_ptr<VeroScript::FBytecodeModule> Utf8Loaded =
			VeroScript::VeroScriptBytecodeIO::ReadFile(
				Utf8CacheName, Fingerprint, Utf8ReadError);
		if (!Expect(
				static_cast<bool>(Utf8Loaded),
				"read bytecode through UTF-8 path"))
		{
			std::cerr << Utf8ReadError << '\n';
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(Utf8Root, Utf8Error);

		#if defined(_WIN32)
		for (const auto& Pair : {
			std::pair<std::string, std::string>{"//?/C:/vero/source.vs", "C:/vero/source.vs"},
			{"//?/UNC/server/share/source.vs", "//server/share/source.vs"},
			{"//?/unc/server/share/source.vs", "//server/share/source.vs"},
			{"//?/UNC/server/COM1/source.vs", "//server/COM1/source.vs"}})
		{
			if (!Expect(VeroScript::PathUtil::Normalize(Pair.first) == Pair.second &&
				VeroScript::PathUtil::ToUtf8(VeroScript::PathUtil::FromUtf8(Pair.first)) == Pair.second,
				"Windows extended DOS and UNC paths use the same public spelling without filesystem access"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const auto* Special : {"//?/Volume{00000000-0000-0000-0000-000000000000}/source.vs",
			"//./PhysicalDrive0", "//?/C:/vero/value.", "//?/C:/vero/value ",
			"//?/C:/vero/NUL.txt", "//?/C:/vero/Com1/source.vs", "//?/C:/vero/NUL .txt",
			"//?/UNC/server./share/source.vs", "//?/UNC/server/share /source.vs"})
		{
			if (!Expect(VeroScript::PathUtil::ToUtf8(VeroScript::PathUtil::FromUtf8(Special)) == Special,
				"Windows device namespaces and verbatim-only components retain their exact namespace"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Expect(
				VeroScript::PathUtil::Normalize("C:/") == "C:/",
				"Windows drive root normalization should preserve root semantics"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				VeroScript::PathUtil::IdentityKey("C:/\xC3\x84/FILE.vs")
					== VeroScript::PathUtil::IdentityKey("c:/\xC3\xA4/file.vs"),
				"Windows path identity should case-fold non-ASCII UTF-8"))
		{
			return EXIT_FAILURE;
		}
		#else
		if (!Expect(VeroScript::PathUtil::Normalize("/tmp/vero\\path/./file.vs")
				== "/tmp/vero\\path/file.vs"
				&& !VeroScript::PathUtil::Equal("/tmp/vero\\path/file.vs", "/tmp/vero/path/file.vs"),
			"POSIX path identity preserves literal backslashes"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	return EXIT_SUCCESS;
}
