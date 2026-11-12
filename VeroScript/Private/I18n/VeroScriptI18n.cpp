// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptI18nInternal.h"
#include <algorithm>
#include <atomic>
#include <limits>

namespace VeroScript::I18n::Detail
{
	namespace
	{
		thread_local std::vector<std::uint64_t> ActiveContexts;
	}
	FOperationScope::FOperationScope(std::uint64_t Id, FI18nError& E)
	{
		if (!Id)
		{
			Fail(E, EI18nStatus::InvalidState, "invalid context snapshot");
			return;
		}
		if (std::find(ActiveContexts.begin(), ActiveContexts.end(), Id) != ActiveContexts.end())
		{
			Fail(E, EI18nStatus::ReentrantOperation, "recursive internationalization operation");
			return;
		}
		ActiveContexts.push_back(Id);
		Entered = true;
	}
	FOperationScope::~FOperationScope()
	{
		if (Entered)
		{
			ActiveContexts.pop_back();
		}
	}
}

namespace VeroScript::I18n
{
	using namespace Detail;
	struct FI18nServices::FState
	{
		mutable std::mutex Mutex;
		std::shared_ptr<const FI18nServicesSnapshot> Current = std::make_shared<FI18nServicesSnapshot>();
	};
	FI18nServices::FI18nServices() : State(std::make_unique<FState>())
	{
	}
	FI18nServices::~FI18nServices() = default;
	std::shared_ptr<const FI18nServicesSnapshot> FI18nServices::Acquire() const
	{
		std::lock_guard<std::mutex> Lock(State->Mutex);
		return State->Current;
	}
	bool FI18nServices::Publish(FI18nProviders Providers, FI18nError& E)
	{
		if (!Providers.Resources)
		{
			Providers.ResourceOwner.reset();
		}

		if (!Providers.Formatter)
		{
			Providers.FormatterOwner.reset();
		}

		return PublishImpl(std::move(Providers), true, true, E);
	}
	bool FI18nServices::PublishResources(std::shared_ptr<const II18nResourceSnapshot> V, FI18nError& E, std::shared_ptr<const void> Owner)
	{
		FI18nProviders P;
		P.Resources = std::move(V);
		if (P.Resources)
		{
			P.ResourceOwner = std::move(Owner);
		}

		return PublishImpl(std::move(P), true, false, E);
	}
	bool FI18nServices::PublishFormatter(std::shared_ptr<const II18nFormatterSnapshot> V, FI18nError& E, std::shared_ptr<const void> Owner)
	{
		FI18nProviders P;
		P.Formatter = std::move(V);
		if (P.Formatter)
		{
			P.FormatterOwner = std::move(Owner);
		}

		return PublishImpl(std::move(P), false, true, E);
	}
	bool FI18nServices::PublishImpl(FI18nProviders Providers, bool Resources, bool Formatter, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			for (;;)
			{
				auto Old = Acquire();
				// Re-publishing an identical view without an owner is a no-op, not
				// permission to unload the module containing its virtual functions.
				const auto ResourceOwner = Providers.ResourceOwner.use_count() ? Providers.ResourceOwner
				    : Providers.Resources == Old->Resources                    ? Old->ResourceOwner
				                                                               : nullptr;
				const auto FormatterOwner = Providers.FormatterOwner.use_count()
				    ? Providers.FormatterOwner
				    : Providers.Formatter == Old->Formatter ? Old->FormatterOwner
				                                            : nullptr;
				const auto SameOwner = [](const auto& A, const auto& B)
				{
					return !A.owner_before(B) && !B.owner_before(A);
				};
				const bool R = Resources &&
				    (Old->Resources != Providers.Resources ||
				        !SameOwner(Old->ResourceOwner, ResourceOwner));
				const bool F = Formatter &&
				    (Old->Formatter != Providers.Formatter ||
				        !SameOwner(Old->FormatterOwner, FormatterOwner));
				if ((R && Old->ResourceRevision == std::numeric_limits<std::uint64_t>::max()) ||
				    (F && Old->FormatterRevision == std::numeric_limits<std::uint64_t>::max()))
				{
					return Fail(E, EI18nStatus::ResourceLimit, "service revision exhausted");
				}

				auto Next = std::make_shared<FI18nServicesSnapshot>(*Old);
				if (R)
				{
					Next->ResourceOwner = ResourceOwner;
					Next->Resources = Providers.Resources;
					++Next->ResourceRevision;
				}
				if (F)
				{
					Next->FormatterOwner = FormatterOwner;
					Next->Formatter = Providers.Formatter;
					++Next->FormatterRevision;
				}
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					if (State->Current != Old)
					{
						continue;
					}

					if (R || F)
					{
						State->Current = std::move(Next);
					}

					return true;
				}
			}
		});
	}

	struct FI18nContext::FState
	{
		mutable std::mutex Mutex;
		std::uint64_t Id = 0;
		std::shared_ptr<const FContextConfiguration> Current;
	};
	FI18nContext::~FI18nContext() = default;
	FI18nSnapshot FI18nContext::CaptureSnapshot() const
	{
		auto Next = std::make_shared<FI18nSnapshot::FData>();
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			Next->Context = State->Current;
		}
		if (Next->Context->Services)
		{
			Next->Services = Next->Context->Services->Acquire();
		}

		else
		{
			static const auto Empty = std::make_shared<const FI18nServicesSnapshot>();
			Next->Services = Empty;
		}
		Next->Stamp = {State->Id, Next->Context->Revision, Next->Services->ResourceRevision, Next->Services->FormatterRevision};
		FI18nSnapshot Result;
		Result.Data = std::move(Next);
		return Result;
	}
	bool CreateContext(const FI18nConfiguration& Config, std::shared_ptr<FI18nServices> Services,
	    std::shared_ptr<FI18nContext>& Out, FI18nError& E)
	{
		const bool Ok = Boundary(E,
		    [&]()
		{
			auto Initial = std::make_shared<FContextConfiguration>();
			if (!NormalizeConfiguration(Config, Initial->Config, E)) return false;
			Initial->Services = std::move(Services);
			auto Next = std::shared_ptr<FI18nContext>(new FI18nContext);
			static std::atomic<std::uint64_t> LastId{0};
			auto Id = LastId.load(std::memory_order_relaxed);
			do
			{
				if (Id == std::numeric_limits<std::uint64_t>::max())
				{
					return Fail(E, EI18nStatus::ResourceLimit, "context identity exhausted");
				}
			}
			while (!LastId.compare_exchange_weak(Id, Id + 1, std::memory_order_relaxed));
			Next->State->Id = Id + 1;
			Next->State->Current = std::move(Initial);
			Out = std::move(Next);
			return true;
		});
		if (!Ok)
		{
			Out.reset();
		}
		return Ok;
	}
	FI18nConfiguration FI18nContext::Configuration() const
	{
		std::shared_ptr<const FContextConfiguration> Old;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			Old = State->Current;
		}
		return Old->Config;
	}
	bool FI18nContext::Configure(const FI18nConfiguration& C, FI18nError& E)
	{
		return Update(C, {}, -1, false, E);
	}
	bool FI18nContext::SetLanguage(std::string L, FI18nError& E)
	{
		FI18nConfiguration C;
		C.Language = std::move(L);
		return Update(C, {}, 0, false, E);
	}
	bool FI18nContext::SetFallbackLanguages(std::vector<std::string> L, FI18nError& E)
	{
		FI18nConfiguration C;
		C.FallbackLanguages = std::move(L);
		return Update(C, {}, 1, false, E);
	}
	bool FI18nContext::SetFormatLocale(std::string L, FI18nError& E)
	{
		FI18nConfiguration C;
		C.FormatLocale = std::move(L);
		return Update(C, {}, 2, false, E);
	}
	bool FI18nContext::SetFormatOptions(FI18nArguments O, FI18nError& E)
	{
		FI18nConfiguration C;
		C.FormatOptions = std::move(O);
		return Update(C, {}, 3, false, E);
	}
	bool FI18nContext::SetServices(std::shared_ptr<FI18nServices> S, FI18nError& E)
	{
		return Update({}, std::move(S), 4, true, E);
	}
	bool FI18nContext::ConfigureAndSetServices(
	    const FI18nConfiguration& C, std::shared_ptr<FI18nServices> S, FI18nError& E)
	{
		return Update(C, std::move(S), -1, true, E);
	}
	bool FI18nContext::Clone(std::shared_ptr<FI18nContext>& Out, FI18nError& E) const
	{
		std::shared_ptr<const FContextConfiguration> Old;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			Old = State->Current;
		}
		return CreateContext(Old->Config, Old->Services, Out, E);
	}
	FI18nContext::FI18nContext() : State(std::make_unique<FState>())
	{
	}

	bool FI18nContext::Update(const FI18nConfiguration& Config, std::shared_ptr<FI18nServices> Services,
	    int Field, bool ChangeServices, FI18nError& E)
	{
		return Boundary(E,
		    [&]()
		{
			for (;;)
			{
				std::shared_ptr<const FContextConfiguration> Old;
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					Old = State->Current;
				}
				auto Next = std::make_shared<FContextConfiguration>(*Old);
				FI18nConfiguration Candidate = Field < 0 ? Config : Old->Config;
				if (Field == 0)
				{
					Candidate.Language = Config.Language;
				}

				if (Field == 1)
				{
					Candidate.FallbackLanguages = Config.FallbackLanguages;
				}

				if (Field == 2)
				{
					Candidate.FormatLocale = Config.FormatLocale;
				}

				if (Field == 3)
				{
					Candidate.FormatOptions = Config.FormatOptions;
				}

				if (!NormalizeConfiguration(Candidate, Next->Config, E)) return false;
				if (ChangeServices)
				{
					Next->Services = Services;
				}

				const bool Changed = !(Next->Config == Old->Config) || Next->Services != Old->Services;
				if (Changed && Old->Revision == std::numeric_limits<std::uint64_t>::max())
				{
					return Fail(E, EI18nStatus::ResourceLimit, "configuration revision exhausted");
				}
				if (Changed)
				{
					++Next->Revision;
				}

				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					if (State->Current != Old)
					{
						continue;
					}

					if (Changed)
					{
						State->Current = std::move(Next);
					}

					return true;
				}
			}
		});
	}

	FI18nConfiguration FI18nSnapshot::Configuration() const
	{
		return Data ? Data->Context->Config : FI18nConfiguration{};
	}
	FI18nVersion FI18nSnapshot::Version() const noexcept
	{
		return Data ? Data->Stamp : FI18nVersion{};
	}
	namespace
	{
		bool HasError(const FI18nError& E)
		{
			return E.Status != EI18nStatus::Ok || !E.Diagnostic.Message.empty() ||
			    !E.Diagnostic.MessageId.empty() || !E.Diagnostic.MessageArgs.empty();
		}
		bool ValidFailure(const FI18nError& E)
		{
			return E.Status > EI18nStatus::Ok && E.Status <= EI18nStatus::ReentrantOperation &&
			    (!E.Diagnostic.Message.empty() || !E.Diagnostic.MessageId.empty());
		}
	}
	bool FI18nSnapshot::TryLookup(std::string_view Domain, std::string_view Key,
	    FI18nMessageTemplate& Out, FI18nError& E, FI18nWork Work) const
	{
		// Native callbacks may replace the caller's snapshot variable. Pin this
		// operation's state and provider owners until all callbacks have returned.
		const FI18nSnapshot Stable = *this;
		const bool Ok = Boundary(E,
		    [&]()
		{
			FOperationScope Scope(Stable.Version().ContextId, E);
			if (!Scope) return false;
			FI18nMessageTemplate Next;
			if (!Stable.LookupImpl(Domain, Key, Next, E, Work) ||
			    !CheckSize(Next.Pattern.size(), Work, E))
			{
				return false;
			}
			Out = std::move(Next);
			return true;
		});
		if (!Ok)
		{
			// The input views may refer to strings in Out; consume them first.
			if (E.Language.empty())
			{
				Identify(E, Domain, Key, Stable.Data ? Stable.Data->Context->Config.Language : "");
			}

			Out = {};
		}
		return Ok;
	}
	bool FI18nSnapshot::TryResolve(const FI18nText& Text, FI18nResolvedText& Out,
	    FI18nError& E, FI18nWork Work) const
	{
		const FI18nSnapshot Stable = *this;
		const FI18nText StableText = Text;
		const bool Ok = Boundary(E,
		    [&]()
		{
			FOperationScope Scope(Stable.Version().ContextId, E);
			if (!Scope || !CheckWork(Work, E)) return false;
			FI18nResolvedText Next;
			Next.Version = Stable.Data->Stamp;
			if (StableText.IsLiteral())
			{
				if (!CheckSize(StableText.LiteralValue().size(), Work, E)) return false;
				Next.Value = StableText.LiteralValue();
			}
			else
			{
				FI18nMessageTemplate Message;
				if (!Stable.LookupImpl(StableText.Domain(), StableText.Key(), Message, E, Work))
				{
					return false;
				}
				Next.MatchedLanguage = Message.MatchedLanguage;
				if (!Stable.FormatImpl(Message.Pattern, StableText.Params(), Message.FormatId,
				        Message.MatchedLanguage, Next.Value, E, Work))
				{
					Identify(E, StableText.Domain(), StableText.Key(), Message.MatchedLanguage);
					return false;
				}
			}
			if (!CheckWork(Work, E)) return false;
			Next.Valid = true;
			Out = std::move(Next);
			return true;
		});
		if (!Ok)
		{
			Out = {};
			if (!StableText.IsLiteral() && E.Language.empty())
			{
				Identify(E, StableText.Domain(), StableText.Key(),
				    Stable.Data ? Stable.Data->Context->Config.Language : "");
			}
		}
		return Ok;
	}
	bool FI18nSnapshot::TryFormat(std::string_view Pattern, const FI18nArguments& Args,
	    std::string& Out, FI18nError& E, FI18nWork Work, std::string_view FormatId) const
	{
		const FI18nSnapshot Stable = *this;
		const bool Ok = Boundary(E,
		    [&]()
		{
			FOperationScope Scope(Stable.Version().ContextId, E);
			if (!Scope) return false;
			const FI18nArguments StableArgs = Args;
			return Stable.FormatImpl(
			    Pattern, StableArgs, FormatId, Stable.Data->Context->Config.Language, Out, E, Work);
		});
		if (!Ok)
		{
			if (E.Language.empty())
			{
				Identify(E, {}, {}, Stable.Data ? Stable.Data->Context->Config.Language : "");
			}

			Out.clear();
		}
		return Ok;
	}
	bool FI18nSnapshot::LookupImpl(std::string_view Domain, std::string_view Key,
	    FI18nMessageTemplate& Out, FI18nError& E, FI18nWork Work) const
	{
		if (!CheckWork(Work, E)) return false;
		if (!ValidName(Domain) || !ValidName(Key)) return Fail(E, EI18nStatus::InvalidArgument, "invalid resource domain or key");
		if (!Data->Services->Resources) return Fail(E, EI18nStatus::MissingResolver, "no resource resolver is configured");
		const auto& C = Data->Context->Config;
		for (std::size_t I = 0; I <= C.FallbackLanguages.size(); ++I)
		{
			const auto& Language = I ? C.FallbackLanguages[I - 1] : C.Language;
			if (I && Language == C.Language)
			{
				continue;
			}

			if (!CheckWork(Work, E, 1)) return false;
			FI18nMessage M;
			FI18nError ProviderError;
			const auto Status = Data->Services->Resources->Lookup({Domain, Key, Language}, Work, M, ProviderError);
			if (!CheckWork(Work, E)) return false;
			if (Status == EI18nLookupResult::NotFound && !HasError(ProviderError))
			{
				continue;
			}

			if (Status == EI18nLookupResult::Found && !HasError(ProviderError) && ValidFormatId(M.FormatId))
			{
				Out = {true, std::move(M.Pattern), std::move(M.FormatId), Language, Data->Stamp};
				return true;
			}
			if (Status == EI18nLookupResult::Failed && ValidFailure(ProviderError))
			{
				E = std::move(ProviderError);
			}

			else
			{
				Fail(E, EI18nStatus::ProviderFailure, "resource resolver returned an invalid result");
			}

			Identify(E, Domain, Key, Language);
			return false;
		}
		return Fail(E, EI18nStatus::MissingMessage, "message not found: " + std::string(Domain) + "/" + std::string(Key));
	}
	bool FI18nSnapshot::FormatImpl(std::string_view Pattern, const FI18nArguments& Args,
	    std::string_view FormatId, std::string_view MatchedLanguage, std::string& Out,
	    FI18nError& E, FI18nWork Work) const
	{
		if (!CheckWork(Work, E)) return false;
		if (!ValidFormatId(FormatId)) return Fail(E, EI18nStatus::UnsupportedFormat, "invalid format identifier");
		const auto& C = Data->Context->Config;
		std::string Result;
		if (FormatId == "literal")
		{
			if (!CheckSize(Pattern.size(), Work, E)) return false;
			Result.assign(Pattern);
		}
		else if (FormatId == "vero.named")
		{
			if (C.FormatOptions.Size()) return Fail(E, EI18nStatus::UnsupportedOption, "vero.named does not support format options");
			if (!TryFormatNamed(Pattern, Args, Result, E, Work)) return false;
		}
		else
		{
			const auto& F = Data->Services->Formatter;
			const bool Supported = F && F->SupportsFormat(FormatId);
			if (!CheckWork(Work, E)) return false;
			if (!Supported) return Fail(E, EI18nStatus::UnsupportedFormat, "unsupported format: " + std::string(FormatId));
			FI18nError ProviderError;
			const FI18nFormatRequest Request{Pattern, FormatId, Args, C.Language, MatchedLanguage,
			    C.FormatLocale.empty() ? std::string_view(C.Language) : std::string_view(C.FormatLocale), C.FormatOptions};
			const bool Success = F->Format(Request, Work, Result, ProviderError);
			if (!CheckWork(Work, E)) return false;
			if (!Success || HasError(ProviderError))
			{
				if (!Success && ValidFailure(ProviderError))
				{
					E = std::move(ProviderError);
				}

				else
				{
					Fail(E, EI18nStatus::ProviderFailure, "formatter returned an invalid result");
				}

				return false;
			}
		}
		if (!CheckSize(Result.size(), Work, E) || !CheckWork(Work, E)) return false;
		Out = std::move(Result);
		return true;
	}
}
