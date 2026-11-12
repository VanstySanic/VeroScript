// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"

namespace VeroScript::Json::Binding
{
	enum class EStreamAction
	{
		ArrayBuilder,
		ObjectBuilder,
		BuilderPush,
		BuilderSet,
		BuilderReserve,
		BuilderSize,
		BuilderCapacity,
		BuilderFinish,
		BuilderResetArray,
		BuilderResetObject,
		Reader,
		ReaderFeed,
		ReaderVisit,
		ReaderEvent,
		ReaderState,
		ReaderError,
		ReaderReset,
		Writer,
		WriterWrite,
		WriterResume,
		WriterFinish,
		WriterState,
		WriterError,
		WriterTake,
		MakeEvent,
		EventKind,
		EventText,
		EventBoolean
	};

	struct FScriptReader
	{
		explicit FScriptReader(FParseOptions Options = {}) : Reader(std::move(Options))
		{
		}
		FReader Reader{};
		FEvent Event;
		FError Error;
		EReadState State = EReadState::NeedInput;
		bool bVisiting = false;
	};

	struct FReaderActivity
	{
		explicit FReaderActivity(FScriptReader& R) : Reader(R)
		{
			Reader.bVisiting = true;
		}
		~FReaderActivity()
		{
			Reader.bVisiting = false;
		}
		FScriptReader& Reader;
	};

	struct FScriptVisitorFailure
	{
	};

	struct FScriptWriter
	{
		struct FBuffer
		{
			std::string Text;
		};
		explicit FScriptWriter(FWriteOptions Options = {})
			: Buffer(std::make_shared<FBuffer>()),
			  Writer(
				  [B = Buffer](std::string_view Text)
				  {
					  const auto Count = std::min<std::size_t>(Text.size(), 65536 - B->Text.size());
					  B->Text.append(Text.data(), Count);
					  return FSinkResult{Count, Count < Text.size() ? ESinkState::NeedOutput : ESinkState::Progress};
				  },
				  std::move(Options))
		{
		}
		std::shared_ptr<FBuffer> Buffer;
		FWriter Writer;
		FError Error;
		EWriteState State = EWriteState::Active;
	};

	template <>
	const char* TypeName<FScriptReader>()
	{
		return "json::reader";
	}
	template <>
	const char* TypeName<FScriptWriter>()
	{
		return "json::writer";
	}

	template <EStreamAction Action>
	bool StreamCall(const FNativeCallContext& Input, VeroScript::FValue& Out, std::string& Error)
	{
		FNativeCallContext C = Input;
		if (C.HostThis)
		{
			++C.Args;
			if (C.MutableArgs)
			{
				++C.MutableArgs;
			}
			--C.ArgCount;
		}
		auto& S = *static_cast<FState*>(C.UserData);
		FError E;
		FJsonValue V;
		auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
		FWork W{Work.Check, Work.UserData};
		if constexpr (Action == EStreamAction::ArrayBuilder || Action == EStreamAction::ObjectBuilder)
		{
			return Wrap(S, FBuilder(Action == EStreamAction::ArrayBuilder ? EKind::Array : EKind::Object), Out, Error);
		}
		else if constexpr (Action >= EStreamAction::BuilderPush && Action <= EStreamAction::BuilderResetObject)
		{
			auto& B = *static_cast<FBuilder*>(C.HostThis);
			if constexpr (Action == EStreamAction::BuilderPush)
			{
				B.TryPush(Object<FJsonValue>(C, 0), E);
			}
			else if constexpr (Action == EStreamAction::BuilderSet)
			{
				B.TrySet(std::get<std::string>(C.Args[0]), Object<FJsonValue>(C, 1), E);
			}
			else if constexpr (Action == EStreamAction::BuilderReserve)
			{
				B.TryReserve(std::get<std::int64_t>(C.Args[0]), E);
			}
			else if constexpr (Action == EStreamAction::BuilderFinish)
			{
				B.TryFinish(V, E);
			}
			else if constexpr (Action == EStreamAction::BuilderResetArray ||
							   Action == EStreamAction::BuilderResetObject)
			{
				B.TryReset(Action == EStreamAction::BuilderResetArray ? EKind::Array : EKind::Object, E);
			}
			else
			{
				std::int64_t Count = 0;
				if constexpr (Action == EStreamAction::BuilderSize)
				{
					B.TrySize(Count, E);
				}
				else
				{
					B.TryCapacity(Count, E);
				}
				Out = Count;
			}
		}
		else if constexpr (Action == EStreamAction::Reader)
		{
			return Wrap(S, FScriptReader{C.ArgCount ? Object<FParseOptions>(C, 0) : FParseOptions{}}, Out, Error);
		}
		else if constexpr (Action >= EStreamAction::ReaderFeed && Action <= EStreamAction::ReaderReset)
		{
			auto& R = *static_cast<FScriptReader*>(C.HostThis);
			if constexpr (Action == EStreamAction::ReaderFeed || Action == EStreamAction::ReaderVisit ||
						  Action == EStreamAction::ReaderReset)
			{
				if (R.bVisiting)
				{
					Error = "JSON reader cannot be changed by its active visitor";
					return false;
				}
			}
			if constexpr (Action == EStreamAction::ReaderFeed)
			{
				const auto Feed =
					R.Reader.Feed(std::get<std::string>(C.Args[0]), std::get<bool>(C.Args[1]), R.Event, R.Error, W);
				R.State = Feed.State;
				Out = static_cast<std::int64_t>(Feed.Consumed);
				if (R.Error.Code == EError::Cancelled)
				{
					E = R.Error;
				}
			}
			else if constexpr (Action == EStreamAction::ReaderVisit)
			{
				if (!C.ScriptInvoker)
				{
					Error = "JSON SAX requires an active script invoker";
					return false;
				}
				FReaderActivity Active(R);
				bool CallFailed = false;
				auto Feed = R.Reader.Visit(
					std::get<std::string>(C.Args[0]),
					std::get<bool>(C.Args[1]),
					[&](const FEvent& Event)
					{
						R.Event = Event;
						R.State = EReadState::EventReady;
						VeroScript::FValue Argument, Returned;
						if (!Wrap(S, Event, Argument, Error))
						{
							CallFailed = true;
							throw FScriptVisitorFailure{};
						}
						std::vector<VeroScript::FValue> Arguments{std::move(Argument)};
						if (!C.ScriptInvoker->InvokeCallable(
								std::get<FFunctionHandle>(C.Args[2]), Arguments, Returned, Error))
						{
							CallFailed = true;
							throw FScriptVisitorFailure{};
						}
						const auto* Continue = std::get_if<bool>(&Returned);
						if (!Continue)
						{
							Error = "JSON visitor must return bool";
							CallFailed = true;
							throw FScriptVisitorFailure{};
						}
						return *Continue;
					},
					R.Error,
					W);
				R.State = Feed.State;
				Out = static_cast<std::int64_t>(Feed.Consumed);
				if (CallFailed)
				{
					return false;
				}
				if (R.Error.Code == EError::Cancelled)
				{
					E = R.Error;
				}
			}
			else if constexpr (Action == EStreamAction::ReaderEvent)
			{
				if (R.State != EReadState::EventReady)
				{
					Error = "JSON reader has no current event";
					return false;
				}
				return Wrap(S, R.Event, Out, Error);
			}
			else if constexpr (Action == EStreamAction::ReaderState)
			{
				Out = static_cast<std::int64_t>(R.State);
			}
			else if constexpr (Action == EStreamAction::ReaderError)
			{
				return Wrap(S, R.Error, Out, Error);
			}
			else
			{
				R.Reader.Reset();
				R.Event = {};
				R.Error = {};
				R.State = EReadState::NeedInput;
			}
		}
		else if constexpr (Action == EStreamAction::Writer)
		{
			return Wrap(S, FScriptWriter{C.ArgCount ? Object<FWriteOptions>(C, 0) : FWriteOptions{}}, Out, Error);
		}
		else if constexpr (Action >= EStreamAction::WriterWrite && Action <= EStreamAction::WriterTake)
		{
			auto& R = *static_cast<FScriptWriter*>(C.HostThis);
			if constexpr (Action == EStreamAction::WriterWrite)
			{
				bool Accepted = false;
				R.State = R.Writer.Write(Object<FEvent>(C, 0), Accepted, R.Error, W);
				Out = Accepted;
			}
			else if constexpr (Action == EStreamAction::WriterResume)
			{
				R.State = R.Writer.Resume(R.Error, W);
			}
			else if constexpr (Action == EStreamAction::WriterFinish)
			{
				R.State = R.Writer.Finish(R.Error, W);
			}
			else if constexpr (Action == EStreamAction::WriterState)
			{
				Out = static_cast<std::int64_t>(R.State);
			}
			else if constexpr (Action == EStreamAction::WriterError)
			{
				return Wrap(S, R.Error, Out, Error);
			}
			else
			{
				Out = std::move(R.Buffer->Text);
				R.Buffer->Text.clear();
			}
			if (R.Error.Code == EError::Cancelled)
			{
				E = R.Error;
			}
		}
		else if constexpr (Action == EStreamAction::MakeEvent)
		{
			const auto Kind = std::get<std::int64_t>(C.Args[0]);
			if (Kind < 0 || Kind > static_cast<std::int64_t>(EEvent::String))
			{
				Error = "JSON event kind is out of range";
				return false;
			}
			return Wrap(S,
				FEvent{static_cast<EEvent>(Kind), std::get<std::string>(C.Args[1]), std::get<bool>(C.Args[2])},
				Out,
				Error);
		}
		else if constexpr (Action == EStreamAction::EventKind || Action == EStreamAction::EventText ||
						   Action == EStreamAction::EventBoolean)
		{
			const auto& Event = *static_cast<const FEvent*>(C.HostThis);
			if constexpr (Action == EStreamAction::EventKind)
			{
				Out = static_cast<std::int64_t>(Event.Kind);
			}
			else if constexpr (Action == EStreamAction::EventText)
			{
				Out = Event.Text;
			}
			else
			{
				Out = Event.Boolean;
			}
		}

		if (E)
		{
			Error = std::string(E.Id()) + ": " + E.Message;
			Diagnostic(C, E.Id(), E.Message, E.InstancePointer);
			return false;
		}
		if constexpr (Action == EStreamAction::BuilderFinish)
		{
			return Wrap(S, std::move(V), Out, Error);
		}
		return true;
	}

	bool RegisterStreams(FState& S, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId R, W, B, Event;
		return HandleType<FScriptReader>(S, R, Error) && HandleType<FScriptWriter>(S, W, Error) &&
			   HandleType<FBuilder>(S, B, Error) && Type<FEvent>(S, Event, Error) && RegisterBuilderTry(S, B, Error) &&
			   Function<StreamCall<EStreamAction::Reader>>(
				   S, "json::make_reader", T::HostObject, {}, Error, {}, {}, "json::reader") &&
			   Function<StreamCall<EStreamAction::Reader>>(S,
				   "json::make_reader_with_options",
				   T::HostObject,
				   {T::HostObject},
				   Error,
				   {"json::parse_options"},
				   {},
				   "json::reader") &&
			   Function<StreamCall<EStreamAction::Writer>>(
				   S, "json::make_writer", T::HostObject, {}, Error, {}, {}, "json::writer") &&
			   Function<StreamCall<EStreamAction::Writer>>(S,
				   "json::make_writer_with_options",
				   T::HostObject,
				   {T::HostObject},
				   Error,
				   {"json::write_options"},
				   {},
				   "json::writer") &&
			   Function<StreamCall<EStreamAction::ArrayBuilder>>(
				   S, "json::array_builder", T::HostObject, {}, Error, {}, {}, "json::builder") &&
			   Function<StreamCall<EStreamAction::ObjectBuilder>>(
				   S, "json::object_builder", T::HostObject, {}, Error, {}, {}, "json::builder") &&
			   Function<StreamCall<EStreamAction::MakeEvent>>(S,
				   "json::make_event",
				   T::HostObject,
				   {T::Int64, T::String, T::Bool},
				   Error,
				   {},
				   {},
				   "json::event") &&
			   Method<StreamCall<EStreamAction::ReaderFeed>>(
				   S, R, "feed", T::Int64, {T::String, T::Bool}, Error, true) &&
			   Method<StreamCall<EStreamAction::ReaderVisit>>(S,
				   R,
				   "visit",
				   T::Int64,
				   {T::String, T::Bool, T::Function},
				   Error,
				   true,
				   "",
				   {"", "", "fn(json::event)->bool"}) &&
			   Method<StreamCall<EStreamAction::ReaderEvent>>(
				   S, R, "event", T::HostObject, {}, Error, false, "json::event") &&
			   Method<StreamCall<EStreamAction::ReaderError>>(
				   S, R, "error", T::HostObject, {}, Error, false, "json::error") &&
			   Method<StreamCall<EStreamAction::ReaderState>>(S, R, "state", T::Int64, {}, Error) &&
			   Method<StreamCall<EStreamAction::ReaderReset>>(S, R, "reset", T::Void, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::WriterWrite>>(
				   S, W, "write", T::Bool, {T::HostObject}, Error, true, "", {"json::event"}) &&
			   Method<StreamCall<EStreamAction::WriterResume>>(S, W, "resume", T::Void, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::WriterFinish>>(S, W, "finish", T::Void, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::WriterState>>(S, W, "state", T::Int64, {}, Error) &&
			   Method<StreamCall<EStreamAction::WriterError>>(
				   S, W, "error", T::HostObject, {}, Error, false, "json::error") &&
			   Method<StreamCall<EStreamAction::WriterTake>>(S, W, "take", T::String, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::EventKind>>(S, Event, "kind", T::Int64, {}, Error) &&
			   Method<StreamCall<EStreamAction::EventText>>(S, Event, "text", T::String, {}, Error) &&
			   Method<StreamCall<EStreamAction::EventBoolean>>(S, Event, "boolean", T::Bool, {}, Error) &&
			   Method<StreamCall<EStreamAction::BuilderPush>>(S, B, "push", T::Void, {T::HostObject}, Error, true) &&
			   Method<StreamCall<EStreamAction::BuilderSet>>(
				   S, B, "set", T::Void, {T::String, T::HostObject}, Error, true) &&
			   Method<StreamCall<EStreamAction::BuilderReserve>>(S, B, "reserve", T::Void, {T::Int64}, Error, true) &&
			   Method<StreamCall<EStreamAction::BuilderSize>>(S, B, "size", T::Int64, {}, Error) &&
			   Method<StreamCall<EStreamAction::BuilderCapacity>>(S, B, "capacity", T::Int64, {}, Error) &&
			   Method<StreamCall<EStreamAction::BuilderFinish>>(S, B, "finish", T::HostObject, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::BuilderResetArray>>(S, B, "reset_array", T::Void, {}, Error, true) &&
			   Method<StreamCall<EStreamAction::BuilderResetObject>>(S, B, "reset_object", T::Void, {}, Error, true);
	}

} // namespace VeroScript::Json::Binding
