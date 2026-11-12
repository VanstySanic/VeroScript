// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptProgramPreparation.h"
#include "VeroScriptGuiEvent.h"
#include <map>

namespace VeroScript::Gui
{
	enum class EGuiEventKind : std::uint8_t
	{
		Single,
		Multicast
	};

	struct FGuiPropertySchema
	{
		std::string Identity, DeclarationId, Name, Mark;
		std::string StorageType, ValueType, DefaultSource;
		FAnnotationSourcePosition Location;
		bool bBindable = false, bStyle = false, bBuiltin = false;
		// An explicit native presentation mapping, separate from style/binding support.
		bool bAnimatable = false;
		std::string Interpolator;
	};

	struct FGuiMethodSchema
	{
		std::string Identity, DeclarationId, Name, Mark, ReturnType;
		std::vector<FAnnotationParameterView> Parameters;
		FAnnotationSourcePosition Location;
		EMemberAccess Access = EMemberAccess::Public;
		bool bConst = false;
	};

	struct FGuiEventSchema
	{
		std::string Identity, DeclarationId, Name, Mark, ReturnType;
		std::vector<std::string> ParameterTypes;
		FAnnotationSourcePosition Location;
		EGuiEventKind Kind = EGuiEventKind::Single;
		EEventDispatch Dispatch = EEventDispatch::Direct;
	};

	struct FGuiTypeSchema
	{
		std::string Identity, DeclarationId, ModuleId, Name, CanonicalName, Mark, DefaultId;
		std::vector<std::string> Bases;
		std::vector<EMemberAccess> BaseAccess;
		std::vector<FGuiPropertySchema> Properties;
		std::vector<FGuiMethodSchema> Methods;
		std::vector<FGuiEventSchema> Events;
		FAnnotationSourcePosition Location;
		bool bWidget = false, bRegistered = false, bExported = false;
	};

	/** Owned, immutable declarations shared by GUI code generation and file frontends.
	 * Defaults retain source
	 * expressions and are never executed while describing a type. */
	class FGuiSchema final
	{
	public:
		static std::shared_ptr<const FGuiSchema> Build(const FPreparedProgram& Program,
													   std::vector<FScriptError>& Diagnostics)
		{
			return Build(Program.GetDeclarations(), Program.GetSourceMetadata(), Diagnostics);
		}
		static std::shared_ptr<const FGuiSchema> Build(const std::vector<FAnnotationDeclarationView>& Declarations,
													   const FSourceMetadataSnapshot& Metadata,
													   std::vector<FScriptError>& Diagnostics);
		const std::vector<FGuiTypeSchema>& GetTypes() const noexcept;
		const FGuiTypeSchema* FindType(const std::string& Name) const;
		const FGuiTypeSchema* FindWidget(const std::string& Tag, std::string& Error) const;
		const FGuiPropertySchema* FindProperty(const FGuiTypeSchema& Type, const std::string& Name) const;
		const FGuiEventSchema* FindEvent(const FGuiTypeSchema& Type, const std::string& Name) const;
		const std::vector<FEventDefinition>& GetRoutedEvents() const;

	private:
		std::vector<FGuiTypeSchema> Types;
		std::map<std::string, std::size_t> TypeNames;
		std::map<std::string, std::vector<std::size_t>> Tags;
	};
} // namespace VeroScript::Gui
