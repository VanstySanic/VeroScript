// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

#include "VeroScriptHostBindingCatalog.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace VeroScript::Interop
{
	namespace
	{
		constexpr std::size_t HostTypeDescV1Size =
			offsetof(vs_interop_host_type_desc, enum_underlying_type_name);

		bool HasEnumUnderlyingTypeField(const vs_interop_host_type_desc& Type)
		{
			return Type.struct_size >= sizeof(vs_interop_host_type_desc);
		}

		bool CopyStringList(
			const vs_interop_string_list& Source,
			std::vector<std::string>& Out)
		{
			if ((Source.count != 0 && !Source.values)
				|| Source.count > static_cast<std::uint64_t>(
					std::numeric_limits<std::size_t>::max()))
			{
				return false;
			}
			Out.reserve(static_cast<std::size_t>(Source.count));
			for (std::uint64_t Index = 0; Index < Source.count; ++Index)
			{
				if (!IsValidText(Source.values[Index]))
				{
					return false;
				}
				Out.push_back(CopyString(Source.values[Index]));
			}
			return true;
		}

		bool ConvertAnnotations(
			const vs_interop_annotation_list& Source,
			std::vector<FHostAnnotationInstance>& Out,
			std::string& OutError)
		{
			if ((Source.count != 0 && !Source.values) || Source.count > 4096)
			{
				OutError = "invalid host annotation list";
				return false;
			}
			Out.reserve(static_cast<std::size_t>(Source.count));
			for (std::uint64_t Index = 0; Index < Source.count; ++Index)
			{
				const auto& Item = Source.values[Index];
				if (Item.struct_size < sizeof(vs_interop_annotation)
					|| !IsValidText(Item.name) || Item.name.size == 0
					|| (Item.argument_count != 0 && !Item.arguments)
					|| Item.argument_count > 4096)
				{
					OutError = "invalid host annotation";
					return false;
				}
				FHostAnnotationInstance Annotation;
				Annotation.Name = CopyString(Item.name);
				Annotation.Arguments.reserve(static_cast<std::size_t>(Item.argument_count));
				for (std::uint64_t ArgIndex = 0; ArgIndex < Item.argument_count; ++ArgIndex)
				{
					const auto& SourceArgument = Item.arguments[ArgIndex];
					if (!IsValidText(SourceArgument.name) || SourceArgument.name.size == 0
						|| !IsValidText(SourceArgument.value))
					{
						OutError = "invalid host annotation argument";
						return false;
					}
					FHostAnnotationArgument Argument;
					Argument.Name = CopyString(SourceArgument.name);
					if (SourceArgument.has_value != 0)
					{
						Argument.Value = CopyString(SourceArgument.value);
					}
					Annotation.Arguments.push_back(std::move(Argument));
				}
				Out.push_back(std::move(Annotation));
			}
			return true;
		}

		bool ConvertHostTypeKind(
			const vs_interop_host_type_kind Source,
			EHostTypeKind& Out)
		{
			switch (Source)
			{
			case VS_INTEROP_HOST_ENUM:
				{
					Out = EHostTypeKind::Enum;
					return true;
				}
			case VS_INTEROP_HOST_STRUCT:
				{
					Out = EHostTypeKind::Struct;
					return true;
				}
			case VS_INTEROP_HOST_CLASS:
				{
					Out = EHostTypeKind::Class;
					return true;
				}
			default:
				return false;
			}
		}

		bool ConvertHostMemberKind(
			const vs_interop_host_member_kind Source,
			EHostMemberKind& Out)
		{
			switch (Source)
			{
			case VS_INTEROP_HOST_CONSTRUCTOR:
				{
					Out = EHostMemberKind::Constructor;
					return true;
				}
			case VS_INTEROP_HOST_METHOD:
				{
					Out = EHostMemberKind::Method;
					return true;
				}
			case VS_INTEROP_HOST_STATIC_METHOD:
				{
					Out = EHostMemberKind::StaticMethod;
					return true;
				}
			case VS_INTEROP_HOST_PROPERTY_GETTER:
				{
					Out = EHostMemberKind::PropertyGetter;
					return true;
				}
			case VS_INTEROP_HOST_PROPERTY_SETTER:
				{
					Out = EHostMemberKind::PropertySetter;
					return true;
				}
			case VS_INTEROP_HOST_ENUM_MEMBER:
				{
					Out = EHostMemberKind::EnumMember;
					return true;
				}
			case VS_INTEROP_HOST_FIELD:
				{
					Out = EHostMemberKind::Field;
					return true;
				}
			case VS_INTEROP_HOST_STATIC_CONSTANT:
				{
					Out = EHostMemberKind::StaticConstant;
					return true;
				}
			default:
				return false;
			}
		}

		void AppendTypeMetadata(
			const FNativeTypeDesc& Type,
			std::string& HostName,
			std::string& ElementName,
			std::string& KeyName,
			std::string& ValueName)
		{
			HostName = Type.HostTypeName;
			ElementName = Type.TypeArguments.size() == 1
				? ResolveNativeTypeDescName(Type.TypeArguments[0])
				: Type.ElementTypeName;
			if (Type.TypeArguments.size() == 2)
			{
				KeyName = ResolveNativeTypeDescName(Type.TypeArguments[0]);
				ValueName = ResolveNativeTypeDescName(Type.TypeArguments[1]);
			}
			else
			{
				KeyName = Type.KeyTypeName;
				ValueName = Type.ValueTypeName;
			}
		}

		bool ConvertHostMember(
			const vs_interop_host_member_desc& Source,
			FHostMemberDesc& Out,
			std::shared_ptr<FNativeThunk>& OutThunk,
			std::string& OutError)
		{
			if (Source.struct_size < sizeof(vs_interop_host_member_desc)
				|| !IsValidText(Source.name)
				|| !IsValidText(Source.documentation)
				|| !IsValidText(Source.return_documentation)
				|| (Source.parameter_count != 0 && !Source.parameters)
				|| Source.parameter_count > UINT8_MAX
				|| (Source.parameter_annotation_count != 0 && !Source.parameter_annotations)
				|| (Source.parameter_annotation_count != 0
					&& Source.parameter_annotation_count != Source.parameter_count))
			{
				OutError = "invalid host member descriptor";
				return false;
			}
			if (!ConvertHostMemberKind(Source.kind, Out.Kind))
			{
				OutError = "invalid host member kind";
				return false;
			}
			if (Out.Kind != EHostMemberKind::Constructor
				&& Source.name.size == 0)
			{
				OutError = "host member name is empty";
				return false;
			}
			FNativeTypeDesc ReturnType;
			std::size_t NodeCount = 0;
			if (!ConvertNativeType(Source.return_type, ReturnType, OutError, 0, &NodeCount))
			{
				return false;
			}
			std::vector<FNativeParamDesc> Params;
			Params.reserve(static_cast<std::size_t>(Source.parameter_count));
			for (std::uint64_t Index = 0; Index < Source.parameter_count; ++Index)
			{
				FNativeParamDesc Param;
				if (!ConvertNativeParam(Source.parameters[Index], Param, OutError, &NodeCount))
				{
					return false;
				}
				Params.push_back(std::move(Param));
			}

			Out.Name = CopyString(Source.name);
			Out.Documentation = CopyString(Source.documentation);
			Out.ReturnDocumentation = CopyString(Source.return_documentation);
			Out.EnumValue = Source.enum_value;
			Out.bOperator = Source.is_operator != 0;
			Out.bConstMethod = Source.is_const_method != 0;
			Out.bMutableThis = Source.mutable_this != 0;
			Out.ReturnType = ReturnType.Type;
			AppendTypeMetadata(ReturnType, Out.ReturnHostTypeName,
				Out.ReturnElementTypeName, Out.ReturnKeyTypeName, Out.ReturnValueTypeName);
			for (const FNativeParamDesc& Param : Params)
			{
				Out.ParamTypes.push_back(Param.Type.Type);
				Out.ParamFlags.push_back(Param.Flags);
				std::string Host, Element, Key, Value;
				AppendTypeMetadata(Param.Type, Host, Element, Key, Value);
				Out.ParamHostTypeNames.push_back(std::move(Host));
				Out.ParamElementTypeNames.push_back(std::move(Element));
				Out.ParamKeyTypeNames.push_back(std::move(Key));
				Out.ParamValueTypeNames.push_back(std::move(Value));
			}
			if (!CopyStringList(Source.parameter_names, Out.ParamNames)
				|| !CopyStringList(Source.parameter_documentation, Out.ParamDocumentation)
				|| (!Out.ParamNames.empty() && Out.ParamNames.size() != Params.size())
				|| (!Out.ParamDocumentation.empty()
					&& Out.ParamDocumentation.size() != Params.size())
				|| !ConvertAnnotations(Source.annotations, Out.Annotations, OutError))
			{
				if (OutError.empty())
				{
					OutError = "invalid host member parameter metadata";
				}
				return false;
			}
			if (Source.parameter_annotation_count != 0)
			{
				Out.ParamAnnotations.resize(Params.size());
				for (std::size_t Index = 0; Index < Params.size(); ++Index)
				{
					if (!ConvertAnnotations(Source.parameter_annotations[Index],
						Out.ParamAnnotations[Index], OutError))
					{
						return false;
					}
				}
			}
			if (Source.callback)
			{
				OutThunk = std::make_shared<FNativeThunk>();
				OutThunk->Callback = Source.callback;
				OutThunk->UserData = Source.user_data;
				OutThunk->ReturnType = ReturnType.Type;
				Out.Callback = &NativeThunkCallback;
				Out.UserData = OutThunk.get();
			}
			return true;
		}

		bool ConvertHostFunction(
			const vs_interop_host_function_desc& Source,
			FHostFunctionDesc& Out,
			std::shared_ptr<FNativeThunk>& OutThunk,
			std::string& OutError)
		{
			if (Source.struct_size < sizeof(vs_interop_host_function_desc)
				|| !IsValidText(Source.name) || Source.name.size == 0
				|| !IsValidText(Source.documentation)
				|| !IsValidText(Source.return_documentation)
				|| !IsValidText(Source.script_return_type_name)
				|| !Source.callback
				|| (Source.parameter_count != 0 && !Source.parameters)
				|| Source.parameter_count > UINT8_MAX
				|| (Source.parameter_annotation_count != 0
					&& !Source.parameter_annotations)
				|| (Source.parameter_annotation_count != 0
					&& Source.parameter_annotation_count != Source.parameter_count))
			{
				OutError = "invalid host function descriptor";
				return false;
			}

			FNativeTypeDesc ReturnType;
			std::size_t NodeCount = 0;
			if (!ConvertNativeType(Source.return_type, ReturnType, OutError, 0, &NodeCount))
			{
				return false;
			}
			std::vector<FNativeParamDesc> Params;
			Params.reserve(static_cast<std::size_t>(Source.parameter_count));
			for (std::uint64_t Index = 0; Index < Source.parameter_count; ++Index)
			{
				FNativeParamDesc Param;
				if (!ConvertNativeParam(Source.parameters[Index], Param, OutError, &NodeCount))
				{
					return false;
				}
				Params.push_back(std::move(Param));
			}

			Out.Name = CopyString(Source.name);
			Out.Documentation = CopyString(Source.documentation);
			Out.ReturnDocumentation = CopyString(Source.return_documentation);
			Out.ReturnType = ReturnType.Type;
			Out.ScriptReturnTypeName = CopyString(Source.script_return_type_name);
			Out.bGenericHostObjectCast = Source.is_generic_host_object_cast != 0;
			AppendTypeMetadata(ReturnType, Out.ReturnHostTypeName,
				Out.ReturnElementTypeName, Out.ReturnKeyTypeName, Out.ReturnValueTypeName);
			for (const FNativeParamDesc& Param : Params)
			{
				Out.ParamTypes.push_back(Param.Type.Type);
				Out.ParamFlags.push_back(Param.Flags);
				std::string Host, Element, Key, Value;
				AppendTypeMetadata(Param.Type, Host, Element, Key, Value);
				Out.ParamHostTypeNames.push_back(std::move(Host));
				Out.ParamElementTypeNames.push_back(std::move(Element));
				Out.ParamKeyTypeNames.push_back(std::move(Key));
				Out.ParamValueTypeNames.push_back(std::move(Value));
			}
			if (!CopyStringList(Source.parameter_names, Out.ParamNames)
				|| !CopyStringList(Source.parameter_documentation, Out.ParamDocumentation)
				|| !CopyStringList(Source.type_parameters, Out.TypeParams)
				|| (!Out.ParamNames.empty() && Out.ParamNames.size() != Params.size())
				|| (!Out.ParamDocumentation.empty()
					&& Out.ParamDocumentation.size() != Params.size())
				|| !ConvertAnnotations(Source.annotations, Out.Annotations, OutError))
			{
				if (OutError.empty())
				{
					OutError = "invalid host function metadata";
				}
				return false;
			}
			if (Source.parameter_annotation_count != 0)
			{
				Out.ParamAnnotations.resize(Params.size());
				for (std::size_t Index = 0; Index < Params.size(); ++Index)
				{
					if (!ConvertAnnotations(Source.parameter_annotations[Index],
						Out.ParamAnnotations[Index], OutError))
					{
						return false;
					}
				}
			}

			OutThunk = std::make_shared<FNativeThunk>();
			OutThunk->Callback = Source.callback;
			OutThunk->UserData = Source.user_data;
			OutThunk->ReturnType = ReturnType.Type;
			return true;
		}

		void ShareRegisteredProtocolOwner(const FSessionState& State, FNativeThunk& Thunk) noexcept
		{
			if (!Thunk.bHostProtocol || !Thunk.UserData || Thunk.UserDataOwner) return;
			const auto Find = [&](const auto& Entries)
			{
				for (const auto& Entry : Entries)
				{
					if (Entry->bHostProtocol && Entry->UserData == Thunk.UserData && Entry->UserDataOwner)
					{
						Thunk.UserDataOwner = Entry->UserDataOwner;
						return true;
					}
				}
				return false;
			};
			if (Find(State.NativeThunks)) return;
			for (const auto& Generic : State.GenericHostTypeThunks)
			{
				if (Find(Generic->GeneratedNativeThunks)) return;
			}
		}

		std::shared_ptr<FNativeThunk> MakeProtocolThunk(
			const vs_interop_native_callback Callback,
			void* const UserData,
			const ENativeType ReturnType,
			const FSessionState* State = nullptr)
		{
			if (!Callback)
			{
				return nullptr;
			}
			auto Thunk = std::make_shared<FNativeThunk>();
			Thunk->bHostProtocol = true;
			Thunk->Callback = Callback;
			Thunk->UserData = UserData;
			Thunk->ReturnType = ReturnType;
			if (State)
			{
				ShareRegisteredProtocolOwner(*State, *Thunk);
			}

			return Thunk;
		}

		bool GenericHostTypeThunkCallback(
			const std::vector<std::string>& TypeArguments,
			FHostTypeDesc& OutConcreteType,
			std::string& OutError,
			void* const UserData) noexcept
		{
			auto* Thunk = static_cast<FGenericHostTypeThunk*>(UserData);
			if (!Thunk || !Thunk->Callback)
			{
				try
				{
					OutError = "generic host type factory is unavailable";
				}
				catch (...)
				{
				}
				return false;
			}
			try
			{
				::vs_interop_generic_type_context Context;
				Context.Owner = Thunk;
				Context.TypeArguments = &TypeArguments;
				const vs_interop_callback_result Result =
					Thunk->Callback(&Context, Thunk->UserData);
				if (Result != VS_INTEROP_CALLBACK_OK || !Context.bTypeSet)
				{
					OutError = !Context.Error.empty() ? std::move(Context.Error)
						: Result == VS_INTEROP_CALLBACK_OK
							? "generic host type factory did not describe its concrete type"
							: "generic host type factory failed";
					return false;
				}
				OutConcreteType = std::move(Context.Prototype);
				return true;
			}
			catch (const std::exception& Error)
			{
				try
				{
					OutError = Error.what();
				}
				catch (...)
				{
				}
				return false;
			}
			catch (...)
			{
				try
				{
					OutError = "foreign generic host type factory raised an exception";
				}
				catch (...)
				{
				}
				return false;
			}
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_function(
		vs_interop_session* session,
		const vs_interop_host_function_desc* function)
	{
		if (!session || !session->State || !function)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
			State, "RegisterHostFunction");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FHostFunctionDesc Function;
			std::shared_ptr<VeroScript::Interop::FNativeThunk> Thunk;
			std::string Error;
			if (!VeroScript::Interop::ConvertHostFunction(
				*function, Function, Thunk, Error))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::ReserveAdditional(State.NativeThunks, 1);
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			// The raw registry deliberately does not know language builtins. Keep a
			// validation copy so the public Session path applies its builtin-name and
			// running-state rules before the metadata-rich candidate is committed.
			const VeroScript::FHostFunctionDesc ValidationFunction = Function;
			std::string ErrorId;
			if (!Candidate.RegisterNative(std::move(Function),
					&VeroScript::Interop::NativeThunkCallback, Thunk.get(), ErrorId))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "host function registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!State.Session.RegisterNative(
					ValidationFunction.Name,
					ValidationFunction.ReturnType,
					ValidationFunction.ParamTypes,
					&VeroScript::Interop::NativeThunkCallback,
					Thunk.get(),
					ValidationFunction.ReturnHostTypeName,
					ValidationFunction.ParamHostTypeNames,
					ValidationFunction.ReturnElementTypeName,
					ValidationFunction.ParamElementTypeNames,
					ValidationFunction.ParamFlags,
					ValidationFunction.ReturnKeyTypeName,
					ValidationFunction.ReturnValueTypeName,
					ValidationFunction.ParamKeyTypeNames,
					ValidationFunction.ParamValueTypeNames))
			{
				VeroScript::Interop::SetError(State,
					"host function registration was rejected by the session");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.Session.GetBindingRegistry().Swap(Candidate);
			State.NativeThunks.push_back(std::move(Thunk));
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_annotation(
		vs_interop_session* session,
		const vs_interop_annotation_desc* annotation)
	{
		if (!session || !session->State || !annotation
			|| annotation->struct_size < sizeof(vs_interop_annotation_desc)
			|| !VeroScript::Interop::IsValidText(annotation->name)
			|| annotation->name.size == 0
			|| !VeroScript::Interop::IsValidText(annotation->detail)
			|| !VeroScript::Interop::IsValidText(annotation->documentation)
			|| (annotation->argument_count != 0 && !annotation->arguments)
			|| annotation->argument_count > 4096
			|| (annotation->name_flags & ~VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE) != 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
			State, "RegisterAnnotation");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<VeroScript::FHostAnnotationArgumentDesc> Arguments;
			Arguments.reserve(static_cast<std::size_t>(annotation->argument_count));
			for (std::uint64_t Index = 0; Index < annotation->argument_count; ++Index)
			{
				const auto& Source = annotation->arguments[Index];
				if (Source.struct_size < sizeof(vs_interop_annotation_argument_desc)
					|| !VeroScript::Interop::IsValidText(Source.name)
					|| Source.name.size == 0
					|| !VeroScript::Interop::IsValidText(Source.detail)
					|| !VeroScript::Interop::IsValidText(Source.documentation))
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				VeroScript::FHostAnnotationArgumentDesc Argument;
				Argument.Name = VeroScript::Interop::CopyString(Source.name);
				Argument.Detail = VeroScript::Interop::CopyString(Source.detail);
				Argument.Documentation =
					VeroScript::Interop::CopyString(Source.documentation);
				Arguments.push_back(std::move(Argument));
			}
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			std::string ErrorId;
			if (!Candidate.RegisterAnnotation(
					VeroScript::Interop::CopyString(annotation->name),
					VeroScript::Interop::CopyString(annotation->detail), ErrorId,
					VeroScript::Interop::CopyString(annotation->documentation),
					std::move(Arguments), VeroScript::AnnotationAllTargets,
					(annotation->name_flags & VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE) == 0))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "annotation registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_type(
		vs_interop_session* session,
		const vs_interop_host_type_desc* type,
		uint32_t* out_type_id)
	{
		if (!session || !session->State || !type || !out_type_id
			|| type->struct_size < VeroScript::Interop::HostTypeDescV1Size
			|| !VeroScript::Interop::IsValidText(type->name) || type->name.size == 0
			|| !VeroScript::Interop::IsValidText(type->documentation)
			|| (VeroScript::Interop::HasEnumUnderlyingTypeField(*type)
				&& !VeroScript::Interop::IsValidText(type->enum_underlying_type_name)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_type_id = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "RegisterHostType");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::EHostTypeKind Kind;
			std::string Error;
			std::vector<VeroScript::FHostAnnotationInstance> Annotations;
			if (!VeroScript::Interop::ConvertHostTypeKind(type->kind, Kind)
				|| !VeroScript::Interop::ConvertAnnotations(type->annotations, Annotations, Error))
			{
				VeroScript::Interop::SetError(State,
					Error.empty() ? "invalid host type kind" : std::move(Error));
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			VeroScript::FHostTypeId TypeId;
			std::string ErrorId;
			if (!Candidate.RegisterType(VeroScript::Interop::CopyString(type->name), Kind,
					TypeId, ErrorId, std::move(Annotations))
				|| !Candidate.SetTypeDocumentation(TypeId,
					VeroScript::Interop::CopyString(type->documentation), ErrorId)
				|| (type->inheritable != 0
					&& !Candidate.SetTypeInheritable(TypeId, true, ErrorId))
				|| (type->scoped_enum != 0
					&& !Candidate.SetEnumScoped(TypeId, true, ErrorId))
				|| (VeroScript::Interop::HasEnumUnderlyingTypeField(*type)
					&& type->enum_underlying_type_name.size != 0
					&& !Candidate.SetEnumUnderlyingType(
						TypeId,
						VeroScript::Interop::CopyString(type->enum_underlying_type_name),
						ErrorId)))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "host type registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			*out_type_id = TypeId.Value;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_member(
		vs_interop_session* session,
		const uint32_t type_id,
		const vs_interop_host_member_desc* member)
	{
		if (!session || !session->State || !member || type_id == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "RegisterHostMember");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FHostMemberDesc Member;
			std::shared_ptr<VeroScript::Interop::FNativeThunk> Thunk;
			std::string Error;
			if (!VeroScript::Interop::ConvertHostMember(*member, Member, Thunk, Error))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::ReserveAdditional(State.NativeThunks, Thunk ? 1u : 0u);
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			std::string ErrorId;
			const VeroScript::FHostTypeId TypeId{type_id};
			const bool bRegistered = Member.bOperator
				? Candidate.RegisterOperator(TypeId, std::move(Member), ErrorId)
				: Candidate.RegisterMember(TypeId, std::move(Member), ErrorId);
			if (!bRegistered)
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "host member registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			if (Thunk)
			{
				State.NativeThunks.push_back(std::move(Thunk));
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_protocol(
		vs_interop_session* session,
		const uint32_t type_id,
		const vs_interop_host_type_protocol* protocol)
	{
		if (!session || !session->State || !protocol || type_id == 0
			|| protocol->struct_size < sizeof(vs_interop_host_type_protocol))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "RegisterHostProtocol");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<std::shared_ptr<VeroScript::Interop::FNativeThunk>> Thunks;
			auto AddThunk = [&](const vs_interop_native_callback Callback,
				void* UserData, const VeroScript::ENativeType ReturnType)
			{
				if (auto Thunk = VeroScript::Interop::MakeProtocolThunk(
					Callback, UserData, ReturnType, &State))
				{
					Thunks.push_back(std::move(Thunk));
				}
			};
			AddThunk(protocol->clone_callback, protocol->clone_user_data,
				VeroScript::ENativeType::HostObject);
			AddThunk(protocol->equals_callback, protocol->equals_user_data,
				VeroScript::ENativeType::Bool);
			AddThunk(protocol->hash_callback, protocol->hash_user_data,
				VeroScript::ENativeType::UInt64);
			AddThunk(protocol->subclass_factory_callback,
				protocol->subclass_factory_user_data, VeroScript::ENativeType::HostObject);
			if (Thunks.empty())
			{
				VeroScript::Interop::SetError(State, "host protocol has no callbacks");
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::ReserveAdditional(State.NativeThunks, Thunks.size());
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			std::size_t ThunkIndex = 0;
			std::string ErrorId;
			const VeroScript::FHostTypeId TypeId{type_id};
			auto Register = [&](const vs_interop_native_callback Callback,
				auto&& Operation) -> bool
			{
				if (!Callback)
				{
					return true;
				}
				const auto& Thunk = Thunks[ThunkIndex++];
				if (!Operation(&VeroScript::Interop::NativeThunkCallback, Thunk.get(), ErrorId)) return false;
				Candidate.RetainNativeCallback(Thunk.get(), Thunk);
				return true;
			};
			if (!Register(protocol->clone_callback,
					[&](auto Callback, auto User, auto& ErrorOut)
					{
						return Candidate.RegisterTypeClone(TypeId, Callback, User, ErrorOut);
					})
				|| !Register(protocol->equals_callback,
					[&](auto Callback, auto User, auto& ErrorOut)
					{
						return Candidate.RegisterTypeEquals(TypeId, Callback, User, ErrorOut);
					})
				|| !Register(protocol->hash_callback,
					[&](auto Callback, auto User, auto& ErrorOut)
					{
						return Candidate.RegisterTypeHash(TypeId, Callback, User, ErrorOut);
					})
				|| !Register(protocol->subclass_factory_callback,
					[&](auto Callback, auto User, auto& ErrorOut)
					{
						return Candidate.RegisterTypeSubclassFactory(TypeId, Callback, User, ErrorOut);
					}))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "host protocol registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			for (auto& Thunk : Thunks)
			{
				State.NativeThunks.push_back(std::move(Thunk));
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_retain_callback_user_data(
		vs_interop_session* session, void* user_data, vs_interop_host_object_destroy release)
	{
		if (!session || !session->State || !user_data || !release)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}

		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}

		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "RetainCallbackUserData");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}

		try
		{
			std::vector<std::shared_ptr<VeroScript::Interop::FNativeThunk>> Matches;
			auto Find = [&](const auto& Thunks)
			{
				for (const auto& Thunk : Thunks)
				{
					if (Thunk->bHostProtocol && Thunk->UserData == user_data)
					{
						Matches.push_back(Thunk);
					}
				}
			};
			Find(State.NativeThunks);
			for (const auto& Generic : State.GenericHostTypeThunks)
			{
				Find(Generic->GeneratedNativeThunks);
			}

			if (Matches.empty() ||
				std::any_of(Matches.begin(), Matches.end(),
					[](const auto& Thunk)
					{
						return bool(Thunk->UserDataOwner);
					}))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}

			struct FLifetime
			{
				~FLifetime()
				{
					if (bCommitted)
					{
						try
						{
							Release(Data);
						}
						catch (...)
						{
						}
					}
				}

				void* Data;

				vs_interop_host_object_destroy Release;

				bool bCommitted = false;
			};
			auto Lifetime = std::make_shared<FLifetime>();
			Lifetime->Data = user_data;
			Lifetime->Release = release;
			for (const auto& Thunk : Matches)
			{
				Thunk->UserDataOwner = Lifetime;
			}

			Lifetime->bCommitted = true;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_generic_host_type_template(
		vs_interop_session* session,
		const vs_interop_string_view name,
		const uint32_t arity,
		const vs_interop_string_view documentation,
		const vs_interop_generic_host_type_callback callback,
		void* user_data,
		uint32_t* out_template_id)
	{
		if (!session || !session->State || !out_template_id || !callback
			|| arity == 0 || arity > 255
			|| !VeroScript::Interop::IsValidText(name) || name.size == 0
			|| !VeroScript::Interop::IsValidText(documentation))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_template_id = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
			State, "RegisterGenericHostTypeTemplate");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			auto Thunk = std::make_unique<VeroScript::Interop::FGenericHostTypeThunk>();
			Thunk->Callback = callback;
			Thunk->UserData = user_data;
			VeroScript::Interop::ReserveAdditional(State.GenericHostTypeThunks, 1);
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			VeroScript::FGenericHostTypeTemplateId TemplateId;
			std::string ErrorId;
			if (!Candidate.RegisterGenericHostTypeTemplate(
					VeroScript::Interop::CopyString(name), arity,
					&VeroScript::Interop::GenericHostTypeThunkCallback, Thunk.get(),
					TemplateId, ErrorId, VeroScript::Interop::CopyString(documentation)))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "generic host type template registration was rejected"
					: std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			State.GenericHostTypeThunks.push_back(std::move(Thunk));
			*out_template_id = TemplateId.Value;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_instantiate_generic_host_type(
		vs_interop_session* session,
		const uint32_t template_id,
		const vs_interop_string_list type_arguments,
		uint32_t* out_type_id)
	{
		if (!session || !session->State || template_id == 0 || !out_type_id)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_type_id = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
			State, "InstantiateGenericHostType");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<std::string> Arguments;
			if (!VeroScript::Interop::CopyStringList(type_arguments, Arguments))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::vector<std::size_t> GeneratedThunkCounts;
			GeneratedThunkCounts.reserve(State.GenericHostTypeThunks.size());
			for (const auto& Thunk : State.GenericHostTypeThunks)
			{
				GeneratedThunkCounts.push_back(Thunk->GeneratedNativeThunks.size());
			}
			auto RollBackGeneratedThunks = [&]() noexcept
			{
				for (std::size_t Index = 0; Index < GeneratedThunkCounts.size(); ++Index)
				{
					auto& Generated = State.GenericHostTypeThunks[Index]->GeneratedNativeThunks;
					if (Generated.size() > GeneratedThunkCounts[Index])
					{
						Generated.resize(GeneratedThunkCounts[Index]);
					}
				}
			};
			VeroScript::FHostTypeId TypeId;
			std::string ErrorId;
			bool bInstantiated = false;
			try
			{
				const VeroScript::Interop::FScopedSessionActivity Activity(State);
				if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
					Status != VS_INTEROP_STATUS_OK)
				{
					return Status;
				}

				auto Candidate = State.Session.GetBindingRegistry();
				if (Candidate.InstantiateGenericHostType(
						VeroScript::FGenericHostTypeTemplateId{template_id},
						std::move(Arguments), TypeId, ErrorId))
				{
					// Attach retained user data before publishing newly generated protocols.
					for (std::size_t Index = 0; Index < GeneratedThunkCounts.size(); ++Index)
					{
						auto& Generated = State.GenericHostTypeThunks[Index]->GeneratedNativeThunks;
						for (std::size_t New = GeneratedThunkCounts[Index]; New < Generated.size(); ++New)
						{
							VeroScript::Interop::ShareRegisteredProtocolOwner(State, *Generated[New]);
						}
					}
					State.Session.GetBindingRegistry().Swap(Candidate);
					bInstantiated = true;
				}
			}
			catch (...)
			{
				RollBackGeneratedThunks();
				throw;
			}
			if (!bInstantiated)
			{
				RollBackGeneratedThunks();
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "generic host type instantiation was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			*out_type_id = TypeId.Value;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	uint64_t VS_INTEROP_CALL vs_interop_generic_type_context_argument_count(
		const vs_interop_generic_type_context* context)
	{
		return context && context->TypeArguments
			? static_cast<uint64_t>(context->TypeArguments->size())
			: 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_get_argument(
		const vs_interop_generic_type_context* context,
		const uint64_t index,
		vs_interop_string_view* out_argument)
	{
		if (!context || !context->TypeArguments || !out_argument
			|| index >= context->TypeArguments->size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_argument = VeroScript::Interop::ViewOf(
			(*context->TypeArguments)[static_cast<std::size_t>(index)]);
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_set_type(
		vs_interop_generic_type_context* context,
		const vs_interop_host_type_desc* type)
	{
		if (!context || !context->Owner || !type || context->bTypeSet
			|| type->struct_size < VeroScript::Interop::HostTypeDescV1Size
			|| !VeroScript::Interop::IsValidText(type->name)
			|| !VeroScript::Interop::IsValidText(type->documentation)
			|| (VeroScript::Interop::HasEnumUnderlyingTypeField(*type)
				&& !VeroScript::Interop::IsValidText(type->enum_underlying_type_name)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			VeroScript::EHostTypeKind Kind;
			std::vector<VeroScript::FHostAnnotationInstance> Annotations;
			std::string Error;
			if (!VeroScript::Interop::ConvertHostTypeKind(type->kind, Kind)
				|| !VeroScript::Interop::ConvertAnnotations(
					type->annotations, Annotations, Error))
			{
				context->Error = Error.empty() ? "invalid generic host type descriptor" : Error;
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			context->Prototype.Name = VeroScript::Interop::CopyString(type->name);
			context->Prototype.Documentation =
				VeroScript::Interop::CopyString(type->documentation);
			context->Prototype.Kind = Kind;
			context->Prototype.Annotations = std::move(Annotations);
			context->Prototype.bInheritable = type->inheritable != 0;
			context->Prototype.bScopedEnum = type->scoped_enum != 0;
			if (VeroScript::Interop::HasEnumUnderlyingTypeField(*type)
				&& type->enum_underlying_type_name.size != 0)
			{
				context->Prototype.EnumUnderlyingTypeName =
					VeroScript::Interop::CopyString(type->enum_underlying_type_name);
			}
			context->bTypeSet = true;
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_add_member(
		vs_interop_generic_type_context* context,
		const vs_interop_host_member_desc* member)
	{
		if (!context || !context->Owner || !context->bTypeSet || !member)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			VeroScript::FHostMemberDesc Converted;
			std::shared_ptr<VeroScript::Interop::FNativeThunk> Thunk;
			std::string Error;
			if (!VeroScript::Interop::ConvertHostMember(*member, Converted, Thunk, Error))
			{
				context->Error = std::move(Error);
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::ReserveAdditional(context->Prototype.Members, 1);
			if (Thunk)
			{
				VeroScript::Interop::ReserveAdditional(context->Owner->GeneratedNativeThunks, 1);
				context->Owner->GeneratedNativeThunks.push_back(std::move(Thunk));
			}
			context->Prototype.Members.push_back(std::move(Converted));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_set_protocol(
		vs_interop_generic_type_context* context,
		const vs_interop_host_type_protocol* protocol)
	{
		if (!context || !context->Owner || !context->bTypeSet || !protocol
			|| protocol->struct_size < sizeof(vs_interop_host_type_protocol))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		if (!protocol->clone_callback && !protocol->equals_callback
			&& !protocol->hash_callback && !protocol->subclass_factory_callback)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			struct FPending
			{
				std::shared_ptr<VeroScript::Interop::FNativeThunk> Thunk;
				VeroScript::FNativeCallback* CallbackSlot = nullptr;
				void** UserSlot = nullptr;
			};
			std::vector<FPending> Pending;
			auto Add = [&](const vs_interop_native_callback Callback, void* UserData,
				const VeroScript::ENativeType ReturnType,
				VeroScript::FNativeCallback& CallbackSlot, void*& UserSlot)
			{
				if (!Callback)
				{
					return;
				}
				FPending Item;
				Item.Thunk = VeroScript::Interop::MakeProtocolThunk(
					Callback, UserData, ReturnType);
				Item.CallbackSlot = &CallbackSlot;
				Item.UserSlot = &UserSlot;
				Pending.push_back(std::move(Item));
			};
			Add(protocol->clone_callback, protocol->clone_user_data,
				VeroScript::ENativeType::HostObject,
				context->Prototype.CloneCallback, context->Prototype.CloneUserData);
			Add(protocol->equals_callback, protocol->equals_user_data,
				VeroScript::ENativeType::Bool,
				context->Prototype.EqualsCallback, context->Prototype.EqualsUserData);
			Add(protocol->hash_callback, protocol->hash_user_data,
				VeroScript::ENativeType::UInt64,
				context->Prototype.HashCallback, context->Prototype.HashUserData);
			Add(protocol->subclass_factory_callback, protocol->subclass_factory_user_data,
				VeroScript::ENativeType::HostObject,
				context->Prototype.SubclassFactory,
				context->Prototype.SubclassFactoryUserData);
			VeroScript::Interop::ReserveAdditional(context->Owner->GeneratedNativeThunks, Pending.size());
			VeroScript::Interop::ReserveAdditional(context->Prototype.CallbackLifetimes, Pending.size());
			for (auto& Item : Pending)
			{
				*Item.CallbackSlot = &VeroScript::Interop::NativeThunkCallback;
				*Item.UserSlot = Item.Thunk.get();
				context->Prototype.CallbackLifetimes.emplace_back(Item.Thunk.get(), Item.Thunk);
				context->Owner->GeneratedNativeThunks.push_back(std::move(Item.Thunk));
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	void VS_INTEROP_CALL vs_interop_generic_type_context_set_error(
		vs_interop_generic_type_context* context,
		const vs_interop_string_view error)
	{
		if (!context || !VeroScript::Interop::IsValidText(error))
		{
			return;
		}
		try
		{
			context->Error = VeroScript::Interop::CopyString(error);
		}
		catch (...)
		{
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_type_alias(
		vs_interop_session* session,
		const vs_interop_string_view name,
		const vs_interop_string_list type_parameters,
		const vs_interop_string_view target_type_name,
		const vs_interop_string_view documentation)
	{
		if (!session || !session->State
			|| !VeroScript::Interop::IsValidText(name) || name.size == 0
			|| !VeroScript::Interop::IsValidText(target_type_name)
			|| target_type_name.size == 0
			|| !VeroScript::Interop::IsValidText(documentation))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "RegisterTypeAlias");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<std::string> TypeParams;
			if (!VeroScript::Interop::CopyStringList(type_parameters, TypeParams))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (const auto Status = VeroScript::Interop::RequireMutableBindings(State);
				Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}

			auto Candidate = State.Session.GetBindingRegistry();
			std::string ErrorId;
			if (!Candidate.RegisterTypeAlias(VeroScript::Interop::CopyString(name),
					std::move(TypeParams), VeroScript::Interop::CopyString(target_type_name),
					ErrorId, VeroScript::Interop::CopyString(documentation)))
			{
				VeroScript::Interop::SetError(State, ErrorId.empty()
					? "host type alias registration was rejected" : std::move(ErrorId));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.GetBindingRegistry().Swap(Candidate);
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_export_bindings_json(
		vs_interop_session* session,
		vs_interop_buffer** out_json)
	{
		if (!session || !session->State || !out_json)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_json = nullptr;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "ExportBindingsJson");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::string Json;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!VeroScript::ExportBindingCatalogJson(
					State.Session.GetBindingCatalog(), Json))
			{
				VeroScript::Interop::SetError(State, "unable to export binding catalog JSON");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			return VeroScript::Interop::MakeTextBuffer(std::move(Json), out_json);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}
}
