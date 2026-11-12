// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			bool MethodMatchesVirtualInterfaceSlot(
				const FUserTypeInfo& Type,
				const FMethodDecl& Method) const
			{
				if (Type.InterfaceTypeNames.empty() || !Type.Methods)
				{
					return false;
				}
				for (const std::string& InterfaceName : Type.InterfaceTypeNames)
				{
					const auto It = UserTypes.find(InterfaceName);
					if (It == UserTypes.end() || !It->second.Methods)
					{
						continue;
					}
					for (const FMethodDecl& IfaceMethod : *It->second.Methods)
					{
						if (IfaceMethod.Name != Method.Name
							|| IfaceMethod.Parameters.size() != Method.Parameters.size()
							|| IfaceMethod.bIsConst != Method.bIsConst)
						{
							continue;
						}
						// Pure or virtual default: implementor slot is virtual.
						if (IfaceMethod.bPureVirtual || IfaceMethod.bVirtual)
						{
							bool bParamsMatch = true;
							for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
							{
								if (Method.Parameters[Index].TypeName
									!= IfaceMethod.Parameters[Index].TypeName
									|| Method.Parameters[Index].bIsRef
									!= IfaceMethod.Parameters[Index].bIsRef)
								{
									bParamsMatch = false;
									break;
								}
							}
							if (bParamsMatch)
							{
								return true;
							}
						}
					}
				}
				return false;
			}

			static std::string ConstructionLocalName(
				std::string Name, const std::vector<FFunctionParam>& Parameters)
			{
				while (std::any_of(Parameters.begin(), Parameters.end(),
					[&](const auto& Parameter)
					{
						return Parameter.Name == Name;
					}))
				{
					Name += '_';
				}
				return Name;
			}

			static std::string ConstructionCompleteField(const FUserTypeInfo& Type)
			{
				std::string Name = "___vs_constructed_" + SanitizeForMangle(
					Type.OpenGenericName.empty() ? Type.Name : Type.OpenGenericName);
				while ((Type.Fields && std::any_of(Type.Fields->begin(), Type.Fields->end(),
					[&](const auto& Field)
					{
						return Field.Name == Name;
					}))
					|| (Type.Methods && std::any_of(Type.Methods->begin(), Type.Methods->end(),
						[&](const auto& Method)
						{
							return Method.Name == Name;
						})))
				{
					Name += '_';
				}
				return Name;
			}

			std::vector<FUserTypeInfo> ConstructionAncestors(const FUserTypeInfo& Type)
			{
				std::vector<FUserTypeInfo> Result;
				std::unordered_set<std::string> Seen{Type.Name};
				const auto Append = [&](const FUserTypeInfo& Owner)
				{
					TValueScopeRestore<decltype(TypeSubst)> Bindings(TypeSubst);
					PushTypeSubstForUserType(Owner);
					auto Parents = Owner.BaseTypeNames;
					Parents.insert(Parents.end(), Owner.InterfaceTypeNames.begin(), Owner.InterfaceTypeNames.end());
					for (const auto& Parent : Parents)
					{
						const auto* Declaration = ResolveUserTypeForLookup(Parent);
						if (Declaration && Seen.insert(Declaration->Name).second)
						{
							Result.push_back(*Declaration);
						}
					}
				};
				Append(Type);
				for (std::size_t Index = 0; Index < Result.size(); ++Index)
				{
					const auto Owner = Result[Index];
					Append(Owner);
				}
				return Result;
			}

			const FMethodDecl* ConstructionImplementation(const FUserTypeInfo& Type,
				const FMethodDecl& Method, const std::vector<std::string>& ParameterTypes,
				std::unordered_map<std::string, const FMethodDecl*>& Implementations)
			{
				const auto Cached = Implementations.find(Type.Name);
				if (Cached != Implementations.end()) return Cached->second;
				// Cache misses as well as matches. Shared ancestors in an interface
				// diamond must not be searched again for every inheritance path.
				auto& Result = Implementations.emplace(Type.Name, nullptr).first->second;
				TValueScopeRestore<decltype(TypeSubst)> Bindings(TypeSubst);
				PushTypeSubstForUserType(Type);
				for (const auto& Candidate : *Type.Methods)
				{
					if (Candidate.Name != Method.Name || Candidate.bIsStatic
						|| Candidate.bIsConst != Method.bIsConst
						|| Candidate.Parameters.size() != Method.Parameters.size())
					{
						continue;
					}
					bool Matches = true;
					for (std::size_t Index = 0; Index < ParameterTypes.size(); ++Index)
					{
						const auto& Parameter = Candidate.Parameters[Index];
						Matches = Matches && Parameter.bIsVariadic == Method.Parameters[Index].bIsVariadic
							&& MapParamType(Parameter.TypeName, Parameter.bIsRef, Parameter.bIsConst) == ParameterTypes[Index];
					}
					if (Matches) return Result = &Candidate;
				}
				auto Parents = Type.BaseTypeNames;
				Parents.insert(Parents.end(), Type.InterfaceTypeNames.begin(), Type.InterfaceTypeNames.end());
				for (const auto& Parent : Parents)
				{
					const auto* Declaration = ResolveUserTypeForLookup(Parent);
					if (Declaration)
					{
						const auto Owner = *Declaration;
						if (const auto* Implementation = ConstructionImplementation(Owner, Method, ParameterTypes, Implementations))
						{
							return Result = Implementation;
						}
					}
				}
				return nullptr;
			}

			void EmitConstructionMethodDispatch(const FUserTypeInfo& Type, const FMethodDecl& Method)
			{
				if (Method.bIsStatic) return;
				std::vector<std::string> ParameterTypes;
				for (const auto& Parameter : Method.Parameters)
				{
					ParameterTypes.push_back(MapParamType(Parameter.TypeName, Parameter.bIsRef, Parameter.bIsConst));
				}
				const auto Ancestors = ConstructionAncestors(Type);
				std::unordered_map<std::string, const FMethodDecl*> Implementations;
				bool Virtual = Method.bVirtual || Method.bOverride || Method.bPureVirtual;
				for (const auto& Ancestor : Ancestors)
				{
					const auto* Implementation = ConstructionImplementation(Ancestor, Method, ParameterTypes, Implementations);
					Virtual = Virtual || (Implementation && (Implementation->bVirtual
						|| Implementation->bOverride || Implementation->bPureVirtual));
				}
				if (!Virtual) return;
				bNeedsConstructionPhase = true;
				const auto Phase = ConstructionLocalName("___vs_construction_phase", Method.Parameters);
				Out << "\tif (const auto* " << Phase << " = VsConstructionScope::Find(VsConstructionAddress(*this)))\n\t{\n";
				for (const auto& Ancestor : Ancestors)
				{
					if (Ancestor.bIsInterface)
					{
						continue;
					}
					Out << "\t\tif (*" << Phase << "->Types[0] == typeid(" << MapBaseCppType(Ancestor.Name) << "))\n\t\t{\n";
					const auto* Implementation = ConstructionImplementation(Ancestor, Method, ParameterTypes, Implementations);
					if (!Implementation || Implementation->bPureVirtual || Implementation->bDeleted)
					{
						Out << "\t\t\tVsScriptFail(\"virtual method unavailable during construction\");\n";
					}
					else
					{
						const bool Void = ResolveAliasChain(ApplyTypeSubst(Method.ReturnType)) == "void";
						Out << "\t\t\t" << (Void ? "" : "return ") << "this->"
							<< MapBaseCppType(Ancestor.Name) << "::" << SanitizeSymbolName(Method.Name) << '(';
						for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
						{
							if (Index)
							{
								Out << ", ";
							}
							Out << Method.Parameters[Index].Name;
						}
						Out << ");\n";
						if (Void)
						{
							Out << "\t\t\treturn;\n";
						}
					}
					Out << "\t\t}\n";
				}
				Out << "\t\tif (!" << Phase << "->Contains(typeid(" << MapBaseCppType(Type.Name)
					<< ")))\n\t\t{\n\t\t\tVsScriptFail(\"virtual method unavailable during construction\");\n\t\t}\n\t}\n";
			}

			void EmitMethodMemberSignature(
				const FUserTypeInfo& Type,
				const FMethodDecl& Method,
				const bool bPureVirtual)
			{
				Out << "\t";
				// Static member functions: no virtual/override/final/const, no `= 0`.
				const bool bEmitVirtual = !Method.bIsStatic
					&& (Type.bIsInterface
					? (Method.bPureVirtual || Method.bVirtual)
						: (Method.bVirtual || Method.bOverride
							|| MethodMatchesVirtualInterfaceSlot(Type, Method)));
				if (Method.bIsStatic)
				{
					Out << "static ";
				}
				else if (bEmitVirtual)
				{
					Out << "virtual ";
				}
				Out << MapType(Method.ReturnType) << ' ' << SanitizeSymbolName(Method.Name) << '(';
				for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Method.Parameters[Index];
					CheckParamSupport(Param);
					if (bFailed)
					{
						return;
					}
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamType(Param.TypeName, Param.bIsRef, Param.bIsConst) << ' '
						<< Param.Name;
				}
				Out << ')';
				if (!Method.bIsStatic)
				{
					if (Method.bIsConst)
					{
						Out << " const";
					}
					if (Method.bOverride)
					{
						Out << " override";
					}
					if (Method.bFinal)
					{
						Out << " final";
					}
					if (bPureVirtual)
					{
						Out << " = 0;\n";
					}
				}
				if (Method.bDeleted)
				{
					Out << " = delete;\n";
				}
			}

			std::vector<std::string> DeferredTypeDefinitions;

			void EmitDefaultStorageDefinition(const FUserTypeInfo& Type)
			{
				if (Type.bIsClass || Type.bIsInterface) return;
				const std::string Qualifier = EmitTypeDefinitionQualifier(Type);
				Out << "inline void VsInitializeDefaultStorage(" << Qualifier << "& " << SelfReceiverName << ");\n";
				FOperandOutputScope OutputScope(Out);
				TValueScopeRestore<decltype(TypeSubst)> SubstScope(TypeSubst);
				TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
				TValueScopeRestore<bool> SelfScope(bInSelfContext);
				TValueScopeRestore<std::string> ModuleScope(ActiveModulePath);
				TValueScopeRestore<std::string> NamespaceScope(ActiveNamespace);
				PushTypeSubstForUserType(Type);
				LocalTypes.clear();
				bInSelfContext = false;
				ActiveModulePath = Type.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Type.Name);
				const std::string DefinitionQualifier = EmitTypeDefinitionQualifier(Type);
				Out << "inline void VsInitializeDefaultStorage(" << DefinitionQualifier << "& " << SelfReceiverName << ") {\n";
				if (Type.Destructor && Type.Destructor->Body && Type.Constructors->empty())
				{
					bNeedsConstructionPhase = true;
					Out << "\tVsConstructionCompletion ___vs_completion(" << SelfReceiverName << '.' << ConstructionCompleteField(Type) << ".Complete);\n";
				}
				if (!Type.Constructors->empty())
				{
					const auto* Constructor = ResolveCtor(Type, {});
					if (Constructor && !Constructor->bDeleted)
					{
						Out << "\t" << MangleCtorInit(Type.Name) << '(' << SelfReceiverName;
						if (!Constructor->Parameters.empty())
						{
							Out << ", ";
							const std::vector<std::unique_ptr<FExpr>> Arguments;
							EmitCallArgsForUserTypeParams(Type, Constructor->Parameters, Arguments);
						}
						Out << ", VsConstructionAddress(" << SelfReceiverName << "));\n";
					}
					else
					{
						// CompileCheck rejects uses of this default construction. Keep
						// unused declarations from referencing a nonexistent initializer.
						Out << "\tVsScriptFail(\"default constructor is unavailable\");\n";
					}
					Out << "}\n";
					DeferredTypeDefinitions.push_back(Out.str());
					return;
				}
				for (const auto& Base : Type.BaseTypeNames)
				{
					Out << "\tVsInitializeDefaultStorage(static_cast<" << MapBaseCppType(Base)
						<< "&>(" << SelfReceiverName << "));\n";
				}

				for (const auto& Field : *Type.Fields)
				{
					if (Field.bIsStatic)
					{
						continue;
					}

					if (Field.DefaultValue)
					{
						if (Field.DefaultValue->Kind == EExprKind::Call
							&& TryEmitStructFieldConstructor(Field, static_cast<const FCallExpr&>(*Field.DefaultValue)))
						{
							continue;
						}
						if (TryEmitStructFieldValue(Field, *Field.DefaultValue))
						{
							continue;
						}
						Out << "\t" << SelfReceiverName << '.' << Field.Name << " = ";
						EmitCoercedValue(Field.TypeName, *Field.DefaultValue);
						Out << ";\n";
					}
					else
					{
						Out << "\tVsInitializeDefaultStorage(" << SelfReceiverName << '.' << Field.Name << ");\n";
					}
				}
				Out << "}\n";
				DeferredTypeDefinitions.push_back(Out.str());
			}

			bool TryEmitStructFieldConstructor(const FFieldDecl& Field, const FCallExpr& Call)
			{
				if (Call.CalleeExpr) return false;
				const auto* FieldType = ResolveUserTypeForLookup(Field.TypeName);
				if (!FieldType || FieldType->bIsClass || FieldType->bIsInterface) return false;
				const auto Owner = *FieldType;
				const auto* CalledType = ResolveUserTypeForLookup(
					Call.TypeArgs.empty() ? Call.Callee : ReconstructTypeFromCall(Call));
				if (!CalledType || CalledType->Name != Owner.Name) return false;
				std::vector<std::string> Arguments;
				for (const auto* Argument : Call.GetArguments())
				{
					Arguments.push_back(InferExprType(*Argument));
				}
				const FConstructorDecl* Constructor = nullptr;
				{
					TValueScopeRestore<decltype(TypeSubst)> Bindings(TypeSubst);
					PushTypeSubstForUserType(Owner);
					Constructor = ResolveCtor(Owner, Arguments);
				}
				if (!Owner.Constructors->empty() && !Constructor) return false;
				const auto EmitInitialization = [&]
				{
					Out << MangleCtorInit(Owner.bResolutionOnlyMonomorph ? CtorInitOwnerName(Owner.Name) : Owner.Name)
						<< '(' << SelfReceiverName << '.' << Field.Name;
					if (Constructor && !Constructor->Parameters.empty())
					{
						Out << ", ";
						EmitCallArgsForUserTypeParams(Owner, Constructor->Parameters, Call.GetArguments());
					}
					Out << ", VsConstructionAddress(" << SelfReceiverName << '.' << Field.Name << "))";
				};
				Out << '\t';
				if (!TryEmitSequencedExpression(Call, nullptr, EmitInitialization))
				{
					EmitInitialization();
				}
				Out << ";\n";
				return true;
			}

			bool TryEmitStructFieldValue(const FFieldDecl& Field, const FExpr& Value)
			{
				const auto* Type = ResolveUserTypeForLookup(Field.TypeName);
				if (!Type || Type->bIsClass || Type->bIsInterface) return false;
				std::string Storage = "___vs_field_storage";
				while (LocalTypes.count(Storage))
				{
					Storage += '_';
				}
				Out << "\tVsInitializeConstructedField(" << SelfReceiverName << '.' << Field.Name << ", [&](void* " << Storage
					<< ")\n\t{\n\t\t::new (" << Storage << ") " << MapType(Field.TypeName) << '(';
				EmitCoercedValue(Field.TypeName, Value);
				Out << ");\n\t});\n";
				return true;
			}

			std::string EmitTypeDefinitionQualifier(const FUserTypeInfo& Type)
			{
				std::string Qualifier = UserCppType(Type.Name);
				if (Type.TypeParams.empty() || !Type.MonoTypeArgs.empty()) return Qualifier;
				Out << "template <";
				Qualifier += '<';
				for (std::size_t Index = 0; Index < Type.TypeParams.size(); ++Index)
				{
					if (Index)
					{
						Out << ", ";
						Qualifier += ", ";
					}
					Out << "typename " << Type.TypeParams[Index];
					Qualifier += Type.TypeParams[Index];
				}
				const auto Open = OpenGenericTypes.find(Type.Name);
				if (Open != OpenGenericTypes.end() && !Open->second.Bounds.empty())
				{
					std::string Constraint = "__vs_type_constraint";
					while (std::find(Type.TypeParams.begin(), Type.TypeParams.end(), Constraint) !=
						Type.TypeParams.end())
					{
						Constraint += '_';
					}

					Out << ", typename " << Constraint;
					Qualifier += ", " + Constraint;
				}
				Out << ">\n";
				return Qualifier + '>';
			}

			void QueueMemberMethodDefinition(const FUserTypeInfo& Type, const FMethodDecl& Method)
			{
				FOperandOutputScope OutputScope(Out);
				TValueScopeRestore<decltype(TypeSubst)> SubstScope(TypeSubst);
				PushTypeSubstForUserType(Type);
				const std::string Qualifier = EmitTypeDefinitionQualifier(Type);
				Out << "inline " << MapType(Method.ReturnType) << ' ' << Qualifier << "::"
					<< SanitizeSymbolName(Method.Name) << '(';
				for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
				{
					if (Index)
					{
						Out << ", ";
					}

					const auto& Param = Method.Parameters[Index];
					Out << MapParamType(Param.TypeName, Param.bIsRef, Param.bIsConst) << ' ' << Param.Name;
				}
				Out << ')';
				if (Method.bIsConst && !Method.bIsStatic)
				{
					Out << " const";
				}

				EmitMemberMethodBody(Type, Method);
				DeferredTypeDefinitions.push_back(Out.str());
			}

			void EmitMemberMethodBody(const FUserTypeInfo& Type, const FMethodDecl& Method)
			{
				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
				RetainedReferenceAliases.clear();
				LocalTypes.clear();
				LocalConstness.clear();
				PushTypeSubstForUserType(Type);
				// Static member functions have no `this`; disable self/member context so
				// bare names never bind to instance fields and `this` is rejected.
				const bool bStatic = Method.bIsStatic;
				bInSelfContext = !bStatic;
				bSelfIsConst = !bStatic && Method.bIsConst;
				bMemberMethodBody = !bStatic;
				SelfTypeName = bStatic ? std::string{} : Type.Name;
				ActiveModulePath = Type.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Type.Name);
				ActiveReturnType = Method.ReturnType;
				for (const FFunctionParam& Param : Method.Parameters)
				{
					LocalTypes[Param.Name] = ApplyTypeSubst(Param.TypeName);
					LocalConstness[Param.Name] = Param.bIsConst;
				}
				Out << " {\n";
				EmitConstructionMethodDispatch(Type, Method);
				if (!Method.Body)
				{
					Fail("AOT: method body required");
					return;
				}
				if (!Method.Body->DefinitionModulePath.empty())
				{
					ActiveModulePath = Method.Body->DefinitionModulePath;
					ActiveNamespace = Method.Body->DefinitionNamespace;
				}
				for (const std::unique_ptr<FStmt>& Child : Method.Body->Statements)
				{
					if (Child)
					{
						EmitStmt(*Child, 2);
					}
				}
				Out << "\t}\n";
				bInSelfContext = false;
				bSelfIsConst = false;
				bMemberMethodBody = false;
				SelfTypeName.clear();
				TypeSubst.clear();
			}

			/**
			 * Emit file-scope definitions for a type's static data members, right after
			 * the type/template is closed. For an open template the definition repeats
			 * the `template <...>` header and the `<T...>` args on the qualified name so
			 * each instantiation (`vs_Box<int>` vs `vs_Box<string>`) gets its own storage.
			 * Caller must have `PushTypeSubstForUserType(Type)` / ActiveOpenTypeParams
			 * still live from EmitUserTypeStruct.
			 */
			void EmitStaticFieldDefinitions(const FUserTypeInfo& Type)
			{
				if (bFailed || !Type.Fields)
				{
					return;
				}
				FOperandOutputScope OutputScope(Out);
				for (const FFieldDecl& Field : *Type.Fields)
				{
					if (!Field.bIsStatic)
					{
						continue;
					}
					PushTypeSubstForUserType(Type);
					const std::string Qualifier = EmitTypeDefinitionQualifier(Type);
					Out << "thread_local " << MapType(Field.TypeName) << ' ' << Qualifier;
					Out << "::" << Field.Name;
					if (Field.DefaultValue)
					{
						TValueScopeRestore<bool> InitializerScope(bEmittingNamespaceInitializer);
						bEmittingNamespaceInitializer = true;
						Out << " = ";
						EmitCoercedValue(Field.TypeName, *Field.DefaultValue);
						Out << ";\n";
					}
					else
					{
						// No initializer: value-initialize (0 / false / empty), matching
						// the VM's zero-default for uninitialized static fields.
						Out << "{};\n";
					}
				}
				DeferredTypeDefinitions.push_back(Out.str());
			}

			void EmitUserTypeStruct(const FUserTypeInfo& Type)
			{
				PushTypeSubstForUserType(Type);
				if (Type.bIsClass || Type.bIsInterface)
				{
					bNeedsMemory = true;
					bNeedsObjectBase = true;
				}
				Out << "struct " << UserCppType(Type.Name);
				if (Type.bFinal)
				{
					Out << " final";
				}
				bool bFirstBase = true;
				auto EmitBase = [&](const std::string& BaseName, const bool bVirtual = false)
				{
					if (BaseName.empty())
					{
						return;
					}
					Out << (bFirstBase ? " : " : ", ")
						<< (bVirtual ? "virtual public " : "public ")
						<< MapBaseCppType(BaseName);
					bFirstBase = false;
				};
				if (Type.bIsClass || Type.bIsInterface)
				{
					Out << " : virtual public VsObjectBase";
					bFirstBase = false;
				}
				for (const std::string& Base : Type.BaseTypeNames)
				{
					EmitBase(Base);
				}
				for (const std::string& Iface : Type.InterfaceTypeNames)
				{
					EmitBase(Iface);
				}
				Out << " {\n";
				if (!Type.bIsClass && !Type.bIsInterface)
				{
					bNeedsConstructionPhase = true;
					const auto Name = UserCppType(Type.Name);
					std::string InitializerType = "___VsInitializer";
					while (std::find(Type.TypeParams.begin(), Type.TypeParams.end(), InitializerType) != Type.TypeParams.end())
					{
						InitializerType += '_';
					}
					Out << '\t' << Name << "() = default;\n"
						<< '\t' << Name << "(const " << Name << "&) = default;\n"
						<< '\t' << Name << '(' << Name << "&&) = default;\n"
						<< '\t' << Name << "& operator=(const " << Name << "&) = default;\n"
						<< "\ttemplate <typename " << InitializerType << ">\n"
						<< '\t' << Name << "(VsConstructionValueTag, " << InitializerType << "&& Init)\n"
						<< "\t\t: " << Name << "()\n\t{\n\t\tInit(*this);\n\t}\n";
				}
				if (Type.Destructor && Type.Destructor->Body)
				{
					// C++ storage exists before the script constructor helper runs. Keep
					// its script destructor disabled until this owner's helper succeeds.
					Out << "\t::VsConstructionState " << ConstructionCompleteField(Type) << ";\n";
				}

				const FDestructorDecl* const ScriptDtor = Type.Destructor;
				if (Type.bIsInterface || TypeNeedsOopLayout(Type) || Type.bIsClass)
				{
					if (ScriptDtor && ScriptDtor->bPureVirtual)
					{
						Out << "\tvirtual ~" << UserCppType(Type.Name) << "() = 0;\n";
					}
					else if (ScriptDtor && ScriptDtor->Body)
					{
						// Script classes are reference types and can participate in checked
						// down/cross casts. Keep every generated class polymorphic even when its
						// source destructor was not explicitly virtual.
						const bool bVirtualDtor = ScriptDtor->bVirtual
							|| Type.bIsInterface || Type.bIsClass;
						Out << "\t" << (bVirtualDtor ? "virtual " : "") << '~'
							<< UserCppType(Type.Name) << "();\n";
						FOperandOutputScope OutputScope(Out);
						TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
						RetainedReferenceAliases.clear();
						const std::string Qualifier = EmitTypeDefinitionQualifier(Type);
						Out << "inline " << Qualifier << "::~" << UserCppType(Type.Name) << "() {\n";
						Out << "\tif (!" << ConstructionCompleteField(Type) << ".Complete) return;\n\t"
							<< ConstructionCompleteField(Type) << ".Complete = false;\n";
						LocalTypes.clear();
						LocalConstness.clear();
						PushTypeSubstForUserType(Type);
						bInSelfContext = true;
						bSelfIsConst = false;
						bMemberMethodBody = true;
						bInDestructorBody = true;
						SelfTypeName = Type.Name;
						ActiveModulePath = Type.ModulePath;
						ActiveNamespace = NamespaceOfSymbol(Type.Name);
						for (const std::unique_ptr<FStmt>& Child : ScriptDtor->Body->Statements)
						{
							if (Child)
							{
								EmitStmt(*Child, 2);
							}
						}
						Out << "\t}\n";
						DeferredTypeDefinitions.push_back(Out.str());
						bInSelfContext = false;
						bSelfIsConst = false;
						bMemberMethodBody = false;
						bInDestructorBody = false;
						SelfTypeName.clear();
						TypeSubst.clear();
					}
					else if (Type.bIsInterface || Type.bIsClass || TypeNeedsOopLayout(Type))
					{
						Out << "\tvirtual ~" << UserCppType(Type.Name) << "() = default;\n";
					}
				}

				if (Type.Fields)
				{
					for (const FFieldDecl& Field : *Type.Fields)
					{
						// Static data members are per-type (monomorph / template
						// instantiation) storage; their definition is emitted out of
						// line after the struct. Instance fields stay as-is.
						Out << "\t"
							<< (Field.bIsStatic ? "static thread_local " : "")
							<< MapType(Field.TypeName) << ' ' << Field.Name << ";\n";
					}
				}

				if (Type.bIsInterface)
				{
					EMemberAccess LastAccess = static_cast<EMemberAccess>(-1);
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (Method.Access != LastAccess)
						{
							// Generated layouts are private to this translation unit. Script access,
							// including exact-signature friendship, is already enforced by the
							// mandatory CompileCheck; C++ labels cannot model it without declaration-
							// order coupling, so keep the implementation surface uniformly public.
							Out << "\tpublic:\n";
							LastAccess = Method.Access;
						}
						const bool bPure = Method.bPureVirtual;
						EmitMethodMemberSignature(Type, Method, bPure);
						if (bFailed)
						{
							TypeSubst.clear();
							return;
						}
						if (!bPure && !Method.bDeleted)
						{
							Out << ";\n";
							QueueMemberMethodDefinition(Type, Method);
						}
					}
					Out << "};\n\n";
					EmitStaticFieldDefinitions(Type);
					TypeSubst.clear();
					return;
				}

				if (TypeNeedsOopLayout(Type))
				{
					EMemberAccess LastAccess = static_cast<EMemberAccess>(-1);
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (Method.Access != LastAccess)
						{
							Out << "\tpublic:\n";
							LastAccess = Method.Access;
						}
						EmitMethodMemberSignature(Type, Method, Method.bPureVirtual);
						if (bFailed)
						{
							TypeSubst.clear();
							return;
						}
						if (!Method.bDeleted && !Method.bPureVirtual)
						{
							Out << ";\n";
							QueueMemberMethodDefinition(Type, Method);
						}
					}
				}
				else if (Type.Methods)
				{
					// Value struct with only static helpers: emit `static` members
					// without a vtable / virtual destructor.
					EMemberAccess LastAccess = static_cast<EMemberAccess>(-1);
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (!Method.bIsStatic)
						{
							continue;
						}
						if (Method.Access != LastAccess)
						{
							Out << "\tpublic:\n";
							LastAccess = Method.Access;
						}
						EmitMethodMemberSignature(Type, Method, false);
						if (bFailed)
						{
							TypeSubst.clear();
							return;
						}
						if (!Method.bDeleted)
						{
							Out << ";\n";
							QueueMemberMethodDefinition(Type, Method);
						}
					}
				}
				Out << "};\n\n";
				EmitDefaultStorageDefinition(Type);
				if (ScriptDtor && ScriptDtor->bPureVirtual)
				{
					// A pure virtual destructor still needs a callable base definition.
					FOperandOutputScope OutputScope(Out);
					TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
					RetainedReferenceAliases.clear();
					const std::string Qualifier = EmitTypeDefinitionQualifier(Type);
					Out << "inline " << Qualifier << "::~"
						<< UserCppType(Type.Name) << "() {\n";
					Out << "\tif (!" << ConstructionCompleteField(Type) << ".Complete) return;\n\t"
						<< ConstructionCompleteField(Type) << ".Complete = false;\n";
					LocalTypes.clear();
					LocalConstness.clear();
					PushTypeSubstForUserType(Type);
					bInSelfContext = true;
					bSelfIsConst = false;
					bMemberMethodBody = true;
					bInDestructorBody = true;
					SelfTypeName = Type.Name;
					ActiveModulePath = Type.ModulePath;
					ActiveNamespace = NamespaceOfSymbol(Type.Name);
					if (!ScriptDtor->Body)
					{
						Fail("AOT: pure virtual destructor requires a same-file definition");
						return;
					}
					for (const std::unique_ptr<FStmt>& Child : ScriptDtor->Body->Statements)
					{
						if (Child)
						{
							EmitStmt(*Child, 1);
						}
					}
					Out << "}\n\n";
					DeferredTypeDefinitions.push_back(Out.str());
					bInSelfContext = false;
					bSelfIsConst = false;
					bMemberMethodBody = false;
					bInDestructorBody = false;
					SelfTypeName.clear();
					TypeSubst.clear();
				}
				if (Type.Methods)
				{
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (!Method.bPureVirtual || !Method.Body)
						{
							continue;
						}
						QueueMemberMethodDefinition(Type, Method);
					}
				}
				EmitStaticFieldDefinitions(Type);
				TypeSubst.clear();
			}

			/**
			 * Emit `where T : I` bounds as a C++17 SFINAE default template parameter
			 * (`typename = std::enable_if_t<std::is_convertible_v<...>>`).
			 * Caller must already be inside `template <...` with at least the real
			 * type parameters written; this appends a leading comma then the constraint.
			 * Must be called with the relevant type params active in
			 * ActiveOpenTypeParams so `IBox<U>` maps `U` correctly.
			 */
			void EmitEnableIfTemplateParam(
				const std::vector<FTypeParamBound>& Bounds,
				const bool bWithDefault = true)
			{
				if (Bounds.empty() || bFailed)
				{
					return;
				}
				for (const FTypeParamBound& Bound : Bounds)
				{
					if (ActiveOpenTypeParams.find(Bound.TypeParam) == ActiveOpenTypeParams.end())
					{
						Fail(
							"AOT: where bound type parameter '" + Bound.TypeParam
								+ "' is not a template type parameter of this declaration");
						return;
					}
				}
				if (!bWithDefault)
				{
					Out << ", typename";
					return;
				}
				bNeedsTypeTraits = true;
				Out << ", typename = std::enable_if_t<";
				for (std::size_t Index = 0; Index < Bounds.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << " && ";
					}
					// Script classes/interfaces use shared handles; structs implement
					// the same nominal interface contract as native value types.
					Out << "(std::is_convertible_v<" << Bounds[Index].TypeParam << ", "
						<< MapType(Bounds[Index].InterfaceTypeName)
						<< "> || std::is_base_of_v<" << MapBaseCppType(Bounds[Index].InterfaceTypeName)
						<< ", " << Bounds[Index].TypeParam << ">)";
				}
				Out << '>';
			}

			void EmitOpenGenericForwardDecl(const FOpenGenericType& Open)
			{
				ActiveOpenTypeParams.clear();
				for (const std::string& Param : Open.TypeParams)
				{
					ActiveOpenTypeParams.insert(Param);
				}
				Out << "template <";
				for (std::size_t Index = 0; Index < Open.TypeParams.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << "typename " << Open.TypeParams[Index];
					if (Index < Open.TypeParamDefaults.size()
						&& !Open.TypeParamDefaults[Index].empty())
					{
						Out << " = " << MapType(Open.TypeParamDefaults[Index]);
					}
				}
				EmitEnableIfTemplateParam(Open.Bounds);
				Out << "> struct " << UserCppType(Open.Name) << ";\n";
				ActiveOpenTypeParams.clear();
			}

			void EmitOpenGenericCtorPrototypes(const FOpenGenericType& Open)
			{
				if (Open.bIsInterface)
				{
					return;
				}
				ActiveOpenTypeParams.clear();
				for (const std::string& Param : Open.TypeParams)
				{
					ActiveOpenTypeParams.insert(Param);
				}
				if (UserTypes.find(Open.Name) == UserTypes.end())
				{
					FUserTypeInfo Info;
					Info.Name = Open.Name;
					Info.ModulePath = Open.ModulePath;
					Info.bExported = Open.bExported;
					Info.bFinal = Open.bFinal;
					Info.bIsAbstract = Open.bIsAbstract;
					Info.bHasPureVirtualDestructor = Open.bHasPureVirtualDestructor;
					Info.bIsClass = Open.bIsClass;
					Info.bIsInterface = Open.bIsInterface;
					Info.Fields = Open.Fields;
					Info.Methods = Open.Methods;
					Info.Constructors = Open.Constructors;
					Info.Destructor = Open.Destructor;
					Info.TypeParams = Open.TypeParams;
					Info.BaseTypeNames = Open.BaseTypeNames;
					Info.InterfaceTypeNames = Open.InterfaceTypeNames;
					UserTypes.emplace(Open.Name, std::move(Info));
				}
				const auto EmitTemplateHeader = [&]()
				{
					Out << "template <";
					for (std::size_t Index = 0; Index < Open.TypeParams.size(); ++Index)
					{
						if (Index > 0)
						{
							Out << ", ";
						}
						Out << "typename " << Open.TypeParams[Index];
					}
					Out << ">\n";
				};
				if (Open.Constructors->empty())
				{
					EmitTemplateHeader();
					EmitCtorInitSignature(Open.Name, UserTypes.at(Open.Name), {}, true);
					if (!Open.bIsAbstract)
					{
						EmitTemplateHeader();
						EmitCtorSignature(Open.Name, {}, {}, true);
					}
				}
				else
				{
					for (const FConstructorDecl& Ctor : *Open.Constructors)
					{
						if (Ctor.bDeleted)
						{
							continue;
						}
						std::vector<std::string> ParamTypes;
						for (const FFunctionParam& Param : Ctor.Parameters)
						{
							ParamTypes.push_back(Param.TypeName);
						}
						EmitTemplateHeader();
						EmitCtorInitSignature(
							Open.Name, UserTypes.at(Open.Name), Ctor.Parameters, true);
						if (!Open.bIsAbstract)
						{
							EmitTemplateHeader();
							EmitCtorSignature(
								Open.Name, ParamTypes, Ctor.Parameters, true);
						}
					}
				}
				ActiveOpenTypeParams.clear();
			}

			void EmitOpenGenericType(const FOpenGenericType& Open)
			{
				if (bFailed)
				{
					return;
				}
				ActiveOpenTypeParams.clear();
				for (const std::string& Param : Open.TypeParams)
				{
					ActiveOpenTypeParams.insert(Param);
				}
				Out << "template <";
				for (std::size_t Index = 0; Index < Open.TypeParams.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << "typename " << Open.TypeParams[Index];
				}
				ActiveOpenBounds = Open.Bounds;
				EmitEnableIfTemplateParam(Open.Bounds, false);
				Out << ">\n";

				FUserTypeInfo Info;
				Info.Name = Open.Name;
				Info.ModulePath = Open.ModulePath;
				Info.bExported = Open.bExported;
				Info.bFinal = Open.bFinal;
				Info.bIsAbstract = Open.bIsAbstract;
				Info.bHasPureVirtualDestructor = Open.bHasPureVirtualDestructor;
				Info.bIsClass = Open.bIsClass;
				Info.bIsInterface = Open.bIsInterface;
				Info.Fields = Open.Fields;
				Info.Methods = Open.Methods;
				Info.Constructors = Open.Constructors;
				Info.Destructor = Open.Destructor;
				Info.TypeParams = Open.TypeParams;
				Info.BaseTypeNames = Open.BaseTypeNames;
				Info.InterfaceTypeNames = Open.InterfaceTypeNames;
				if (UserTypes.find(Open.Name) == UserTypes.end())
				{
					UserTypes.emplace(Open.Name, Info);
				}
				EmitUserTypeStruct(Info);
				if (!Open.bIsInterface)
				{
					FOperandOutputScope OutputScope(Out);
					const auto EmitTemplateHeader = [&]()
					{
						Out << "template <";
						for (std::size_t Index = 0; Index < Open.TypeParams.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							Out << "typename " << Open.TypeParams[Index];
						}
						Out << ">\n";
					};
					if (Open.Constructors->empty())
					{
						EmitTemplateHeader();
						EmitCtor(Open.Name, Info, nullptr);
					}
					else
					{
						for (const FConstructorDecl& Ctor : *Open.Constructors)
						{
							if (Ctor.bDeleted)
							{
								continue;
							}
							EmitTemplateHeader();
							EmitCtor(Open.Name, Info, &Ctor);
						}
					}
					DeferredTypeDefinitions.push_back(Out.str());
				}
				ActiveOpenBounds.clear();
				ActiveOpenTypeParams.clear();
			}

			void EmitSpawnDispatchers()
			{
				// Workers: void name(channel<T> ch) -> VsSpawnByName_T(name, callerModule, ch).
				std::unordered_map<std::string, std::vector<const FFunctionDecl*>> ByElem;
				for (const FFunctionDecl* Function : ConcreteFunctions)
				{
					if (Function->ReturnType != "void" || Function->Parameters.size() != 1)
					{
						continue;
					}
					std::string Elem;
					if (!TryParseChannelType(Function->Parameters[0].TypeName, Elem))
					{
						continue;
					}
					ByElem[Elem].push_back(Function);
				}
				if (ByElem.empty())
				{
					return;
				}
				bNeedsThread = true;
				bNeedsChannel = true;
				bNeedsMemory = true;
				for (const auto& Entry : ByElem)
				{
					const std::string& Elem = Entry.first;
					const std::string MangledElem = SanitizeForMangle(Elem);
					Out << "static inline int VsSpawnByName_" << MangledElem
						<< "(const std::string& Name, const std::string& CallerModule, "
						<< "std::shared_ptr<vs_Channel<" << MapType(Elem)
						<< ">> Ch, bool AutoClose) {\n";
					for (const FFunctionDecl* Worker : Entry.second)
					{
						const std::string ModuleKey =
							Worker->ModulePath.empty() ? "local" : Worker->ModulePath;
						Out << "\tif (Name == \"" << EscapeCppString(Worker->Name) << "\" && (";
						if (Worker->bExported)
						{
							Out << "true";
						}
						else
						{
							Out << "CallerModule == \"" << EscapeCppString(ModuleKey) << "\"";
						}
						Out << ")) return AutoClose\n"
							<< "\t\t? VsSpawnResultWithChannelCloser("
							<< "[=]() { Ch->close(); }, [=]() { "
							<< EmitFunctionCppName(*Worker) << "(Ch); })\n"
							<< "\t\t: VsSpawnWithChannelCloser("
							<< "[=]() { Ch->close(); }, [=]() { "
							<< EmitFunctionCppName(*Worker) << "(Ch); });\n";
					}
					Out << "\treturn -1;\n"
						<< "}\n"
						<< "static inline int VsSpawnByNameOrFail_" << MangledElem
						<< "(const std::string& Name, const std::string& CallerModule, "
						<< "std::shared_ptr<vs_Channel<" << MapType(Elem)
						<< ">> Ch, bool AutoClose) {\n"
						<< "\tconst int Id = VsSpawnByName_" << MangledElem
						<< "(Name, CallerModule, Ch, AutoClose);\n"
						<< "\tif (Id < 0) VsScriptFail(\"spawn target not found\");\n"
						<< "\treturn Id;\n"
						<< "}\n"
						<< "static inline int VsSpawnByNameOrFail("
						<< "const std::string& Name, const std::string& CallerModule, "
						<< "std::shared_ptr<vs_Channel<" << MapType(Elem)
						<< ">> Ch, bool AutoClose) {\n"
						<< "\treturn VsSpawnByNameOrFail_" << MangledElem
						<< "(Name, CallerModule, Ch, AutoClose);\n"
						<< "}\n\n";
				}
			}

			void EmitStructInterfaceBoxes()
			{
				for (const std::string& Key : PendingStructBoxes)
				{
					const std::string::size_type Sep = Key.find('\n');
					if (Sep == std::string::npos)
					{
						continue;
					}
					const std::string StructName = Key.substr(0, Sep);
					const std::string IfaceName = Key.substr(Sep + 1);
					const auto StructIt = UserTypes.find(StructName);
					const auto IfaceIt = UserTypes.find(IfaceName);
					if (StructIt == UserTypes.end() || IfaceIt == UserTypes.end())
					{
						continue;
					}
					const std::string IfaceCpp = MapBaseCppType(IfaceName);
					const std::string StructCpp = MapBaseCppType(StructName);
					const std::string BoxName =
						SanitizeForMangle(StructCpp) + "_box_" + SanitizeForMangle(IfaceCpp);
					Out << "struct " << BoxName << " : public " << IfaceCpp << " {\n"
						<< "\t" << StructCpp << " value;\n"
						<< "\texplicit " << BoxName << "(" << StructCpp
						<< " v) : value(std::move(v)) {}\n";
					for (const FMethodDecl& Method : *IfaceIt->second.Methods)
					{
						Out << "\t" << MapType(Method.ReturnType) << ' '
							<< SanitizeSymbolName(Method.Name) << '(';
						for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							const FFunctionParam& Param = Method.Parameters[Index];
							Out << MapParamType(Param.TypeName, Param.bIsRef, Param.bIsConst)
								<< ' ' << Param.Name;
						}
						Out << ") override {\n\t\t";
						if (Method.ReturnType != "void")
						{
							Out << "return ";
						}
						Out << "value." << SanitizeSymbolName(Method.Name) << '(';
						for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							Out << Method.Parameters[Index].Name;
						}
						Out << ");\n\t}\n";
					}
					Out << "};\n\n";
				}
			}

			void NoteStructInterfaceBox(
				const std::string& TargetTypeName,
				const std::string& ValueTypeName)
			{
				const std::string Target =
					ResolveAliasChain(ApplyTypeSubst(TargetTypeName));
				const std::string Value =
					ResolveAliasChain(ApplyTypeSubst(ValueTypeName));
				const auto TargetIt = UserTypes.find(Target);
				const auto ValueIt = UserTypes.find(Value);
				if (TargetIt != UserTypes.end() && TargetIt->second.bIsInterface
					&& ValueIt != UserTypes.end() && !ValueIt->second.bIsClass
					&& !ValueIt->second.bIsInterface)
				{
					PendingStructBoxes.insert(Value + "\n" + Target);
				}
			}

			// Emit RHS coerced to interface/base (struct?I boxes by value copy).
			void EmitCoercedValue(
				const std::string& TargetTypeName,
				const FExpr& ValueExpr,
				const bool bAllowExplicitConstructor = false)
			{
				if (TryEmitSequencedOperand(
						ValueExpr,
						[&]
						{
							EmitCoercedValue(TargetTypeName, ValueExpr, bAllowExplicitConstructor);
						},
						true))
				{
					return;
				}

				const std::string Target = ResolveAliasChain(ApplyTypeSubst(TargetTypeName));
				TValueScopeRestore<const FExpr*> ValueScope(ValueExpression);
				ValueExpression = Runtime::IsReferenceTypeName(Target) || ValueExpr.Kind == EExprKind::Ref
					|| ReferenceExpression == &ValueExpr ? nullptr : &ValueExpr;
				std::string HandlePayload;
				if (ValueExpr.Kind == EExprKind::Call && OpenGenericTypes.find("handle") == OpenGenericTypes.end()
					&& TryParseHandleType(Target, HandlePayload))
				{
					const auto& Call = static_cast<const FCallExpr&>(ValueExpr);
					if (Call.Callee == "handle" && Call.TypeArgs.empty()
						&& !HasNonGenericHandleType())
					{
						FCallExpr TypedCall(Target, {});
						TypedCall.Location = Call.Location;
						TypedCall.BorrowArgumentsFrom(Call.GetArguments());
						EmitExpr(TypedCall);
						return;
					}
				}
				// Contextual fn typing: bare `fn(x)=>?` needs the target as emit hint.
				if (ValueExpr.Kind == EExprKind::Lambda && Runtime::IsFunctionTypeName(Target))
				{
					EmitExpr(ValueExpr, &Target);
					return;
				}
				const std::string ValueType = ResolveValueType(InferExprType(ValueExpr));
				std::vector<std::string> SourceAttributeArgs;
				if (bHasStandardAttribute &&
					TryParseSoftBuiltinType(ValueType, "attribute", SourceAttributeArgs) &&
					SourceAttributeArgs.size() == 1 &&
					Target == ResolveAliasChain(ApplyTypeSubst(SourceAttributeArgs[0])))
				{
					Out << '(';
					EmitExpr(ValueExpr);
					Out << ").get()";
					return;
				}
				std::vector<std::string> AttributeArgs;
				if (bHasStandardAttribute && TryParseSoftBuiltinType(Target, "attribute", AttributeArgs) &&
					AttributeArgs.size() == 1 && Target != ValueType)
				{
					Out << MapType(Target) << '(';
					EmitCoercedValue(AttributeArgs[0],ValueExpr);
					Out << ')';
					return;
				}
				if (ValueExpr.Kind == EExprKind::Identifier && ValueType.empty()
					&& Runtime::IsFunctionTypeName(Target))
				{
					std::vector<std::string> Parameters;
					std::string ReturnType;
					Runtime::ParseFunctionType(Target, Parameters, ReturnType);
					const auto& Name = static_cast<const FIdentifierExpr&>(ValueExpr).Name;
					const auto LookupNames = BuildFreeFunctionLookupNames(Name, &ValueExpr.Location);
					std::vector<std::string> MappedParameters;
					for (const auto& Parameter : Parameters)
					{
						MappedParameters.push_back(MapType(Parameter));
					}

					const std::string MappedReturn = MapType(ReturnType);
					const FFunctionDecl* Match = nullptr;
					for (const auto* Function : ConcreteFunctions)
					{
						if (Function->bDeleted || Function->Parameters.size() != Parameters.size() ||
							std::find(LookupNames.begin(), LookupNames.end(), Function->Name) ==
								LookupNames.end() ||
							!IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
						{
							continue;
						}

						TValueScopeRestore<std::string> ModuleScope(ActiveModulePath);
						ActiveModulePath = Function->ModulePath;
						bool Compatible = MapType(Function->ReturnType) == MappedReturn;
						for (std::size_t I = 0; Compatible && I < Parameters.size(); ++I)
						{
							const auto& P = Function->Parameters[I];
							Compatible = !P.bIsVariadic && MapParamType(P.TypeName, P.bIsRef, P.bIsConst) == MappedParameters[I];
						}
						if (!Compatible)
						{
							continue;
						}

						if (Match)
						{
							Fail("AOT: ambiguous function value: " + Name);
							return;
						}
						Match = Function;
					}
					if (!Match)
					{
						Fail("AOT: no matching concrete function value: " + Name);
						return;
					}
					Out << MapType(Target) << "(&" << EmitFunctionCppName(*Match) << ')';
					return;
				}
				if (Target.empty() || ValueType.empty() || Target == ValueType)
				{
					EmitExpr(ValueExpr);
					return;
				}
				if (ValueType != "nullptr" && OpenGenericTypes.find("handle") == OpenGenericTypes.end()
					&& TryParseHandleType(Target, HandlePayload))
				{
					bNeedsMemory = true;
					bNeedsHandle = true;
					Out << "std::make_shared<vs_SoftHandle<" << MapType(HandlePayload) << ">>(";
					EmitCoercedValue(HandlePayload, ValueExpr);
					Out << ')';
					return;
				}
				std::vector<std::string> ReadonlyArgs;
				if (TryParseSoftBuiltinType(Target, "readonly", ReadonlyArgs)
					&& ReadonlyArgs.size() == 1
					&& ResolveAliasChain(ApplyTypeSubst(ReadonlyArgs[0])) == ValueType)
				{
					Out << "vs_Readonly<" << MapType(ReadonlyArgs[0]) << ">(";
					EmitExpr(ValueExpr);
					Out << ')';
					return;
				}
				const auto IsNumericPrimitive = [](const std::string& Type)
				{
					return Type == "char" || Type == "int8"
						|| Type == "unsigned char" || Type == "uint8"
						|| Type == "short" || Type == "int16"
						|| Type == "unsigned short" || Type == "uint16"
						|| Type == "int" || Type == "int32"
						|| Type == "unsigned" || Type == "unsigned int"
						|| Type == "uint32" || Type == "long" || Type == "int64"
						|| Type == "unsigned long" || Type == "uint64"
						|| Type == "float" || Type == "double";
				};
				if (IsNumericPrimitive(Target)
					&& (IsNumericPrimitive(ValueType) || ValueType == "bool"))
				{
					Out << "VsCheckedNumericCast<" << MapType(Target) << ">(";
					EmitExpr(ValueExpr);
					Out << ')';
					return;
				}
				const auto TargetCtorIt = UserTypes.find(Target);
				if (TargetCtorIt != UserTypes.end()
					&& !TargetCtorIt->second.bIsInterface)
				{
					const FConstructorDecl* const Constructor = ResolveCtor(
						TargetCtorIt->second,
						{ValueType},
						bAllowExplicitConstructor,
						false);
					if (Constructor
						&& Constructor->Parameters.size() == 1
						&& !Constructor->Parameters.front().bIsRef
						&& !Constructor->Parameters.front().bIsVariadic)
					{
						const std::string ParameterType = SubstForUserType(
							TargetCtorIt->second, Constructor->Parameters.front().TypeName);
						Out << MangleCtor(Target) << '(';
						EmitCoercedValue(ParameterType, ValueExpr);
						Out << ')';
						return;
					}
				}
				NoteStructInterfaceBox(Target, ValueType);
				const auto TargetIt = UserTypes.find(Target);
				const auto ValueIt = UserTypes.find(ValueType);
				if (TargetIt != UserTypes.end() && TargetIt->second.bIsInterface
					&& ValueIt != UserTypes.end() && !ValueIt->second.bIsClass
					&& !ValueIt->second.bIsInterface)
				{
					bNeedsMemory = true;
					const std::string IfaceCpp = MapBaseCppType(Target);
					const std::string StructCpp = MapBaseCppType(ValueType);
					const std::string BoxName =
						SanitizeForMangle(StructCpp) + "_box_" + SanitizeForMangle(IfaceCpp);
					Out << "std::shared_ptr<" << IfaceCpp << ">(std::make_shared<"
						<< BoxName << ">(";
					EmitExpr(ValueExpr);
					Out << "))";
					return;
				}
				if (TargetIt != UserTypes.end())
				{
					const bool bNullReference = ValueType == "nullptr"
						&& (TargetIt->second.bIsClass || TargetIt->second.bIsInterface);
					const bool bSafeUserTypeConversion = ValueIt != UserTypes.end()
						&& IsAotSubtypeOf(ValueType, Target);
					if (!bNullReference && !bSafeUserTypeConversion)
					{
						Fail("AOT: cannot convert from '" + ValueType + "' to '" + Target + "'");
						Out << MapType(Target) << "{}";
						return;
					}
				}
				// Class/interface -> interface/base: implicit shared_ptr conversion.
				EmitExpr(ValueExpr);
			}

			void EmitMethodSignature(
				const std::string& TypeName,
				const FMethodDecl& Method,
				const bool bPrototype)
			{
				if (Method.bDeleted)
				{
					return;
				}
				const auto TypeIt = UserTypes.find(TypeName);
				if (TypeIt != UserTypes.end()
					&& (TypeNeedsOopLayout(TypeIt->second) || TypeIt->second.bIsInterface))
				{
					return; // member methods already emitted inside the struct
				}
				if (TypeIt != UserTypes.end())
				{
					PushTypeSubstForUserType(TypeIt->second);
				}
				// Classes are RC (shared_ptr): pass self by value so mutation flows
				// through the shared pointee and temporaries (e.g. operator receivers
				// like `Vec(3)`) bind. Structs need an lvalue receiver; const methods
				// preserve its readonly qualification.
				const bool bByValueSelf =
					TypeIt != UserTypes.end() && TypeIt->second.bIsClass;
				Out << MapType(Method.ReturnType) << ' '
					<< MangleMethod(TypeName, Method.Name) << '('
					<< (!bByValueSelf && Method.bIsConst ? "const " : "")
					<< MapType(TypeName) << (bByValueSelf ? " " : "& ") << SelfReceiverName;
				for (const FFunctionParam& Param : Method.Parameters)
				{
					Out << ", " << MapParamTypeAllowVariadic(Param) << ' ' << Param.Name;
				}
				Out << ')';
				TypeSubst.clear();
				if (bPrototype)
				{
					Out << ";\n";
				}
			}

			void EmitMethod(const std::string& TypeName, const FMethodDecl& Method)
			{
				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
				RetainedReferenceAliases.clear();
				// Static methods are always emitted inside the struct as C++ `static`
				// members (see EmitUserTypeStruct); never as free helpers.
				if (Method.bIsStatic || Method.bDeleted)
				{
					return;
				}
				const auto TypeIt = UserTypes.find(TypeName);
				if (TypeIt != UserTypes.end()
					&& (TypeNeedsOopLayout(TypeIt->second) || TypeIt->second.bIsInterface))
				{
					return;
				}
				LocalTypes.clear();
				LocalConstness.clear();
				TypeSubst.clear();
				if (TypeIt != UserTypes.end())
				{
					PushTypeSubstForUserType(TypeIt->second);
				}
				bInSelfContext = true;
				bSelfIsConst = Method.bIsConst;
				SelfTypeName = TypeName;
				ActiveModulePath =
					TypeIt != UserTypes.end() ? TypeIt->second.ModulePath : EntryModulePath;
				ActiveNamespace = NamespaceOfSymbol(TypeName);
				ActiveReturnType = Method.ReturnType;
				for (const FFunctionParam& Param : Method.Parameters)
				{
					LocalTypes[Param.Name] = Param.bIsVariadic
						? ("array<" + Param.TypeName + ">")
						: Param.TypeName;
					LocalConstness[Param.Name] = Param.bIsConst;
				}
				EmitMethodSignature(TypeName, Method, false);
				Out << '\n';
				if (!Method.Body)
				{
					Fail("AOT: method body required");
					return;
				}
				if (!Method.Body->DefinitionModulePath.empty())
				{
					ActiveModulePath = Method.Body->DefinitionModulePath;
					ActiveNamespace = Method.Body->DefinitionNamespace;
				}
				EmitStmt(*Method.Body, 0);
				Out << '\n';
				bInSelfContext = false;
				bSelfIsConst = false;
				SelfTypeName.clear();
				TypeSubst.clear();
			}

			void EmitCtorSignature(
				const std::string& TypeName,
				const std::vector<std::string>& /*ParamTypes*/,
				const std::vector<FFunctionParam>& Parameters,
				const bool bPrototype)
			{
				const auto TypeIt = UserTypes.find(TypeName);
				if (TypeIt != UserTypes.end())
				{
					PushTypeSubstForUserType(TypeIt->second);
				}
				Out << MapType(TypeName) << ' ' << MangleCtor(TypeName) << '(';
				for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamType(
						Parameters[Index].TypeName,
						Parameters[Index].bIsRef,
						Parameters[Index].bIsConst)
							<< ' ' << Parameters[Index].Name;
				}
				Out << ')';
				TypeSubst.clear();
				if (bPrototype)
				{
					Out << ";\n";
				}
			}

			void EmitCtorInitSignature(
				const std::string& TypeName,
				const FUserTypeInfo& Type,
				const std::vector<FFunctionParam>& Parameters,
				const bool bPrototype)
			{
				PushTypeSubstForUserType(Type);
				// Constructor initialization never needs ownership.  Using the concrete
				// object reference lets factories initialize `*shared_ptr` and also lets
				// proven non-escaping locals initialize automatic storage without an
				// allocation/control block.
				Out << "void " << MangleCtorInit(TypeName) << '('
					<< MapBaseCppType(TypeName) << "& " << SelfReceiverName;
				for (const FFunctionParam& Param : Parameters)
				{
					Out << ", " << MapParamType(
						Param.TypeName, Param.bIsRef, Param.bIsConst) << ' ' << Param.Name;
				}
				Out << ", const void* " << ConstructionLocalName("___vs_complete_receiver", Parameters) << ')';
				TypeSubst.clear();
				if (bPrototype)
				{
					Out << ";\n";
				}
			}

			void EmitCtor(
				const std::string& TypeName,
				const FUserTypeInfo& Type,
				const FConstructorDecl* Ctor)
			{
				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
				RetainedReferenceAliases.clear();
				std::vector<std::string> ParamTypes;
				const std::vector<FFunctionParam>* Parameters = nullptr;
				static const std::vector<FFunctionParam> EmptyParams;
				if (Ctor)
				{
					Parameters = &Ctor->Parameters;
					for (const FFunctionParam& Param : Ctor->Parameters)
					{
						ParamTypes.push_back(Param.TypeName);
					}
				}
				else
				{
					Parameters = &EmptyParams;
				}

				LocalTypes.clear();
				LocalConstness.clear();
				PushTypeSubstForUserType(Type);
				bInSelfContext = true;
				bSelfIsConst = false;
				bCtorInitDirectSelf = true;
				SelfTypeName = TypeName;
				ActiveModulePath = Type.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(TypeName);
				for (const FFunctionParam& Param : *Parameters)
				{
					LocalTypes[Param.Name] = Param.TypeName;
					LocalConstness[Param.Name] = Param.bIsConst;
				}

				EmitCtorInitSignature(TypeName, Type, *Parameters, false);
				Out << " {\n";
				PushTypeSubstForUserType(Type);
				bNeedsConstructionPhase = true;
				const auto CompleteReceiver = ConstructionLocalName("___vs_complete_receiver", *Parameters);
				const auto Hierarchy = ConstructionLocalName("___vs_construction_types", *Parameters);
				const auto Scope = ConstructionLocalName("___vs_construction_scope", *Parameters);
				const auto Ancestors = ConstructionAncestors(Type);
				Out << "\tstatic const std::type_info* const " << Hierarchy << "[] = { &typeid("
					<< MapBaseCppType(TypeName) << ')';
				for (const auto& Ancestor : Ancestors)
				{
					Out << ", &typeid(" << MapBaseCppType(Ancestor.Name) << ')';
				}
				Out << " };\n\tVsConstructionScope " << Scope << '(' << CompleteReceiver << ", " << Hierarchy
					<< ", sizeof(" << Hierarchy << ") / sizeof(*" << Hierarchy << "));\n";
				if (Type.Destructor && Type.Destructor->Body)
				{
					// This guard also covers early returns and leaves the owner incomplete
					// if a later constructor local throws while being destroyed.
					Out << "\tVsConstructionCompletion " << ConstructionLocalName("___vs_completion", *Parameters)
						<< '(' << SelfReceiverName << '.' << ConstructionCompleteField(Type) << ".Complete);\n";
				}
				const std::string SelfAccessor = ".";
				const auto DeclaredBaseTypeName = [&](const std::string& InitName)
				{
					const std::string Owner = CtorInitOwnerName(InitName);
					for (const std::string& BaseName : Type.BaseTypeNames)
					{
						if (CtorInitOwnerName(BaseName) == Owner)
						{
							return BaseName;
						}
					}
					return InitName;
				};
				if (Type.bIsClass)
				{
					bNeedsMemory = true;
					// Initialize base subobjects in place. A factory cannot instantiate an
					// abstract base, while its constructor body must still run for a concrete
					// derived object.
					if (Ctor && !Ctor->BaseInits.empty())
					{
						for (const FBaseCtorInit& BaseInit : Ctor->BaseInits)
						{
							const std::string BaseTypeName =
								DeclaredBaseTypeName(BaseInit.BaseName);
							if (!IsClassBaseType(BaseTypeName))
							{
								continue;
							}
							Out << "\t" << MangleCtorInit(CtorInitOwnerName(BaseTypeName))
								<< "(static_cast<" << MapBaseCppType(BaseTypeName)
								<< "&>(" << SelfReceiverName << ')';
							for (std::size_t Index = 0; Index < BaseInit.Args.size(); ++Index)
							{
								Out << ", ";
								if (BaseInit.Args[Index])
								{
									EmitExpr(*BaseInit.Args[Index]);
								}
							}
							Out << ", " << CompleteReceiver << ");\n";
						}
					}
					// C++ default construction does not execute VeroScript field initializers
					// or constructor bodies. Initialize every direct script base that was not
					// named in the constructor initializer list through its generated default
					// initializer. That initializer recursively initializes its own ancestors.
					for (const std::string& BaseName : Type.BaseTypeNames)
					{
						bool bExplicitlyInitialized = false;
						if (Ctor)
						{
							for (const FBaseCtorInit& BaseInit : Ctor->BaseInits)
							{
								if (CtorInitOwnerName(BaseInit.BaseName)
									== CtorInitOwnerName(BaseName))
								{
									bExplicitlyInitialized = true;
									break;
								}
							}
						}
						if (bExplicitlyInitialized || !IsClassBaseType(BaseName))
						{
							continue;
						}
						Out << "\t" << MangleCtorInit(CtorInitOwnerName(BaseName))
							<< "(static_cast<" << MapBaseCppType(BaseName)
							<< "&>(" << SelfReceiverName << "), " << CompleteReceiver << ");\n";
					}
				}
				else
				{
					if (Ctor && !Ctor->BaseInits.empty())
					{
						for (const FBaseCtorInit& BaseInit : Ctor->BaseInits)
						{
							const std::string BaseTypeName =
								DeclaredBaseTypeName(BaseInit.BaseName);
							if (IsClassBaseType(BaseTypeName))
							{
								continue;
							}
							Out << "\t" << MangleCtorInit(CtorInitOwnerName(BaseTypeName))
								<< "(static_cast<" << MapBaseCppType(BaseTypeName)
								<< "&>(" << SelfReceiverName << ')';
							for (std::size_t Index = 0; Index < BaseInit.Args.size(); ++Index)
							{
								Out << ", ";
								if (BaseInit.Args[Index])
								{
									EmitExpr(*BaseInit.Args[Index]);
								}
							}
							Out << ", " << CompleteReceiver << ");\n";
						}
					}
					for (const std::string& BaseName : Type.BaseTypeNames)
					{
						const bool bExplicit = Ctor && std::any_of(
							Ctor->BaseInits.begin(), Ctor->BaseInits.end(),
							[&](const FBaseCtorInit& Init)
							{
								return CtorInitOwnerName(Init.BaseName)
										== CtorInitOwnerName(BaseName);
							});
						if (bExplicit || IsClassBaseType(BaseName))
						{
							continue;
						}
						Out << "\t" << MangleCtorInit(CtorInitOwnerName(BaseName))
							<< "(static_cast<"
							<< MapBaseCppType(BaseName) << "&>(" << SelfReceiverName << "), " << CompleteReceiver << ");\n";
					}
				}
				if (Type.Fields)
				{
					for (const FFieldDecl& Field : *Type.Fields)
					{
						// Static data members are not per-instance; their initializer is
						// emitted once at file scope, never in the constructor.
						if (Field.bIsStatic)
						{
							continue;
						}
						const FFieldCtorInit* FieldInit = nullptr;
						if (Ctor)
						{
							const auto InitIt = std::find_if(
								Ctor->FieldInits.begin(),
								Ctor->FieldInits.end(),
								[&](const FFieldCtorInit& Init)
								{
									return Init.FieldName == Field.Name;
								});
							if (InitIt != Ctor->FieldInits.end())
							{
								FieldInit = &*InitIt;
							}
						}
						if (FieldInit)
						{
							FCallExpr DirectConstruction(ApplyTypeSubst(Field.TypeName), {});
							DirectConstruction.BorrowArgumentsFrom(FieldInit->Args);
							DirectConstruction.Location = FieldInit->Location;
							if (TryEmitStructFieldConstructor(Field, DirectConstruction))
							{
								continue;
							}
							if (FieldInit->Args.size() == 1
								&& ResolveValueType(InferExprType(*FieldInit->Args.front()))
									== ResolveAliasChain(ApplyTypeSubst(Field.TypeName))
								&& TryEmitStructFieldValue(Field, *FieldInit->Args.front()))
							{
								continue;
							}
							Out << "\t" << SelfReceiverName << SelfAccessor << Field.Name << " = ";
							const std::string ResolvedFieldType =
								ResolveAliasChain(ApplyTypeSubst(Field.TypeName));
							const bool bOpenTypeParameter =
								ActiveOpenTypeParams.find(ResolvedFieldType)
									!= ActiveOpenTypeParams.end();
							const bool bScalarField = ResolvedFieldType == "bool"
								|| ResolvedFieldType == "string"
								|| ResolvedFieldType == "wstring"
								|| ResolvedFieldType == "char"
								|| ResolvedFieldType == "int8"
								|| ResolvedFieldType == "uint8"
								|| ResolvedFieldType == "short"
								|| ResolvedFieldType == "int16"
								|| ResolvedFieldType == "uint16"
								|| ResolvedFieldType == "int"
								|| ResolvedFieldType == "int32"
								|| ResolvedFieldType == "unsigned"
								|| ResolvedFieldType == "unsigned int"
								|| ResolvedFieldType == "uint32"
								|| ResolvedFieldType == "long"
								|| ResolvedFieldType == "int64"
								|| ResolvedFieldType == "unsigned long"
								|| ResolvedFieldType == "uint64"
								|| ResolvedFieldType == "float"
								|| ResolvedFieldType == "double"
								|| EnumUnderlyingTypeNames.find(ResolvedFieldType)
									!= EnumUnderlyingTypeNames.end();
							if (bOpenTypeParameter)
							{
								const std::string ArgumentType = FieldInit->Args.size() == 1
									? ResolveAliasChain(ApplyTypeSubst(
										InferExprType(*FieldInit->Args.front())))
									: std::string{};
								if (ArgumentType != ResolvedFieldType)
								{
									Fail(
										"AOT: constructor field initializer for open type parameter '"
											+ ResolvedFieldType
											+ "' requires exactly one argument of the same type");
									Out << MapType(ResolvedFieldType) << "{}";
								}
								else
								{
									EmitCoercedValue(
										Field.TypeName,
										*FieldInit->Args.front(),
										true);
								}
							}
							else if (bScalarField && FieldInit->Args.empty())
							{
								Out << MapType(ResolvedFieldType) << "{}";
							}
							else if (bScalarField && FieldInit->Args.size() == 1)
							{
								EmitCoercedValue(
									Field.TypeName,
									*FieldInit->Args.front(),
									true);
							}
							else
							{
								FCallExpr Construction(ApplyTypeSubst(Field.TypeName), {});
								Construction.BorrowArgumentsFrom(FieldInit->Args);
								Construction.Location = FieldInit->Location;
								EmitCoercedValue(Field.TypeName, Construction);
							}
							Out << ";\n";
						}
						else if (Field.DefaultValue)
						{
							TValueScopeRestore<decltype(LocalTypes)> DeclarationLocals(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> DeclarationConsts(LocalConstness);
							TValueScopeRestore<bool> DeclarationSelf(bInSelfContext);
							LocalTypes.clear();
							LocalConstness.clear();
							bInSelfContext = false;
							if (Field.DefaultValue->Kind == EExprKind::Call
								&& TryEmitStructFieldConstructor(Field, static_cast<const FCallExpr&>(*Field.DefaultValue)))
							{
								continue;
							}
							if (TryEmitStructFieldValue(Field, *Field.DefaultValue))
							{
								continue;
							}
							Out << "\t" << SelfReceiverName << SelfAccessor << Field.Name << " = ";
							EmitCoercedValue(Field.TypeName, *Field.DefaultValue);
							Out << ";\n";
						}
						else
						{
							Out << "\tVsInitializeDefaultStorage(" << SelfReceiverName << SelfAccessor << Field.Name
								<< ");\n";
						}
					}
				}
				if (Ctor && Ctor->Body)
				{
					// Keep script locals separate from the construction guards while
					// still destroying them before the owner is marked complete.
					Out << "\t{\n";
					for (const std::unique_ptr<FStmt>& Child : Ctor->Body->Statements)
					{
						if (Child)
						{
							EmitStmt(*Child, 2);
						}
					}
					Out << "\t}\n";
				}
				Out << "}\n\n";
				if (!Type.bIsAbstract)
				{
					if (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty()
						&& !ActiveOpenTypeParams.empty())
					{
						Out << "template <";
						for (std::size_t Index = 0; Index < Type.TypeParams.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							Out << "typename " << Type.TypeParams[Index];
						}
						Out << ">\n";
					}
					EmitCtorSignature(TypeName, ParamTypes, *Parameters, false);
					if (Type.bIsClass)
					{
						Out << " {\n\tauto " << SelfReceiverName << " = std::make_shared<";
						PushTypeSubstForUserType(Type);
						if (!Type.OpenGenericName.empty() && !Type.MonoTypeArgs.empty())
						{
							Out << UserCppType(Type.OpenGenericName) << '<';
							for (std::size_t Index = 0; Index < Type.MonoTypeArgs.size(); ++Index)
							{
								if (Index > 0)
								{
									Out << ", ";
								}
								Out << MapType(Type.MonoTypeArgs[Index]);
							}
							Out << '>';
						}
						else
						{
							Out << UserCppType(TypeName);
							if (!Type.TypeParams.empty() && Type.MonoTypeArgs.empty()
								&& !ActiveOpenTypeParams.empty())
							{
								Out << '<';
								for (std::size_t Index = 0; Index < Type.TypeParams.size(); ++Index)
								{
									if (Index > 0)
									{
										Out << ", ";
									}
									Out << Type.TypeParams[Index];
								}
								Out << '>';
							}
						}
						Out << ">();\n\ttry {\n";
					}
					else
					{
						Out << "\n{\n";
						if (ReferenceOwnerHasCleanup(Type))
						{
							Out << "\tstatic_assert(std::is_nothrow_move_constructible_v<" << MapType(TypeName)
								<< ">, \"AOT: a struct with script cleanup requires nonthrowing native relocation\");\n";
						}
						Out << "\treturn " << MapType(TypeName)
							<< "(VsConstructionValueTag{}, [&](" << MapType(TypeName) << "& " << SelfReceiverName << ")\n\t{\n";
					}
					Out << (Type.bIsClass ? "\t\t" : "\t\t") << MangleCtorInit(TypeName)
						<< (Type.bIsClass ? "(*" : "(") << SelfReceiverName;
					for (const FFunctionParam& Param : *Parameters)
					{
						Out << ", " << Param.Name;
					}
					Out << ", VsConstructionAddress(" << (Type.bIsClass ? "*" : "") << SelfReceiverName << "));\n";
					if (Type.bIsClass)
					{
						// Locals have unwound before this handler. An escaped receiver
						// still owns partial subobjects, which requires managed cleanup.
						// Explicit exits and existing fatal errors cannot resume a script catch.
						Out << "\t}\n\tcatch (const VsExitException&)\n\t{\n\t\tthrow;\n\t}\n\tcatch (...)\n\t{\n"
							<< "\t\tif (" << SelfReceiverName << ".use_count() > 1) VsScriptFail(\"AOT: failed class construction has a retained receiver; use the VM or Core AOT backend\");\n"
							<< "\t\tthrow;\n\t}\n\treturn " << SelfReceiverName << ";\n}\n\n";
					}
					else
					{
						Out << "\t});\n}\n\n";
					}
				}
				bInSelfContext = false;
				bSelfIsConst = false;
				bCtorInitDirectSelf = false;
				SelfTypeName.clear();
				TypeSubst.clear();
			}
