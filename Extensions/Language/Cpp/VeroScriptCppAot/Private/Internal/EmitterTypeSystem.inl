// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			void Fail(std::string Message) const
			{
				if (!bFailed)
				{
					Error = std::move(Message);
					bFailed = true;
				}
			}

			static bool TypeNeedsOopLayout(const FUserTypeInfo& Type)
			{
				if (Type.bIsInterface || Type.bIsClass || (Type.Destructor && Type.Destructor->Body))
				{
					return true;
				}
				// Open generics emit as C++ templates with member methods (not free mono helpers).
				if (!Type.TypeParams.empty() && Type.Methods && !Type.Methods->empty()
					&& Type.MonoTypeArgs.empty())
				{
					return true;
				}
				if (!Type.BaseTypeNames.empty() || !Type.InterfaceTypeNames.empty())
				{
					return true;
				}
				if (Type.Methods)
				{
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (Method.bVirtual || Method.bOverride)
						{
							return true;
						}
						// Static methods alone do not need a vtable / virtual dtor —
						// they emit as plain C++ `static` members on a value struct.
					}
				}
				return false;
			}

			bool IsCompilerAbstractType(
				const std::string& Name,
				const std::string& ModulePath,
				const bool bExported) const
			{
				if (AbstractTypeNames.find(Name) != AbstractTypeNames.end())
				{
					return true;
				}
				if (bExported || ModulePath.empty())
				{
					return false;
				}
				return AbstractTypeNames.find(ModulePath + '\x1F' + Name)
					!= AbstractTypeNames.end();
			}

			bool IsInterfaceTypeName(const std::string& TypeName) const
			{
				const auto It = UserTypes.find(TypeName);
				return It != UserTypes.end() && It->second.bIsInterface;
			}

			bool TypeImplementsInterface(
				const FUserTypeInfo& Type,
				const std::string& InterfaceName) const
			{
				for (const std::string& Name : Type.InterfaceTypeNames)
				{
					if (Name == InterfaceName)
					{
						return true;
					}
				}
				for (const std::string& Base : Type.BaseTypeNames)
				{
					const auto BaseIt = UserTypes.find(Base);
					if (BaseIt != UserTypes.end()
						&& TypeImplementsInterface(BaseIt->second, InterfaceName))
					{
						return true;
					}
				}
				return false;
			}

			static std::string MonoTypeKey(
				const std::string& OpenName,
				const std::vector<std::string>& TypeArgs)
			{
				std::string Key = OpenName;
				for (const std::string& Arg : TypeArgs)
				{
					Key += '\n';
					Key += Arg;
				}
				return Key;
			}

			std::string MonoCppTypeName(
				const std::string& OpenName,
				const std::vector<std::string>& TypeArgs) const
			{
				return OpenName + "_g_" + JoinMangledTypes(TypeArgs);
			}

			static std::string EscapeCppString(const std::string& Value)
			{
				std::string Escaped;
				Escaped.reserve(Value.size() + 8);
				for (const unsigned char Ch : Value)
				{
					switch (Ch)
					{
					case '\\':
						{
							Escaped += "\\\\";
							break;
						}
					case '"':
						{
							Escaped += "\\\"";
							break;
						}
					case '\n':
						{
							Escaped += "\\n";
							break;
						}
					case '\r':
						{
							Escaped += "\\r";
							break;
						}
					case '\t':
						{
							Escaped += "\\t";
							break;
						}
					default:
						{
							if (Ch < 0x20 || Ch >= 0x7F)
							{
								char Buf[16];
								// Keep generated translation units source-encoding neutral
								// and terminate \x before a following hexadecimal digit.
								std::snprintf(Buf, sizeof(Buf), "\\x%02X\"\"", Ch);
								Escaped += Buf;
							}
							else
							{
								Escaped.push_back(static_cast<char>(Ch));
							}
							break;
						}
					}
				}
				return Escaped;
			}

			static std::string SanitizeForMangle(const std::string& Text)
			{
				std::string Out;
				Out.reserve(Text.size());
				bool bLastUnderscore = false;
				for (const unsigned char Ch : Text)
				{
					if (std::isalnum(Ch))
					{
						Out.push_back(static_cast<char>(Ch));
						bLastUnderscore = false;
					}
					else if (!bLastUnderscore)
					{
						Out.push_back('_');
						bLastUnderscore = true;
					}
				}
				while (!Out.empty() && Out.back() == '_')
				{
					Out.pop_back();
				}
				return Out.empty() ? std::string("x") : Out;
			}

			// Keeps `operator+=` / `operator==` distinct (plain Sanitize collapses both to `operator`).
			static std::string SanitizeSymbolName(const std::string& Text)
			{
				constexpr const char kOpPrefix[] = "operator";
				constexpr std::size_t kOpPrefixLen = sizeof(kOpPrefix) - 1;
				if (Text.size() > kOpPrefixLen
					&& Text.compare(0, kOpPrefixLen, kOpPrefix) == 0
					&& !std::isalnum(static_cast<unsigned char>(Text[kOpPrefixLen])))
				{
					return std::string("operator_") + EncodeModuleSuffix(Text.substr(kOpPrefixLen));
				}
				return SanitizeForMangle(Text);
			}

			// Avoids path collisions like a/b.vs vs a_b.vs under '_' collapsing.
			static std::string EncodeModuleSuffix(const std::string& Text)
			{
				std::string Out;
				Out.reserve(Text.size() * 2);
				for (const unsigned char Ch : Text)
				{
					if (std::isalnum(Ch))
					{
						Out.push_back(static_cast<char>(Ch));
					}
					else
					{
						char Buf[8];
						std::snprintf(Buf, sizeof(Buf), "_%02X", Ch);
						Out += Buf;
					}
				}
				return Out.empty() ? std::string("x") : Out;
			}

			static std::string JoinMangledTypes(const std::vector<std::string>& Types)
			{
				std::string Joined;
				for (std::size_t Index = 0; Index < Types.size(); ++Index)
				{
					if (Index > 0)
					{
						Joined += '_';
					}
					Joined += SanitizeForMangle(Types[Index]);
				}
				return Joined.empty() ? std::string("void") : Joined;
			}

			static bool IsStringType(const std::string& TypeName)
			{
				return TypeName == "string";
			}

			static bool IsWStringType(const std::string& TypeName)
			{
				return TypeName == "wstring";
			}

			// Splits "A,B<C,D>,E" (already stripped of the outer '<'/'>') on top-level commas.
			static void SplitTopLevelTypeArgs(const std::string& Inner, std::vector<std::string>& OutArgs)
			{
				OutArgs.clear();
				if (Inner.find_first_not_of(" \t\r\n") == std::string::npos) return;
				int Depth = 0;
				int ParenthesisDepth = 0;
				std::string Current;
				for (std::size_t Index = 0; Index < Inner.size(); ++Index)
				{
					const char Ch = Inner[Index];
					if (Ch == '<')
					{
						++Depth;
						Current.push_back(Ch);
					}
					else if (Ch == '>'
						&& (Index == 0 || Inner[Index - 1] != '-'))
					{
						--Depth;
						Current.push_back(Ch);
					}
					else if (Ch == '(')
					{
						++ParenthesisDepth;
						Current.push_back(Ch);
					}
					else if (Ch == ')')
					{
						if (ParenthesisDepth > 0)
						{
							--ParenthesisDepth;
						}
						Current.push_back(Ch);
					}
					else if (Ch == ',' && Depth == 0 && ParenthesisDepth == 0)
					{
						OutArgs.push_back(Current);
						Current.clear();
					}
					else
					{
						Current.push_back(Ch);
					}
				}
				OutArgs.push_back(Current);
				for (std::string& Arg : OutArgs)
				{
					while (!Arg.empty() && Arg.front() == ' ')
					{
						Arg.erase(Arg.begin());
					}
					while (!Arg.empty() && Arg.back() == ' ')
					{
						Arg.pop_back();
					}
				}
			}

			// Matches "BaseName<Arg1,Arg2,...>" (soft builtins: handle<T>, delegate<R,...>, ...).
			static bool TryParseTypeApplication(
				const std::string& TypeName,
				const std::string& BaseName,
				std::vector<std::string>& OutArgs)
			{
				if (TypeName.size() <= BaseName.size() + 1)
				{
					return false;
				}
				if (TypeName.compare(0, BaseName.size(), BaseName) != 0)
				{
					return false;
				}
				if (TypeName[BaseName.size()] != '<' || TypeName.back() != '>')
				{
					return false;
				}
				const std::string Inner = TypeName.substr(
					BaseName.size() + 1,
					TypeName.size() - BaseName.size() - 2);
				SplitTopLevelTypeArgs(Inner, OutArgs);
				return true;
			}

			static bool TryParseHandleType(const std::string& TypeName, std::string& OutInner)
			{
				std::vector<std::string> Args;
				if (TryParseTypeApplication(TypeName, "handle", Args) && Args.size() == 1)
				{
					OutInner = Args[0];
					return true;
				}
				return false;
			}

			static bool TryParseChannelType(
				const std::string& TypeName,
				std::string& OutInner)
			{
				if (Runtime::ParseChannelElementType(TypeName, OutInner))
				{
					return true;
				}
				std::vector<std::string> Args;
				if (TryParseTypeApplication(TypeName, "channel", Args)
					&& Args.size() == 1)
				{
					OutInner = Args[0];
					return true;
				}
				return false;
			}

			bool HasUserPairTemplate() const
			{
				const auto Found = OpenGenericTypes.find("pair");
				return Found != OpenGenericTypes.end() && Found->second.TypeParams.size() == 2;
			}

			bool TryParseSoftBuiltinType(
				const std::string& TypeName,
				const std::string_view BuiltinName,
				std::vector<std::string>& OutArgs) const
			{
				return OpenGenericTypes.find(std::string(BuiltinName))
						== OpenGenericTypes.end()
					&& TryParseTypeApplication(
						TypeName, std::string(BuiltinName), OutArgs);
			}

			bool IsRuntimeSourceMetadataType(const std::string& TypeName) const
			{
				return UserTypes.find(TypeName) == UserTypes.end()
					&& (TypeName == "source_node_info" || TypeName == "annotation_info"
						|| TypeName == "annotation_argument_info" || TypeName == "comment_info"
						|| TypeName == "annotation_definition_info" || TypeName == "module_info");
			}

			bool TypeMentionsActiveOpenParam(const std::string_view TypeName) const
			{
				for (const std::string& Param : ActiveOpenTypeParams)
				{
					std::size_t Pos = 0;
					while ((Pos = TypeName.find(Param, Pos)) != std::string_view::npos)
					{
						const bool bStartOk = Pos == 0
							|| (!std::isalnum(
									static_cast<unsigned char>(TypeName[Pos - 1]))
								&& TypeName[Pos - 1] != '_');
						const std::size_t End = Pos + Param.size();
						const bool bEndOk = End >= TypeName.size()
							|| (!std::isalnum(
									static_cast<unsigned char>(TypeName[End]))
								&& TypeName[End] != '_');
						if (bStartOk && bEndOk)
						{
							return true;
						}
						Pos = End;
					}
				}
				return false;
			}

			static bool IsMulticastEventTypeName(const std::string& TypeName)
			{
				std::vector<std::string> Args;
				return TypeName == "multicast_delegate"
					|| TryParseTypeApplication(TypeName, "multicast_delegate", Args);
			}

			static bool IsEventTypeName(const std::string& TypeName)
			{
				std::vector<std::string> Args;
				return IsMulticastEventTypeName(TypeName)
					|| TryParseTypeApplication(TypeName, "delegate", Args);
			}

			static std::string EventHandlerTypeName(const std::string& TypeName)
			{
				std::vector<std::string> Args;
				if (TryParseTypeApplication(TypeName, "delegate", Args) && !Args.empty())
				{
					const std::string ReturnType = Args.front();
					Args.erase(Args.begin());
					return Runtime::MakeFunctionTypeName(Args, ReturnType);
				}
				if (TypeName == "multicast_delegate" ||
					TryParseTypeApplication(TypeName, "multicast_delegate", Args))
				{
					return Runtime::MakeFunctionTypeName(Args, "void");
				}

				return {};
			}

			static const char* BinaryOpOperatorName(const EBinaryOp Op)
			{
				switch (Op)
				{
				case EBinaryOp::Add:
					return "operator+";
				case EBinaryOp::Subtract:
					return "operator-";
				case EBinaryOp::Multiply:
					return "operator*";
				case EBinaryOp::Divide:
					return "operator/";
				case EBinaryOp::Modulo:
					return "operator%";
				case EBinaryOp::Equal:
					return "operator==";
				case EBinaryOp::NotEqual:
					return "operator!=";
				case EBinaryOp::Less:
					return "operator<";
				case EBinaryOp::LessEqual:
					return "operator<=";
				case EBinaryOp::Greater:
					return "operator>";
				case EBinaryOp::GreaterEqual:
					return "operator>=";
				case EBinaryOp::BitAnd:
					return "operator&";
				case EBinaryOp::BitOr:
					return "operator|";
				case EBinaryOp::BitXor:
					return "operator^";
				case EBinaryOp::ShiftLeft:
					return "operator<<";
				case EBinaryOp::ShiftRight:
					return "operator>>";
				case EBinaryOp::LogicalAnd:
					return "operator&&";
				case EBinaryOp::LogicalOr:
					return "operator||";
				default:
					return nullptr;
				}
			}

			// Mirror relational op (a > b via b < a), matching VM MakeMirrorBinaryOp.
			static std::optional<EBinaryOp> MirrorBinaryOp(const EBinaryOp Op)
			{
				switch (Op)
				{
				case EBinaryOp::Greater:
					return EBinaryOp::Less;
				case EBinaryOp::Less:
					return EBinaryOp::Greater;
				case EBinaryOp::GreaterEqual:
					return EBinaryOp::LessEqual;
				case EBinaryOp::LessEqual:
					return EBinaryOp::GreaterEqual;
				default:
					return std::nullopt;
				}
			}

			static const char* UnaryOpOperatorName(const EUnaryOp Op)
			{
				switch (Op)
				{
				case EUnaryOp::Negate:
					return "operator-";
				case EUnaryOp::Not:
					return "operator!";
				case EUnaryOp::BitNot:
					return "operator~";
				default:
					return nullptr;
				}
			}

			// Maps compound assignment ops to true method names (`operator+=`, etc.).
			static const char* CompoundOpOperatorName(const EBinaryOp Op)
			{
				switch (Op)
				{
				case EBinaryOp::Add:
					return "operator+=";
				case EBinaryOp::Subtract:
					return "operator-=";
				case EBinaryOp::Multiply:
					return "operator*=";
				case EBinaryOp::Divide:
					return "operator/=";
				case EBinaryOp::Modulo:
					return "operator%=";
				case EBinaryOp::BitAnd:
					return "operator&=";
				case EBinaryOp::BitOr:
					return "operator|=";
				case EBinaryOp::BitXor:
					return "operator^=";
				case EBinaryOp::ShiftLeft:
					return "operator<<=";
				case EBinaryOp::ShiftRight:
					return "operator>>=";
				default:
					return nullptr;
				}
			}

			std::string MapParamType(
				const std::string& RawTypeName,
				const bool bIsRef,
				const bool bIsConst) const
			{
				const std::string Mapped = MapType(RawTypeName);
				if (bIsRef)
				{
					if (bIsConst)
					{
						return "const " + Mapped + '&';
					}
					return Mapped + '&';
				}
				return Mapped;
			}

			std::string ReconstructTypeFromCall(const FCallExpr& Call) const
			{
				std::string TypeApp = ApplyTypeSubst(Call.Callee);
				if (Call.TypeArgs.empty() && !Call.bExplicitTemplateArguments)
				{
					return ResolveAliasChain(TypeApp);
				}
				TypeApp.push_back('<');
				for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
				{
					if (Index > 0)
					{
						TypeApp.push_back(',');
					}
					TypeApp += ApplyTypeSubst(Call.TypeArgs[Index]);
				}
				TypeApp.push_back('>');
				return ResolveAliasChain(ApplyTypeSubst(TypeApp));
			}

			static std::string EscapeCppWStringLiteral(const std::u16string& Value)
			{
				std::string Escaped = "L\"";
				Escaped.reserve(Value.size() * 2 + 4);
				for (std::size_t Index = 0; Index < Value.size(); ++Index)
				{
					const char16_t Ch = Value[Index];
					switch (Ch)
					{
					case u'\\':
						{
							Escaped += "\\\\";
							break;
						}
					case u'"':
						{
							Escaped += "\\\"";
							break;
						}
					case u'\n':
						{
							Escaped += "\\n";
							break;
						}
					case u'\r':
						{
							Escaped += "\\r";
							break;
						}
					case u'\t':
						{
							Escaped += "\\t";
							break;
						}
					default:
						{
							if (Ch < 0x20 || Ch == 0x7F)
							{
								char Buf[16];
								std::snprintf(
									Buf,
									sizeof(Buf),
									"\\x%04X\" L\"",
									static_cast<unsigned>(Ch));
								Escaped += Buf;
							}
							else if (Ch < 0x80)
							{
								Escaped.push_back(static_cast<char>(Ch));
							}
							else
							{
								// Keep UTF-16 code units even when wchar_t is 32 bits.
								char Buf[16];
								std::snprintf(
									Buf,
									sizeof(Buf),
									"\\x%04X\" L\"",
									static_cast<unsigned>(Ch));
								Escaped += Buf;
							}
							break;
						}
					}
				}
				Escaped += '"';
				return Escaped;
			}

			bool TryInferContainerMethodReturnType(
				const std::string& ObjectType,
				const std::string& Method,
				const std::size_t ArgCount,
				std::string& OutReturnType) const
			{
				std::string ElementType;
				if (Runtime::IsDynamicArrayTypeName(ObjectType)
					|| Runtime::ParseDynamicArrayElementType(ObjectType, ElementType))
				{
					if (Method == "push" || Method == "reserve"
						|| Method == "clear" || Method == "remove_at")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "remove")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "capacity")
					{
						OutReturnType = "int64";
						return true;
					}
					if (Method == "pop" && ArgCount == 0)
					{
						if (!Runtime::ParseDynamicArrayElementType(ObjectType, ElementType))
						{
							return false;
						}
						OutReturnType = ElementType;
						return true;
					}
				}
				if (Runtime::IsMapTypeName(ObjectType))
				{
					std::string KeyType;
					std::string ValueType;
					if (!Runtime::ParseMapType(ObjectType, KeyType, ValueType))
					{
						return false;
					}
					if (Method == "has" || Method == "remove")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "clear" || Method == "reserve")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Runtime::IsMultiMapTypeName(ObjectType) && Method == "add")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "keys")
					{
						OutReturnType = "array<" + KeyType + ">";
						return true;
					}
					if (Method == "values")
					{
						OutReturnType = "array<" + ValueType + ">";
						return true;
					}
					if (Method == "entries")
					{
						OutReturnType = "array<" + Runtime::MakePairTypeName(KeyType, ValueType) + ">";
						return true;
					}
				}
				if (Runtime::IsOrderedMapTypeName(ObjectType))
				{
					std::string KeyType;
					std::string ValueType;
					if (!Runtime::ParseOrderedMapType(ObjectType, KeyType, ValueType))
					{
						return false;
					}
					if (Method == "has" || Method == "remove")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "clear" || Method == "reserve")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Runtime::IsOrderedMultiMapTypeName(ObjectType) && Method == "add")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "keys")
					{
						OutReturnType = "array<" + KeyType + ">";
						return true;
					}
					if (Method == "values")
					{
						OutReturnType = "array<" + ValueType + ">";
						return true;
					}
					if (Method == "entries")
					{
						OutReturnType = "array<" + Runtime::MakePairTypeName(KeyType, ValueType) + ">";
						return true;
					}
				}
				if (Runtime::IsMultiSetTypeName(ObjectType)
					|| Runtime::IsOrderedMultiSetTypeName(ObjectType))
				{
					if (Method == "has" || Method == "remove")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "count")
					{
						OutReturnType = "int64";
						return true;
					}
					if (Method == "add" || Method == "reserve" || Method == "clear")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "values")
					{
						if (!Runtime::ParseMultiSetElementType(ObjectType, ElementType))
						{
							return false;
						}
						OutReturnType = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsSetTypeName(ObjectType))
				{
					if (Method == "has" || Method == "remove")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "add" || Method == "reserve" || Method == "clear")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "values")
					{
						if (!Runtime::ParseSetElementType(ObjectType, ElementType))
						{
							return false;
						}
						OutReturnType = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::IsDequeTypeName(ObjectType))
				{
					if (Method == "push_back" || Method == "push_front" || Method == "clear")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "pop_back" || Method == "pop_front" || Method == "front" || Method == "back")
					{
						return Runtime::ParseDequeElementType(ObjectType, OutReturnType);
					}
					if (Method == "values")
					{
						if (!Runtime::ParseDequeElementType(ObjectType, ElementType))
						{
							return false;
						}
						OutReturnType = "array<" + ElementType + ">";
						return true;
					}
				}
				if (Runtime::ParseQueueElementType(ObjectType, ElementType))
				{
					if (Method == "push" || Method == "clear")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "pop"
						|| (Method == "front"
							&& !Runtime::IsPriorityQueueTypeName(ObjectType))
						|| (Method == "top"
							&& Runtime::IsPriorityQueueTypeName(ObjectType)))
					{
						OutReturnType = ElementType;
						return true;
					}
				}
				if (Runtime::ParseStackElementType(ObjectType, ElementType))
				{
					if (Method == "push" || Method == "clear")
					{
						OutReturnType = ObjectType;
						return true;
					}
					if (Method == "pop" || Method == "top")
					{
						OutReturnType = ElementType;
						return true;
					}
				}
				return false;
			}

			// delegate<R,...>/multicast_delegate(<...>) hard-coded template API (see MakeRuntimeSupport).
			static bool TryInferEventMethodReturnType(
				const std::string& ObjectType,
				const std::string& Method,
				std::string& OutReturnType)
			{
				std::vector<std::string> DelegateArgs;
				if (TryParseTypeApplication(ObjectType, "delegate", DelegateArgs) && !DelegateArgs.empty())
				{
					if (Method == "is_bound" || Method == "execute_if_bound")
					{
						OutReturnType = "bool";
						return true;
					}
					if (Method == "execute")
					{
						OutReturnType = DelegateArgs[0];
						return true;
					}
					if (Method == "bind" || Method == "unbind")
					{
						OutReturnType = "void";
						return true;
					}
					return false;
				}
				if (IsMulticastEventTypeName(ObjectType))
				{
					if (Method == "count")
					{
						OutReturnType = "int64";
						return true;
					}
					if (Method == "add" || Method == "remove" || Method == "broadcast"
						|| Method == "clear")
					{
						OutReturnType = "void";
						return true;
					}
					return false;
				}
				return false;
			}
