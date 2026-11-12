// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BuildInternal.h"
#include <algorithm>
#include <charconv>

namespace VeroScript::Build::Detail
{
	namespace
	{
		void StringArray(const J& V)
		{
			if (!V.is_array())
			{
				Fail("array", "expected an array of strings");
			}
			std::set<std::string> Seen;
			for (const auto& S : V)
			{
				if (!S.is_string() || !Seen.insert(S.get<std::string>()).second)
				{
					Fail("array", "invalid or duplicate string in array");
				}
			}
		}
		void Input(const J& V)
		{
			if (V.is_string())
			{
				if (V.get<std::string>().empty())
				{
					Fail("input", "empty build input path");
				}
				return;
			}
			Keys(V, {"target", "output", "path"}, "reference");
			Id(V.at("target").get<std::string>());
			Id(V.at("output").get<std::string>());
			if (V.contains("path"))
			{
				Safe(V.at("path").get<std::string>());
			}
		}
		void Compile(const J& V)
		{
			Keys(V, {"stripDebugInfo", "defines", "importPaths", "stdlibSearchPaths", "allowCircularImports", "allowProjectWideAccess"}, "compile");
			for (const auto* K : {"stripDebugInfo", "allowCircularImports", "allowProjectWideAccess"})
			{
				if (V.contains(K) && !V.at(K).is_boolean())
				{
					Fail("compile-option", "expected a boolean compile option", K);
				}
			}
			for (const auto* K : {"defines", "importPaths", "stdlibSearchPaths"})
			{
				if (V.contains(K))
				{
					StringArray(V.at(K));
				}
			}
		}
		void Files(const J& V)
		{
			if (!V.is_array())
			{
				Fail("files", "file mappings must be an array");
			}
			for (const auto& F : V)
			{
				Keys(F, {"from", "to", "protection", "signature"}, "files");
				Input(F.at("from"));
				Safe(F.at("to").get<std::string>());
				if (F.contains("signature"))
				{
					Input(F.at("signature"));
				}
				if (F.contains("protection"))
				{
					const auto P = Protection(F.at("protection"), false);
					if (!P.SigningKey.empty() && P.SignatureScope != "artifact")
					{
						Fail("signature-scope", "file mappings require artifact signatures");
					}
				}
			}
		}
		void Component(const J& V, const std::string& Kind)
		{
			Keys(V, {"mode", "component", "artifact", "path", "requirements", "arguments", "protocol", "output"}, Kind);
			const auto Mode = V.at("mode").get<std::string>();
			if (Kind == "launcher")
			{
				if (Mode != "none" && Mode != "generated" && Mode != "existing")
				{
					Fail("launcher", "unsupported launcher mode");
				}
			}
			else if (Mode != "bundled" && Mode != "external")
			{
				Fail("component", "unsupported component mode");
			}
			if (V.contains("component"))
			{
				Input(V.at("component"));
			}
			if (V.contains("artifact"))
			{
				Input(V.at("artifact"));
			}
			if (V.contains("path"))
			{
				Input(V.at("path"));
			}
			if (V.contains("output"))
			{
				Safe(V.at("output").get<std::string>());
			}
			if (V.contains("arguments") && (!V.at("arguments").is_array() || !V.at("arguments").empty()))
			{
				Fail("component-arguments", "launcher arguments are provided at application startup");
			}
			const auto Sources = static_cast<unsigned>(V.contains("component")) + static_cast<unsigned>(V.contains("artifact")) + static_cast<unsigned>(V.contains("path"));
			if ((Mode == "bundled" && Sources != 1) || ((Mode == "none" || Mode == "external" || Mode == "generated") && Sources))
			{
				Fail("component-source", "component mode has an invalid number of source references");
			}
			if (Kind == "launcher" && Mode == "existing" && (Sources != 1 || !V.contains("path") || V.value("protocol", "") != "0.1.1"))
			{
				Fail("launcher-protocol", "existing launcher needs a path and application protocol 0.1.1");
			}
		}
	}
	std::vector<J> ArtifactReferences(const J& Target)
	{
		std::vector<J> Out;
		const auto Add = [&](const J& Input)
		{
			if (Input.is_object())
			{
				Out.push_back(Input);
			}
		};
		for (const auto* K : {"entry", "source", "application"})
		{
			if (Target.contains(K))
			{
				Add(Target.at(K));
			}
		}
		for (const auto* K : {"modules", "inputs"})
		{
			if (Target.contains(K))
			{
				for (const auto& Input : Target.at(K))
				{
					Add(Input);
				}
			}
		}
		for (const auto& File : Target.value("files", J::array()))
		{
			Add(File.at("from"));
			if (File.contains("signature"))
			{
				Add(File.at("signature"));
			}
		}
		for (const auto* K : {"runtime", "environment", "launcher"})
		{
			if (Target.contains(K))
			{
				for (const auto* Source : {"component", "artifact", "path"})
				{
					if (Target.at(K).contains(Source))
					{
						Add(Target.at(K).at(Source));
					}
				}
			}
		}
		return Out;
	}
	std::set<std::string> References(const J& V)
	{
		std::set<std::string> Out;
		for (const auto& Input : ArtifactReferences(V))
		{
			Out.insert(Input.at("target").get<std::string>());
		}
		return Out;
	}
	Application::FProtectionOptions Protection(const J& V, bool Content)
	{
		Keys(V, {"encryption", "signature"}, "protection");
		Application::FProtectionOptions O;
		O.SignatureScope = Content ? "content" : "artifact";
		if (V.contains("encryption"))
		{
			const auto& E = V.at("encryption");
			Keys(E, {"algorithm", "keyRef", "keyVersion"}, "encryption");
			if (E.at("algorithm") != "xchacha20poly1305-ietf")
			{
				Fail("algorithm", "unsupported encryption algorithm");
			}
			O.EncryptionKey = E.at("keyRef").get<std::string>();
			O.EncryptionVersion = E.at("keyVersion").get<std::string>();
			if (O.EncryptionKey.empty() || O.EncryptionVersion.empty())
			{
				Fail("key-reference", "encryption key reference and version are required");
			}
		}
		if (V.contains("signature"))
		{
			const auto& S = V.at("signature");
			Keys(S, {"algorithm", "keyRef", "keyVersion", "scope"}, "signature");
			if (S.at("algorithm") != "ed25519")
			{
				Fail("algorithm", "unsupported signing algorithm");
			}
			O.SigningKey = S.at("keyRef").get<std::string>();
			O.SigningVersion = S.at("keyVersion").get<std::string>();
			O.SignatureScope = S.value("scope", O.SignatureScope);
			if (O.SigningKey.empty() || O.SigningVersion.empty() || (O.SignatureScope != "content" && O.SignatureScope != "artifact"))
			{
				Fail("signature", "invalid signature policy");
			}
		}
		return O;
	}
	J ValidateConfiguration(std::string_view Text, const FBuildRequest& R)
	{
		auto C = Parse(Text, R.Limits);
		Keys(C, {"format", "formatVersion", "outputDirectory", "defaultTargets", "profiles", "platform", "targets"}, "build");
		if (C.at("format") != "veroscript-build" || C.at("formatVersion") != "0.1.1")
		{
			Fail("build-format", "unsupported build format");
		}
		if (!C.at("targets").is_object() || C.at("targets").empty() || C.at("targets").size() > R.Limits.MaxTargets)
		{
			Fail("targets", "invalid target count");
		}
		if (!C.contains("profiles"))
		{
			C["profiles"] = {{"release", J::object()}};
		}
		if (!C.at("profiles").is_object())
		{
			Fail("profiles", "profiles must be an object");
		}
		for (auto P = C["profiles"].begin(); P != C["profiles"].end(); ++P)
		{
			Id(P.key());
			Keys(P.value(), {"compile", "targetOverrides"}, "profile");
			Compile(P.value().value("compile", J::object()));
			const auto Overrides = P.value().value("targetOverrides", J::object());
			if (!Overrides.is_object())
			{
				Fail("overrides", "targetOverrides must be an object");
			}
			for (auto I = Overrides.begin(); I != Overrides.end(); ++I)
			{
				if (!C.at("targets").contains(I.key()))
				{
					Fail("override-target", "unknown target override", I.key());
				}
				Keys(I.value(), {"compile", "protection", "options"}, "targetOverride");
				if (I.value().contains("compile"))
				{
					Compile(I.value().at("compile"));
				}
				if (I.value().contains("protection"))
				{
					(void)Protection(I.value().at("protection"), false);
				}
			}
		}
		if (C.contains("defaultTargets"))
		{
			StringArray(C.at("defaultTargets"));
			for (const auto& D : C.at("defaultTargets"))
			{
				Id(D);
			}
		}
		std::set<std::string> Folded;
		for (auto I = C["targets"].begin(); I != C["targets"].end(); ++I)
		{
			Id(I.key());
			auto Name = I.key();
			std::transform(Name.begin(), Name.end(), Name.begin(), [](unsigned char X)
			{
				return static_cast<char>(std::tolower(X));
			});
			if (!Folded.insert(Name).second)
			{
				Fail("target-case", "target names collide on a supported filesystem", I.key());
			}
			auto& T = I.value();
			const auto K = T.at("kind").get<std::string>();
			if (K == "library" || K == "application")
			{
				Keys(T, {"kind", "dependsOn", "entry", "packageName", "packageVersion", "modules", "compile", "mode", "files", "output", "protection", "options"}, I.key());
				Input(T.at("entry"));
				Id(T.at("packageName").get<std::string>());
				if (T.contains("packageVersion"))
				{
					const auto S = T.at("packageVersion").get<std::string>();
					std::uint64_t N;
					const auto P = std::from_chars(S.data(), S.data() + S.size(), N);
					if (S.empty() || (S.size() > 1 && S[0] == '0') || S[0] == '+' || S[0] == '-' || P.ec != std::errc{} || P.ptr != S.data() + S.size())
					{
						Fail("package-version", "packageVersion must be a canonical uint64 decimal string");
					}
				}
				Compile(T.value("compile", J::object()));
				if (!T.value("modules", J::object()).is_object())
				{
					Fail("modules", "module mapping must be an object");
				}
				const auto Modules = T.value("modules", J::object());
				for (auto M = Modules.begin(); M != Modules.end(); ++M)
				{
					Safe(M.key());
					Input(M.value());
				}
				if (K == "application")
				{
					if (T.value("mode", "compiled") != "compiled" && T.value("mode", "compiled") != "source")
					{
						Fail("application-mode", "unsupported application mode");
					}
					Keys(T.value("options", J::object()), {"applicationId", "version"}, "application.options");
					if (T.value("mode", "compiled") == "source")
					{
						for (const auto* SearchKey : {"importPaths", "stdlibSearchPaths"})
						{
							if (!T.value("compile", J::object()).value(SearchKey, J::array()).empty())
							{
								Fail("source-search-path", "source applications use shipped files, module mappings and an explicit runtime environment; development search paths cannot be embedded", SearchKey);
							}
						}
					}
				}
				else if (T.contains("mode") || T.contains("files") || !T.value("options", J::object()).empty())
				{
					Fail("library-field", "library target does not accept mode, files or options");
				}
			}
			else if (K == "distribution")
			{
				Keys(T, {"kind", "dependsOn", "source", "files", "output", "protection", "options"}, I.key());
				Input(T.at("source"));
				if (!T.value("options", J::object()).empty())
				{
					Fail("distribution-options", "unsupported distribution options");
				}
			}
			else if (K == "deployment")
			{
				Keys(T, {"kind", "dependsOn", "usage", "roots", "output", "options"}, I.key());
				const auto U = T.value("usage", J::array({"runtime"}));
				StringArray(U);
				if (std::find(U.begin(), U.end(), "runtime") == U.end() || std::any_of(U.begin(), U.end(), [](const auto& V)
				{
					return V != "runtime" && V != "compile";
				}))
				{
					Fail("deployment-usage", "unsupported deployment usage");
				}
				StringArray(T.value("roots", J::array()));
				if (!T.value("options", J::object()).empty())
				{
					Fail("deployment-options", "unsupported deployment options");
				}
			}
			else if (K == "bundle")
			{
				Keys(T, {"kind", "dependsOn", "application", "files", "runtime", "environment", "launcher", "layout", "output", "protection", "options"}, I.key());
				Input(T.at("application"));
				for (const auto* F : {"runtime", "environment", "launcher"})
				{
					if (T.contains(F))
					{
						Component(T.at(F), F);
					}
				}
				const auto Layout = T.value("layout", "vsapp");
				if (Layout != "directory" && Layout != "vsapp" && Layout != "zip" && Layout != "single-file")
				{
					Fail("bundle-layout", "unsupported bundle layout");
				}
				if (Layout == "vsapp" && T.value("launcher", J::object()).value("mode", "none") != "none")
				{
					Fail("bundle-launcher", "a vsapp container cannot expose a native launcher");
				}
				if (!T.value("options", J::object()).empty())
				{
					Fail("bundle-options", "unsupported bundle options");
				}
			}
			else if (K == "custom")
			{
				Keys(T, {"kind", "dependsOn", "provider", "inputs", "outputs", "options", "cache"}, I.key());
				Id(T.at("provider").get<std::string>());
				if (!T.at("outputs").is_object() || T.at("outputs").empty())
				{
					Fail("outputs", "custom target needs named outputs");
				}
				for (auto O = T["outputs"].begin(); O != T["outputs"].end(); ++O)
				{
					Id(O.key());
					Keys(O.value(), {"path", "type", "kind"}, "output");
					Safe(O.value().at("path"));
					if (O.value().at("type") != "file" && O.value().at("type") != "directory")
					{
						Fail("output-type", "unsupported output type");
					}
					Id(O.value().at("kind"));
				}
				if (!T.value("inputs", J::object()).is_object())
				{
					Fail("inputs", "custom inputs must be an object");
				}
				const auto Inputs = T.value("inputs", J::object());
				for (auto N = Inputs.begin(); N != Inputs.end(); ++N)
				{
					Id(N.key());
					Input(N.value());
				}
				if (T.contains("cache") && !T.at("cache").is_boolean())
				{
					Fail("cache", "cache must be boolean");
				}
			}
			else if (K == "group")
			{
				Keys(T, {"kind", "dependsOn"}, I.key());
			}
			else
			{
				Fail("target-kind", "unknown target kind", K);
			}
			if (T.contains("dependsOn"))
			{
				StringArray(T.at("dependsOn"));
				for (const auto& D : T.at("dependsOn"))
				{
					Id(D);
				}
			}
			if (T.contains("files"))
			{
				Files(T.at("files"));
			}
			if (K != "custom" && K != "group")
			{
				const auto Output = T.at("output").get<std::string>();
				Safe(Output);
				auto Ext = NativePathText(PathUtil::FromUtf8(Output).extension());
				std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char X)
				{
					return static_cast<char>(std::tolower(X));
				});
				if ((K == "library" && Ext != ".vslib") || (K == "application" && Ext != ".vsapp") ||
				    (K == "distribution" && Ext != ".vspkg" && Ext != ".zip"))
				{
					Fail("output-extension", "output extension does not match target kind", Output);
				}
				if (K == "bundle" && ((T.value("layout", "vsapp") == "vsapp" && Ext != ".vsapp") || (T.value("layout", "vsapp") == "zip" && Ext != ".zip")))
				{
					Fail("output-extension", "bundle extension does not match its layout", Output);
				}
			}
			if (T.contains("protection"))
			{
				const auto P = Protection(T.at("protection"), K == "application" || K == "bundle");
				if ((K == "distribution" || (K == "application" && T.value("mode", "compiled") == "source")) && !P.EncryptionKey.empty())
				{
					Fail("encryption-kind", "this target cannot be encrypted");
				}
				if (K == "bundle" && (T.value("layout", "vsapp") == "directory" || T.value("layout", "vsapp") == "single-file") &&
				    !P.SigningKey.empty() && P.SignatureScope != "content")
				{
					Fail("signature-scope", "directory and single-file applications require content signatures; native executable signing is a separate platform step");
				}
			}
		}
		return C;
	}
}
