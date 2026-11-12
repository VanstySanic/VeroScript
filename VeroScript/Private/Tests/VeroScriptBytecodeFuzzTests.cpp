// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptPackage.h"
#include "VeroScriptParser.h"

#include "VeroScriptAst.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
	std::unique_ptr<VeroScript::FBytecodeModule> CompileSeedModule()
	{
		constexpr const char* Source =
			"struct Payload { int value = 7; };\n"
			"int twice(int value) { return value + value; }\n"
			"void main() { Payload p = Payload(); print(twice(p.value)); }\n";
		VeroScript::VeroScriptLexer Lexer(Source, "<bytecode-fuzz-seed>");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "<bytecode-fuzz-seed>");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		return Compiler.HasError() ? nullptr : std::move(Module);
	}

	void ExerciseInput(const std::uint8_t* const Data, const std::size_t Size)
	{
		if (Size != 0 && Data == nullptr)
		{
			return;
		}
		const VeroScript::FByteView Bytes(Data, Size);
		std::string Error;
		// Untrusted bytes must either deserialize to a fully validated module or
		// fail cleanly. The fuzzer's assertion is process safety, not acceptance.
		(void)VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
			Bytes, std::nullopt, Error);
		VeroScript::FPackageData Package;
		Error.clear();
		(void)VeroScript::ReadPackage(Bytes, Package, Error);
	}

	std::uint32_t NextRandom(std::uint32_t& State)
	{
		State = State * 1664525u + 1013904223u;
		return State;
	}

	bool Expect(const bool Condition, const char* const Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptBytecodeFuzzTests: " << Message << '\n';
		}
		return Condition;
	}

	bool WriteCorpusFile(
		const std::filesystem::path& Path,
		const std::vector<std::uint8_t>& Bytes)
	{
		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			return false;
		}
		if (!Bytes.empty())
		{
			Stream.write(
				reinterpret_cast<const char*>(Bytes.data()),
				static_cast<std::streamsize>(Bytes.size()));
		}
		return static_cast<bool>(Stream);
	}
}

extern "C" int LLVMFuzzerTestOneInput(
	const std::uint8_t* const Data,
	const std::size_t Size)
{
	ExerciseInput(Data, Size);
	return 0;
}

#if !defined(VEROSCRIPT_LIBFUZZER)
int main(const int Argc, char** const Argv)
{
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSeedModule();
	if (!Expect(static_cast<bool>(Module), "could not compile deterministic seed"))
	{
		return EXIT_FAILURE;
	}

	std::vector<std::uint8_t> Bytecode;
	std::string Error;
	if (!Expect(VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
				*Module, 0, Bytecode, Error),
			"could not serialize bytecode seed"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::FPackageBuildOptions Options;
	Options.PackageName = "fuzz.seed";
	Options.PackageVersion = 1;
	Options.HostMetadata["fuzz.opaque.extension.v1"] = {0x00, 0x7F, 0xFF};
	std::vector<std::uint8_t> Package;
	if (!Expect(VeroScript::BuildPackage(*Module, Options, Package, Error),
			"could not build package seed"))
	{
		return EXIT_FAILURE;
	}
	std::unique_ptr<VeroScript::FBytecodeModule> RoundTrippedBytecode =
		VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
			VeroScript::FByteView(Bytecode), std::nullopt, Error);
	if (!Expect(static_cast<bool>(RoundTrippedBytecode),
			"serialized bytecode seed was not accepted"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::FPackageData RoundTrippedPackage;
	if (!Expect(VeroScript::ReadPackage(
			VeroScript::FByteView(Package), RoundTrippedPackage, Error)
			&& RoundTrippedPackage.HostMetadata == Options.HostMetadata,
		"package seed or opaque host metadata was not preserved"))
	{
		return EXIT_FAILURE;
	}

	if (Argc == 3 && std::string_view(Argv[1]) == "--write-corpus")
	{
		const std::filesystem::path CorpusDirectory(Argv[2]);
		std::error_code FilesystemError;
		std::filesystem::create_directories(CorpusDirectory, FilesystemError);
		if (!Expect(!FilesystemError, "could not create fuzz corpus directory")
			|| !Expect(WriteCorpusFile(CorpusDirectory / "valid-module.vsbc", Bytecode),
				"could not write bytecode corpus seed")
			|| !Expect(WriteCorpusFile(CorpusDirectory / "valid-library.vslib", Package),
				"could not write package corpus seed"))
		{
			return EXIT_FAILURE;
		}
		// Prefixes let the mutator enter progressively deeper header states instead
		// of spending most short CI fuzz runs rediscovering the magic/version bytes.
		for (const std::size_t Prefix : {4u, 8u, 16u, 32u, 64u})
		{
			if (Prefix < Bytecode.size())
			{
				const std::vector<std::uint8_t> Bytes(
					Bytecode.begin(), Bytecode.begin() + Prefix);
				if (!Expect(WriteCorpusFile(
						CorpusDirectory / ("module-prefix-" + std::to_string(Prefix)), Bytes),
					"could not write bytecode prefix corpus seed"))
				{
					return EXIT_FAILURE;
				}
			}
			if (Prefix < Package.size())
			{
				const std::vector<std::uint8_t> Bytes(
					Package.begin(), Package.begin() + Prefix);
				if (!Expect(WriteCorpusFile(
						CorpusDirectory / ("package-prefix-" + std::to_string(Prefix)), Bytes),
					"could not write package prefix corpus seed"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		return EXIT_SUCCESS;
	}

	// Deterministic mutations are run in every normal CTest configuration. They
	// cover truncation, bit/length/index corruption, arbitrary replacement bytes,
	// and package host-extension bytes without depending on a libFuzzer toolchain.
	std::uint32_t Random = 0x51A7F00Du;
	for (const std::vector<std::uint8_t>* const Seed : {&Bytecode, &Package})
	{
		for (std::size_t Length = 0; Length < Seed->size(); ++Length)
		{
			ExerciseInput(Seed->data(), Length);
		}
		for (std::size_t Iteration = 0; Iteration < 512; ++Iteration)
		{
			std::vector<std::uint8_t> Mutated = *Seed;
			if (!Mutated.empty())
			{
				const std::size_t Offset = NextRandom(Random) % Mutated.size();
				Mutated[Offset] ^= static_cast<std::uint8_t>(NextRandom(Random));
			}
			if ((Iteration & 7u) == 0u)
			{
				Mutated.resize(NextRandom(Random) % (Mutated.size() + 1u));
			}
			ExerciseInput(Mutated.data(), Mutated.size());
		}
	}

	std::vector<std::uint8_t> Arbitrary(1024u);
	for (std::uint8_t& Byte : Arbitrary)
	{
		Byte = static_cast<std::uint8_t>(NextRandom(Random));
	}
	ExerciseInput(Arbitrary.data(), Arbitrary.size());
	return EXIT_SUCCESS;
}
#endif
