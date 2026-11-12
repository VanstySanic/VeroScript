// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptI18n.h"
#include "VeroScriptCompile.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptLocale.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <functional>
#include <fstream>
#include <filesystem>
#include <limits>
#include <thread>

using namespace VeroScript;
using namespace VeroScript::I18n;
namespace
{
	bool Expect(bool Ok, const char* Label)
	{
		if (!Ok)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Ok;
	}
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	std::shared_ptr<const IJitBackendFactory> Factory;

	class FResources final : public II18nResourceSnapshot
	{
	public:
		EI18nLookupResult Lookup(const FI18nLookupRequest& R, const FI18nWork&, FI18nMessage& Out, FI18nError&) const override
		{
			++Calls;
			if (Hook)
			{
				Hook();
			}

			if (R.Domain != "ui" || (R.Language != "en-US" && R.Language != "zh-CN"))
			{
				return EI18nLookupResult::NotFound;
			}
			if (R.Key == "items")
			{
				Out = {Prefix + (R.Language == "zh-CN" ? "共有 {n} 项" : "{n} items"), "vero.named"};
			}

			else if (R.Key == "empty")
			{
				Out = {"", "literal"};
			}

			else if (R.Key == "literal")
			{
				Out = {"raw {n}", "literal"};
			}

			else if (R.Key == "positional")
			{
				Out = {"count={0}", "std.format"};
			}

			else if (R.Key == "extension")
			{
				Out = {Prefix + "typed", "test.echo"};
			}

			else
			{
				return EI18nLookupResult::NotFound;
			}

			return EI18nLookupResult::Found;
		}

		explicit FResources(std::string Prefix = "") : Prefix(std::move(Prefix))
		{
		}

		std::string Prefix;

		std::function<void()> Hook;

		mutable std::atomic<unsigned> Calls{0};
	};
	class FFormatter final : public II18nFormatterSnapshot
	{
	public:
		//~ Begin II18nFormatterSnapshot Interface
		bool SupportsFormat(std::string_view F) const override
		{
			return F == "test.echo";
		}

		bool Format(
		    const FI18nFormatRequest& R, const FI18nWork&, std::string& Out, FI18nError& E) const override
		{
			++Calls;
			FI18nArgument A;
			if (!R.Arguments.Get("n", A, E)) return false;
			if (A.Kind() != EI18nArgumentKind::UInt64)
			{
				E.Status = EI18nStatus::ArgumentType;
				E.Diagnostic.Message = "lost original type";
				return false;
			}
			std::uint64_t N;
			if (!A.TryUInt64(N, E)) return false;
			Out = std::string(R.Pattern) + "|" + std::string(R.MatchedLanguage) + "|" + std::string(R.FormatLocale) + "|" + std::to_string(N);
			return true;
		}
		//~ End II18nFormatterSnapshot Interface

		mutable std::atomic<unsigned> Calls{0};
	};
	class FBrokenResources final : public II18nResourceSnapshot
	{
	public:
		EI18nLookupResult Lookup(
		    const FI18nLookupRequest&, const FI18nWork&, FI18nMessage&, FI18nError& E) const override
		{
			E.Status = EI18nStatus::ProviderFailure;
			E.Diagnostic.Message = "permission failure";
			return EI18nLookupResult::NotFound;
		}
	};
	class FFailedResources final : public II18nResourceSnapshot
	{
	public:
		EI18nLookupResult Lookup(const FI18nLookupRequest&, const FI18nWork&, FI18nMessage&, FI18nError& E) const override
		{
			E.Status = EI18nStatus::ProviderFailure;
			E.Diagnostic.MessageId = "test.provider_failure";
			E.Diagnostic.Message = "provider schema";
			E.Diagnostic.MessageArgs = {"original", "second"};
			return EI18nLookupResult::Failed;
		}
	};
	class FCustom final : public II18nCustomValue
	{
	public:
		//~ Begin II18nCustomValue Interface
		const FI18nCustomType& Type() const noexcept override
		{
			return *Descriptor;
		}
		bool Equals(const II18nCustomValue& R) const noexcept override
		{
			return Value == static_cast<const FCustom&>(R).Value;
		}
		const void* BorrowValue() const noexcept override
		{
			return &Value;
		}
		//~ End II18nCustomValue Interface

		FCustom(std::shared_ptr<const FI18nCustomType> T, int V) : Descriptor(std::move(T)), Value(V)
		{
		}

	private:
		std::shared_ptr<const FI18nCustomType> Descriptor;
		int Value;
	};

	bool OperationStabilityTests()
	{
		bool Ok = true;
		FI18nError E;
		FI18nArguments Args;
		Args.Set("n", FI18nArgument(std::uint64_t{7}), E);
		auto Services = std::make_shared<FI18nServices>();
		std::shared_ptr<FI18nContext> Context, Other;
		CreateContext({}, Services, Context, E);
		FI18nConfiguration Config;
		Config.Language = "zh-CN";
		Config.FormatOptions.Set("unsupported", FI18nArgument(true), E);
		CreateContext(Config, {}, Other, E);
		FI18nText Text;
		FI18nText::Message("ui", "items", Args, Text, E);
		auto Resources = std::make_shared<FResources>();
		Resources->Hook = [&Text]()
		{
			Text = FI18nText("replacement");
		};
		Services->PublishResources(Resources, E);
		FI18nResolvedText Result;
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Text, Result, E) && Result.Value == "7 items" && Text.IsLiteral(),
		    "resource callback cannot change the active text or its frozen parameters");
		FI18nText::Message("ui", "items", Args, Text, E);
		FI18nSnapshot View;
		Resources = std::make_shared<FResources>();
		Resources->Hook = [&View, &Other]()
		{
			View = Other->CaptureSnapshot();
		};
		Services->PublishResources(Resources, E);
		View = Context->CaptureSnapshot();
		const auto OriginalVersion = View.Version();
		FI18nMessageTemplate Pattern;
		Ok &= Expect(View.TryLookup("ui", "items", Pattern, E) && Pattern.Version == OriginalVersion && View.Version() != OriginalVersion,
		    "lookup retains its originating snapshot if the caller replaces the snapshot variable");
		View = Context->CaptureSnapshot();
		Ok &= Expect(View.TryResolve(Text, Result, E) && Result.Value == "7 items" && Result.Version == OriginalVersion,
		    "resolve retains the same originating snapshot through lookup and formatting");
		View = Context->CaptureSnapshot();
		std::string Out;
		struct FReplacement
		{
			FI18nSnapshot* Target;
			FI18nSnapshot Value;
		} Replacement{&View, Other->CaptureSnapshot()};
		FI18nWork Swap{[](void* P, std::uint64_t)
		{
			const auto& R = *static_cast<FReplacement*>(P);
			*R.Target = R.Value;
			return true;
		},
		    &Replacement};
		Ok &= Expect(
		    View.TryFormat("{n}", Args, Out, E, Swap) && Out == "7" && View.Version() != OriginalVersion,
		    "format retains its originating configuration through native checkpoints");
		View = Context->CaptureSnapshot();
		FI18nWork Reset{[](void* P, std::uint64_t)
		{
			*static_cast<FI18nSnapshot*>(P) = {};
			return true;
		},
		    &View};
		Ok &= Expect(View.TryFormat("{n}", Args, Out, E, Reset) && Out == "7" && !View.IsValid(),
		    "active operation remains valid after the caller resets its snapshot variable");
		FI18nMessageTemplate Alias;
		Alias.Pattern = "ui";
		Alias.FormatId = "missing";
		Ok &= Expect(!Context->CaptureSnapshot().TryLookup(Alias.Pattern, Alias.FormatId, Alias, E) &&
		        E.Domain == "ui" && E.Key == "missing" && !Alias.Valid,
		    "failed lookup preserves borrowed query identity before clearing an aliased result");
		Ok &= Expect(!Context->CaptureSnapshot().TryFormat("{missing}", Args, Out, E) && E.Language == "en-US" &&
		        E.ArgumentName == "missing" && E.Diagnostic.MessageArgs[3] == "en-US" && E.Diagnostic.MessageArgs[4] == "missing",
		    "direct formatting errors retain the captured language and named parameter");
		return Ok;
	}

	bool NativeTests()
	{
		bool Ok = true;
		FI18nError E;
		std::string S;
		for (const auto& Pair : std::vector<std::pair<std::string, std::string>>{
		         {"EN-us", "en-US"}, {"zh-hANS-cn", "zh-Hans-CN"}, {"en-US-U-nu-LATN", "en-US-u-nu-latn"},
		         {"x-App", "x-app"}, {"i-KLINGON", "i-klingon"}, {"en-b-bbb-a-aaa", "en-a-aaa-b-bbb"}})
		{
			Ok &= Expect(NormalizeLanguageTag(Pair.first, S, E) && S == Pair.second, "language normalization");
			Ok &= Expect(NormalizeLanguageTag(S, S, E) && S == Pair.second, "language normalization is idempotent and supports aliasing");
		}
		for (auto Bad :
		    {"", "en_US.UTF-8", " en-US", "en-u", "en-a-aaa-a-bbb", "de-1901-1901", "*", "en--US"})
		{
			Ok &= Expect(!NormalizeLanguageTag(Bad, S, E) && E.Status == EI18nStatus::InvalidConfiguration,
			    "invalid language rejected");
		}

		double D;
		float F;
		std::int64_t I;
		std::uint64_t U;
		Ok &= Expect(!FI18nArgument(std::uint64_t{9007199254740993ULL}).TryDouble(D, E), "large integer is not rounded into double");
		Ok &= Expect(FI18nArgument(std::uint64_t{1} << 63).TryDouble(D, E) && D == 0x1p63, "exact large integer conversion");
		Ok &= Expect(!FI18nArgument(std::numeric_limits<std::uint64_t>::max()).TryInt64(I, E), "uint64 cannot overflow signed");
		Ok &= Expect(FI18nArgument(-0x1p63).TryInt64(I, E) && I == std::numeric_limits<std::int64_t>::min(), "int64 lower boundary");
		Ok &= Expect(!FI18nArgument(0x1p63).TryInt64(I, E) && !FI18nArgument(0x1p64).TryUInt64(U, E), "exclusive floating upper boundaries");
		Ok &= Expect(!FI18nArgument(-1).TryUInt64(U, E) && !FI18nArgument(1.25).TryInt64(I, E), "negative and fractional conversion rejected");
		Ok &= Expect(!FI18nArgument(0.1).TryFloat(F, E) && FI18nArgument(1.25).TryFloat(F, E), "float narrowing is exact");
		Ok &= Expect(FI18nArgument(-0.0) != FI18nArgument(0.0), "negative zero snapshot identity");
		const double Nan = std::numeric_limits<double>::quiet_NaN();
		Ok &= Expect(FI18nArgument(Nan) == FI18nArgument(Nan) && FI18nArgument(Nan).TryDouble(D, E), "same-kind NaN is preserved");
		Ok &= Expect(!FI18nArgument(Nan).TryFloat(F, E), "NaN payload is not silently narrowed");

		FI18nArguments Args;
		Args.Set("n", FI18nArgument(std::numeric_limits<std::uint64_t>::max()), E);
		FI18nArguments Copy = Args;
		Copy.Set("n", FI18nArgument(1), E);
		Ok &= Expect(Args != Copy && TryFormatNamed("{n}", Args, S, E) && S == "18446744073709551615", "copy-on-write and exact integer output");
		Args.Set("名字", FI18nArgument("Vero"), E);
		bool Present = false;
		Ok &= Expect(Args.Contains("名字", Present, E) && Present, "Unicode names are general data");
		Ok &= Expect(!TryFormatNamed("{名字}", Args, S, E) && E.Status == EI18nStatus::InvalidPattern, "basic formatter validates its own grammar");
		Ok &= Expect(!Args.Set(std::string("a\0b", 3), FI18nArgument(1), E), "NUL name rejected");
		Ok &= Expect(TryFormatNamed("{{{n}}}", Args, S, E) && S == "{18446744073709551615}", "escaped braces and no recursive substitution");
		Args.Set("raw", FI18nArgument(std::string("{n}\0", 4)), E);
		Ok &= Expect(TryFormatNamed("{raw}", Args, S, E) && S == std::string("{n}\0", 4), "string payload preserves braces and NUL");
		Args.Set("f", FI18nArgument(-0.0f), E);
		Ok &= Expect(TryFormatNamed("{f}", Args, S, E) && S == "-0", "float output preserves sign of zero");
		Args.Set("f", FI18nArgument(Nan), E);
		Ok &= Expect(!TryFormatNamed("{f}", Args, S, E) && E.Status == EI18nStatus::NumericConversion, "basic formatter rejects non-finite values");
		Args.Set("signed", FI18nArgument(std::numeric_limits<std::int64_t>::min()), E);
		Ok &= Expect(TryFormatNamed("{signed}", Args, S, E) && S == "-9223372036854775808", "signed minimum formats without conversion overflow");
		Ok &= Expect(!TryFormatNamed("{n}", Args, S, E, {nullptr, nullptr, 3}) && S.empty(), "output limit has no partial result");
		FI18nArgument Missing;
		Ok &= Expect(!Args.Get("missing", Missing, E) && E.ArgumentName == "missing" &&
		        E.Diagnostic.MessageArgs.size() == 5 && E.Diagnostic.MessageArgs[4] == "missing",
		    "argument lookup preserves the named diagnostic slot");
		Ok &= Expect(!TryFormatNamed("{missing}", Args, S, E) && E.ArgumentName == "missing" &&
		        E.Diagnostic.MessageArgs.size() == 5 && E.Diagnostic.MessageArgs[4] == "missing",
		    "named formatter preserves the named diagnostic slot");
		unsigned Checks = 0;
		FI18nWork Cancel{[](void* P, std::uint64_t)
		{
			return ++*static_cast<unsigned*>(P) < 4;
		},
		    &Checks};
		Ok &= Expect(!TryFormatNamed(std::string(2048, 'x'), Args, S, E, Cancel) && S.empty() &&
		        E.Status == EI18nStatus::Interrupted,
		    "long replacement observes cancellation");
		FI18nArguments FrozenInput = Args;
		FI18nWork Mutate{[](void* P, std::uint64_t)
		{
			static_cast<FI18nArguments*>(P)->Clear();
			return true;
		},
		    &FrozenInput};
		Ok &= Expect(TryFormatNamed("{n}", FrozenInput, S, E, Mutate) && S == "18446744073709551615" &&
		        FrozenInput.Size() == 0,
		    "parameters frozen before the first host checkpoint");

		auto Type = std::make_shared<FI18nCustomType>();
		Type->Name = "test.date.v1";
		FI18nArgument Custom, Same, Different;
		Ok &= Expect(MakeCustomArgument(std::make_shared<FCustom>(Type, 7), Custom, E) &&
		        MakeCustomArgument(std::make_shared<FCustom>(Type, 7), Same, E) && Custom == Same,
		    "custom value equality");
		auto OtherType = std::make_shared<FI18nCustomType>(*Type);
		MakeCustomArgument(std::make_shared<FCustom>(OtherType, 7), Different, E);
		const void* View = nullptr;
		Ok &= Expect(Custom != Different && Custom.TryCustomView(*Type, View, E) && *static_cast<const int*>(View) == 7 &&
		        !Custom.TryCustomView(*OtherType, View, E),
		    "custom descriptor is a checked protocol identity");

		auto Services = std::make_shared<FI18nServices>();
		auto Resources = std::make_shared<FResources>();
		auto Formatter = std::make_shared<FFormatter>();
		Services->PublishResources(Resources, E);
		Services->PublishFormatter(Formatter, E);
		auto ModulePin = std::make_shared<int>(42);
		std::weak_ptr<int> WeakPin = ModulePin;
		const std::shared_ptr<const void> OpaquePin(ModulePin, nullptr);
		Services->PublishResources(Resources, E, OpaquePin);
		Services->PublishFormatter(Formatter, E, OpaquePin);
		ModulePin.reset();
		const auto Pinned = Services->Acquire();
		const auto ResourceRevision = Pinned->ResourceRevision, FormatterRevision = Pinned->FormatterRevision;
		Services->PublishResources(Resources, E);
		Services->PublishFormatter(Formatter, E);
		Ok &= Expect(!WeakPin.expired() && Services->Acquire()->ResourceOwner.use_count() && Services->Acquire()->FormatterOwner.use_count() &&
		        Services->Acquire()->ResourceRevision == ResourceRevision && Services->Acquire()->FormatterRevision == FormatterRevision,
		    "repeated view publication without an owner preserves its original module pins and revision");
		FI18nConfiguration Config;
		Config.Language = "zh-CN";
		Config.FallbackLanguages = {"zh-CN", "en-US", "en-US"};
		std::shared_ptr<FI18nContext> Context;
		Ok &= Expect(CreateContext(Config, Services, Context, E), "independent native context");
		const auto DiagnosticLanguage = Locale::GetLanguageTag();
		FI18nText Text;
		FI18nText::Message("ui", "items", Args, Text, E);
		FValue ScriptText, ScriptCopy;
		Ok &= Expect(ToScriptValue(Text, ScriptText, E) && ToScriptValue(Text, ScriptCopy, E) &&
		        Runtime::ValuesEqual(FBytecodeModule{}, ScriptText, ScriptCopy),
		    "generic host value equality needs no key hash");
		auto Forged = std::make_shared<FHostObject>(*std::get<FHostObjectHandle>(ScriptText));
		FI18nText Rejected;
		Ok &= Expect(!FromScriptValue(FValue(Forged), Rejected, E), "native boundary rejects a forged wrapper protocol");
		FI18nResolvedText Result;
		auto Old = Context->CaptureSnapshot();
		Ok &= Expect(Old.TryResolve(Text, Result, E) && Result.Value == "共有 18446744073709551615 项", "resource resolution with typed params");
		std::shared_ptr<FI18nContext> English;
		Context->Clone(English, E);
		English->SetLanguage("en-US", E);
		Ok &= Expect(English->CaptureSnapshot().TryResolve(Text, Result, E) && Result.Value == "18446744073709551615 items" &&
		        Context->Configuration().Language == "zh-CN" && Locale::GetLanguageTag() == DiagnosticLanguage,
		    "contexts and diagnostic language are independent");
		const auto Stamp = Context->CaptureSnapshot().Version();
		Context->Configure(Context->Configuration(), E);
		Ok &= Expect(Stamp == Context->CaptureSnapshot().Version(), "same configuration is a no-op");
		Services->PublishResources(Resources, E);
		Ok &= Expect(Stamp == Context->CaptureSnapshot().Version(), "same immutable resource view is a no-op");
		Services->PublishResources(std::make_shared<FResources>("new:"), E);
		Ok &= Expect(Stamp != Context->CaptureSnapshot().Version() && Old.TryResolve(Text, Result, E) && Result.Value.substr(0, 3) != "new", "old snapshot retains resource contents");
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Text, Result, E) && Result.Value.find("new:") == 0, "next capture observes new resources");
		Context->SetLanguage("fr-FR", E);
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Text, Result, E) && Result.MatchedLanguage == "zh-CN", "explicit primary fallback is preserved after language switch");
		Context->SetFallbackLanguages({"en-US"}, E);
		FI18nText Extension;
		FI18nText::Message("ui", "extension", Args, Extension, E);
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Extension, Result, E) && Result.Value == "new:typed|en-US|fr-FR|18446744073709551615", "message language and format locale reach typed extension");
		const auto Follow = Context->CaptureSnapshot().Version();
		Context->SetFormatLocale("fr-FR", E);
		Ok &= Expect(Follow != Context->CaptureSnapshot().Version(), "following and explicit locale have distinct configuration intent");
		Context->SetFormatLocale("", E);
		FI18nMessageTemplate Template;
		Ok &= Expect(Context->CaptureSnapshot().TryLookup("ui", "positional", Template, E) && Template.FormatId == "std.format", "lookup is separate from formatting");
		FI18nText Positional;
		FI18nText::Message("ui", "positional", {}, Positional, E);
		Ok &= Expect(!Context->CaptureSnapshot().TryResolve(Positional, Result, E) && E.Status == EI18nStatus::UnsupportedFormat, "unknown grammar is not guessed");
		FI18nText Empty;
		FI18nText::Message("ui", "empty", {}, Empty, E);
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Empty, Result, E) && Result.Valid && Result.Value.empty(), "empty found message is successful");
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(FI18nText("{literal}"), Result, E) && Result.Value == "{literal}", "literal bypasses providers");
		Ok &= Expect(!Old.TryResolve(FI18nText("long"), Result, E, {nullptr, nullptr, 1}) && !Result.Valid, "old snapshot uses current operation limits");
		Services->PublishResources(std::make_shared<FBrokenResources>(), E);
		Ok &= Expect(!Context->CaptureSnapshot().TryResolve(Text, Result, E) && E.Status == EI18nStatus::ProviderFailure, "contradictory NotFound does not hide a real failure");
		Services->PublishResources(std::make_shared<FFailedResources>(), E);
		Ok &= Expect(!Context->CaptureSnapshot().TryResolve(Text, Result, E) && E.Domain == "ui" && E.Key == "items" &&
		        E.Diagnostic.MessageId == "test.provider_failure" && E.Diagnostic.MessageArgs == std::vector<std::string>({"original", "second"}),
		    "provider diagnostic schema is preserved with machine-readable resource identity");

		// Reentry is checked per thread and per originating context identity.
		auto Recursive = std::make_shared<FResources>();
		std::weak_ptr<FI18nContext> Weak = Context;
		std::atomic<bool> ReentryRejected{false};
		Recursive->Hook = [Weak, &ReentryRejected]()
		{
			if (auto C = Weak.lock())
			{
				FI18nResolvedText R;
				FI18nError X;
				ReentryRejected = !C->CaptureSnapshot().TryResolve(FI18nText("nested"), R, X) &&
				    X.Status == EI18nStatus::ReentrantOperation;
			}
		};
		Services->PublishResources(Recursive, E);
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Text, Result, E) && ReentryRejected, "recursive context operation rejected without deadlock");
		Services->PublishResources(Resources, E);
		Ok &= Expect(Context->CaptureSnapshot().TryResolve(Text, Result, E), "operation scope cleaned after reentry");
		auto AtomicServices = std::make_shared<FI18nServices>();
		AtomicServices->PublishResources(std::make_shared<FResources>("atomic:"), E);
		FI18nConfiguration AtomicConfig;
		AtomicConfig.Language = "en-US";
		const auto BeforePair = Context->CaptureSnapshot();
		Ok &= Expect(Context->ConfigureAndSetServices(AtomicConfig, AtomicServices, E) &&
		        Context->CaptureSnapshot().TryResolve(Text, Result, E) && Result.Value == "atomic:18446744073709551615 items" &&
		        BeforePair.TryResolve(Text, Result, E) && Result.Value == "18446744073709551615 items",
		    "atomic configuration/service replacement preserves prior snapshot");
		Context->ConfigureAndSetServices(Config, Services, E);

		std::promise<void> Start;
		auto Ready = Start.get_future().share();
		std::atomic<bool> ThreadsOk{true};
		std::thread A(
		    [&]()
		{
			Ready.wait();
			for (unsigned N = 0; N < 100; ++N)
			{
				FI18nError X;
				if (!Context->SetLanguage("en-US", X) ||
				    !Services->PublishResources(std::make_shared<FResources>(), X))
				{
					ThreadsOk = false;
				}
			}
		});
		std::thread B(
		    [&]()
		{
			Ready.wait();
			for (unsigned N = 0; N < 100; ++N)
			{
				FI18nError X;
				if (!Context->SetFormatLocale("ja-JP", X) ||
				    !Services->PublishFormatter(std::make_shared<FFormatter>(), X))
				{
					ThreadsOk = false;
				}
			}
		});
		Start.set_value();
		A.join();
		B.join();
		Ok &= Expect(ThreadsOk && Context->Configuration().Language == "en-US" && Context->Configuration().FormatLocale == "ja-JP" &&
		        Services->Acquire()->Resources && Services->Acquire()->Formatter,
		    "partial concurrent updates preserve the other fields");
		Context.reset();
		Services.reset();
		Ok &= Expect(Old.TryResolve(Text, Result, E), "snapshot owns all required state after context destruction");
		return Ok;
	}

	bool Run(const char* Label, std::string_view Source, std::shared_ptr<FI18nServices> Services = {},
	    const char* ExpectedError = nullptr, FI18nLimits Limits = {}, std::vector<FNativeFunction> Natives = {}, const IHostBindingCatalog* Bindings = nullptr, bool CheckScalarBridge = false)
	{
		auto Config = Options;
		Config.Natives = Natives.empty() ? nullptr : &Natives;
		Config.Bindings = Bindings;
		auto Compiled = VeroScriptCompile::CompileSource(Source, "i18n-test.vs", Config);
		if (!Compiled.bSuccess)
		{
			std::cerr << Label << ": compile: " << Compiled.Error.Location.Line << ": "
			          << Compiled.Error.Message << '\n';
			return false;
		}
		for (auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}

			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (!VeroScriptBytecodeIO::SerializeBytecode(*Compiled.Module, 131, Bytes, Error))
			{
				std::cerr << Error;
				return false;
			}
			auto Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 131, Error);
			if (!Module)
			{
				std::cerr << Error;
				return false;
			}
			if (Natives.empty() && Backend == EExecutionBackend::Mixed)
			{
				FPackageBuildOptions P;
				P.PackageName = "i18n-test";
				FPackageData Package;
				if (!BuildPackage(*Module, P, Bytes, Error) || !ReadPackage(FByteView(Bytes), Package, Error))
				{
					std::cerr << Error;
					return false;
				}
				Module = std::move(Package.Module);
			}
			std::ostringstream Out, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Out, Input, Errors);
			VeroScript::Test::Configure(Vm);
			Vm.SetI18nServices(Services, Limits);
			Vm.SetExecutionBackend(Backend);
			if (Factory && !Vm.SetJitBackendFactory(Factory, Error))
			{
				std::cerr << Error;
				return false;
			}
			const bool Success =
			    Vm.Execute(*Module, ERunMode::Script, {}, Natives.empty() ? nullptr : &Natives);
			if (Success != !ExpectedError || (ExpectedError && Vm.GetError().MessageId != ExpectedError))
			{
				std::cerr << Label << ": backend " << static_cast<int>(Backend) << ": "
				          << Vm.GetError().Location.Line << ": " << Vm.GetError().MessageId << ": "
				          << Vm.GetError().Message << '\n';
				return false;
			}
			if (CheckScalarBridge)
			{
				if (!Expect(Vm.GetError().MessageArgs ==
				                std::vector<std::string>({"owned diagnostic", "ui", "items", "en-US", "n"}) &&
				            Vm.GetError().Location.Line > 0,
				        "native diagnostic preserves its owned arguments and script call site"))
				{
					return false;
				}

				if (Backend == EExecutionBackend::Jit &&
				    (Factory->GetDescription().Capabilities & JitDirectScalarCallsCapability) &&
				    !Expect(Vm.GetJitStats().ScalarNativeFastPathCalls > 0,
				        "diagnostic test enters the specialized native JIT bridge"))
				{
					return false;
				}
			}
		}
		return true;
	}
}
int main(int Argc, char** Argv)
{
	if (Argc > 1)
	{
		Options.StdlibSearchPaths = {Argv[1]};
	}

	const char* BackendPath = Argc > 2 ? Argv[2] : std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (BackendPath && *BackendPath)
	{
		std::string E;
		Factory = LoadJitBackend(BackendPath, E);
		if (!Factory)
		{
			std::cerr << E;
			return 2;
		}
	}
	bool Ok = NativeTests();
	Ok &= OperationStabilityTests();
	FI18nError Error;
	if (Argc > 1)
	{
		std::ifstream File(std::filesystem::path(Argv[1]).parent_path() / "examples/i18n_demo.vs", std::ios::binary);
		std::ostringstream Source;
		Source << File.rdbuf();
		Ok &= Expect(File.good() && !Source.str().empty(), "script example is available");
		if (!Source.str().empty())
		{
			Ok &= Run("script example", Source.str());
		}
	}
	auto Services = std::make_shared<FI18nServices>();
	Services->PublishResources(std::make_shared<FResources>(), Error);
	Services->PublishFormatter(std::make_shared<FFormatter>(), Error);
	Ok &= Run("explicit i18n binding catalog", R"VS(void main(){std::i18n::context c=std::i18n::context();assert(c.resolve(std::i18n::text("owned"))=="owned");})VS",
	    Services, nullptr, {}, {}, &I18n::GetBindingCatalog());
	Ok &= Run("typed values, copies and configuration", R"VS(
void main() {
    std::i18n::arguments a=std::i18n::arguments(); a.set("n", std::i18n::argument(18446744073709551615u64));
    assert(a.get("n").kind() == std::i18n::argument_kind::uint64_value);
    assert(a.get("n").as_uint64() == 18446744073709551615u64);
    std::i18n::arguments b=a; b.set("n",std::i18n::argument(1)); assert(a!=b);
    std::i18n::text t=std::i18n::text::message("ui","items",a);
    std::i18n::text copy=t; assert(copy==t);
    a.set("n",std::i18n::argument(2)); assert(t.params()!=a);
    std::i18n::configuration config=std::i18n::configuration(); config.language="zh-CN";
    array<string> fallbacks={"en-US"}; config.fallback_languages=fallbacks;
    std::i18n::context c=std::i18n::context(config);
    assert(c.configuration().language=="zh-CN");
    assert(c.resolve(t)=="共有 18446744073709551615 项");
    std::i18n::context other=c.clone(); other.set_language("en-US");
    assert(other.resolve(t)=="18446744073709551615 items"); assert(c.configuration().language=="zh-CN");
    std::i18n::snapshot old=c.snapshot(); std::i18n::version v=old.version();
    assert(v.context_id > 0u64 && v.configuration_revision >= 1u64);
    assert(v.resource_revision > 0u64 && v.formatter_revision > 0u64);
    c.set_language("en-US"); assert(v!=c.version()); assert(old.resolve(t)=="共有 18446744073709551615 项");
    std::i18n::resolved_text result=old.resolve_versioned(t); assert(result.version()==v);
    assert(std::format("[{}]",result.value())=="[共有 18446744073709551615 项]");
    assert(c.resolve(std::i18n::text("literal {n}"))=="literal {n}");
    assert(std::i18n::normalize_language_tag("EN-us-U-nu-LATN")=="en-US-u-nu-latn");
})VS",
	    Services);
	Ok &= Run("containers, generics, callable and attribute", R"VS(
import "attribute.vs";
T identity<T>(T v){return v;}
struct Box {public:std::i18n::text value=std::i18n::text();};
std::i18n::text next(){return std::i18n::text("dynamic");}
void main(){
    std::i18n::text t=std::i18n::text("hello"); array<std::i18n::text> values={t};
    map<string,std::i18n::text> items={}; items["a"]=t;
    assert(values[0]==items["a"] && identity(t)==t);
    Box box=Box(); box.value=t; Box copy=box; assert(copy.value==t);
    optional<std::i18n::text> item=optional<std::i18n::text>(t); assert(item.get()==t);
    std::attribute<std::i18n::text> label=t; std::i18n::context c=std::i18n::context();
    assert(c.resolve(label.get())=="hello"); label.bind(next); assert(c.resolve(label.get())=="dynamic");
})VS");
	Ok &= Run("formatter and lookup interfaces", R"VS(
void main(){std::i18n::context c=std::i18n::context(); std::i18n::arguments a=std::i18n::arguments(); a.set("n",std::i18n::argument(18446744073709551615u64));
assert(c.format("{n}",a)=="18446744073709551615");
assert(c.format_with("typed",a,"test.echo")=="typed|en-US|en-US|18446744073709551615");
auto p=c.snapshot().lookup("ui","positional"); assert(p.format_id()=="std.format"); assert(std::format(p.pattern(),42)=="count=42");
assert(c.resolve(std::i18n::text::message_key("ui","empty"))=="");}
)VS",
	    Services);
	Ok &= Run("missing resolver", R"VS(void main(){std::i18n::context c=std::i18n::context();c.resolve(std::i18n::text::message_key("ui","items"));})VS", {}, "diag.i18n_missing_resolver");
	Ok &= Run("precision failure", R"VS(void main(){std::i18n::argument a=std::i18n::argument(9007199254740993u64);a.as_double();})VS", {}, "diag.i18n_numeric_conversion");
	Ok &= Run("output budget", R"VS(void main(){std::i18n::context c=std::i18n::context();c.resolve(std::i18n::text("long"));})VS", {}, "diag.i18n_resource_limit", {1});
	Ok &= Run("unknown format", R"VS(void main(){std::i18n::context c=std::i18n::context();c.resolve(std::i18n::text::message_key("ui","positional"));})VS", Services, "diag.i18n_unsupported_format");
	Ok &= Run("missing parameter", R"VS(void main(){std::i18n::context c=std::i18n::context();c.resolve(std::i18n::text::message_key("ui","items"));})VS", Services, "diag.i18n_missing_argument");
	Ok &= Run("default snapshot invalid", R"VS(void main(){std::i18n::snapshot s=std::i18n::snapshot();s.version();})VS", {}, "diag.i18n_invalid_state");
	Ok &= Run("worker inherits service", R"VS(
import "concurrent.vs";
string work(){std::i18n::context c=std::i18n::context();return c.resolve(std::i18n::text::message_key("ui","literal"));}
void main(){auto t=std::concurrent::spawn_task<string>(work);assert(t.await()=="raw {n}");}
)VS",
	    Services);
	// A scalar native exercises the specialized JIT bridge's current services and
	// owned diagnostic output, separately from unsupported host-value lowering.
	FNativeFunction Fail;
	Fail.Name = "host_i18n_failure";
	Fail.ReturnType = ENativeType::Int;
	Fail.Callback = [](const FNativeCallContext& C, FValue&, std::string& E)
	{
		if (!C.I18nServices || !C.DiagnosticOutput)
		{
			E = "missing current call services";
			return false;
		}
		C.DiagnosticOutput->MessageId = "diag.i18n_invalid_pattern";
		C.DiagnosticOutput->Message = "owned diagnostic";
		C.DiagnosticOutput->MessageArgs = {"owned diagnostic", "ui", "items", "en-US", "n"};
		return false;
	};
	Ok &= Run("native diagnostic through scalar bridge", "int fail(){return host_i18n_failure();}void main(){fail();}", Services, "diag.i18n_invalid_pattern", {}, {Fail}, nullptr, true);
	return Ok ? 0 : 1;
}
