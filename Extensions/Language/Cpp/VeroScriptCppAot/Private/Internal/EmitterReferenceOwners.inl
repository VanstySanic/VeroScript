// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			std::unordered_map<const FExpr*, std::string> RetainedReferenceReceivers;
			std::unordered_set<std::string> RetainedReferenceAliases;

			bool ReferenceOwnerHasCleanup(const FUserTypeInfo& Type, std::unordered_set<std::string>& Visiting)
			{
				if (!Visiting.insert(Type.Name).second) return false;
				if (Type.Destructor && Type.Destructor->Body && !Type.Destructor->Body->Statements.empty()) return true;
				for (const auto* Bases : {&Type.BaseTypeNames, &Type.InterfaceTypeNames})
				{
					for (const auto& Base : *Bases)
					{
						if (const auto* Parent = ResolveUserTypeForLookup(SubstForUserType(Type, Base));
							Parent && ReferenceOwnerHasCleanup(*Parent, Visiting))
						{
							return true;
						}
					}
				}

				for (const auto& Field : *Type.Fields)
				{
					if (Field.bIsStatic)
					{
						continue;
					}

					const std::string FieldType = ResolveValueType(SubstForUserType(Type, Field.TypeName));
					if (Runtime::IsPrimitiveValueTypeName(FieldType))
					{
						continue;
					}

					if (const auto* Member = ResolveUserTypeForLookup(FieldType))
					{
						if (ReferenceOwnerHasCleanup(*Member, Visiting)) return true;
					}
					else
					{
						return true;
					}
					// Native resources and opaque aggregate storage need VM ownership.
				}
				// Receivers and their fields can hold derived objects with cleanup,
				// including when the getter itself is not virtual.
				// Looking up inherited generic types can add monomorphs. Do not
				// keep unordered_map iterators live across recursive lookups.
				std::vector<std::string> KnownTypes;
				KnownTypes.reserve(UserTypes.size());
				for (const auto& Entry : UserTypes)
				{
					KnownTypes.push_back(Entry.first);
				}

				for (const auto& Name : KnownTypes)
				{
					if (Name != Type.Name && IsAotSubtypeOf(Name, Type.Name))
					{
						if (const auto* Derived = ResolveUserTypeForLookup(Name);
							Derived && ReferenceOwnerHasCleanup(*Derived, Visiting))
						{
							return true;
						}
					}
				}

				return false;
			}

			bool ReferenceOwnerHasCleanup(const FUserTypeInfo& Type)
			{
				std::unordered_set<std::string> Visiting;
				return ReferenceOwnerHasCleanup(Type, Visiting);
			}

			void RetainReferenceReceiver(const FExpr& Receiver, const std::string& Pad, const bool bThreadLocal)
			{
				const std::string Owner = SequencedOperandName(NextSequence++, 0);
				Out << Pad << (bThreadLocal ? "thread_local auto " : "auto ") << Owner << " = (";
				const bool bThis = Receiver.Kind == EExprKind::This
					|| (Receiver.Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(Receiver).Name == "this");
				if (bThis && CapturedClassThisLambdaDepth == 0u)
				{
					Out << (bMemberMethodBody ? "VsRetainObject(this)" : "VsRetainObject(&" + SelfReceiverName + ')');
				}

				else
				{
					EmitExpr(Receiver);
				}

				Out << ");\n";
				RetainedReferenceReceivers[&Receiver] = Owner;
			}

			bool IsTemporaryReferenceReceiver(const FExpr& Expr)
			{
				if (Runtime::IsReferenceTypeName(InferExprType(Expr))) return false;
				return Expr.Kind != EExprKind::Identifier && Expr.Kind != EExprKind::This
					&& Expr.Kind != EExprKind::MemberAccess && Expr.Kind != EExprKind::IndexAccess
					&& Expr.Kind != EExprKind::Ref;
			}

			bool ReferenceNeedsReturnedOwner(const FExpr& Expr)
			{
				if (Expr.Kind == EExprKind::Ref)
				{
					return ReferenceNeedsReturnedOwner(*static_cast<const FRefExpr&>(Expr).Operand);
				}

				if (Expr.Kind == EExprKind::IndexAccess) return true;
				if (Expr.Kind == EExprKind::MethodCall)
				{
					const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
					return Call.Object && IsTemporaryReferenceReceiver(*Call.Object);
				}
				if (Expr.Kind != EExprKind::MemberAccess) return false;
				const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
				if (!Access.Object) return false;
				if (const auto* Type =
						ResolveUserTypeForLookup(ResolveValueType(InferExprType(*Access.Object)));
					Type && Type->bIsClass)
				{
					return true;
				}

				return ReferenceNeedsReturnedOwner(*Access.Object);
			}

			void PrepareReferenceInitializer(const FExpr& Expr, const std::string& Pad, const bool bThreadLocal = false)
			{
				if (Expr.Kind == EExprKind::Ref)
				{
					PrepareReferenceInitializer(*static_cast<const FRefExpr&>(Expr).Operand, Pad, bThreadLocal);
					return;
				}
				if (Expr.Kind == EExprKind::IndexAccess)
				{
					Fail("AOT: indexed reference alias requires managed storage paths; use the VM backend");
					return;
				}
				if (Expr.Kind == EExprKind::MemberAccess)
				{
					const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
					if (!Access.Object) return;
					// Nested struct fields may still reside inside a class object.
					PrepareReferenceInitializer(*Access.Object, Pad, bThreadLocal);
					if (bFailed) return;
					const auto* Type = ResolveUserTypeForLookup(ResolveValueType(InferExprType(*Access.Object)));
					if (!Type || !Type->bIsClass) return;
					if (ReferenceOwnerHasCleanup(*Type))
					{
						Fail("AOT: field reference owner requires managed reference cleanup; use the VM backend");
						return;
					}
					if (Runtime::IsReferenceTypeName(ResolveAliasChain(FieldType(*Type, Access.Member))))
					{
						Fail("AOT: reference field requires managed owner retention; use the VM backend");
						return;
					}
					RetainReferenceReceiver(*Access.Object, Pad, bThreadLocal);
					return;
				}
				if (Expr.Kind != EExprKind::MethodCall) return;
				const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
				if (!Call.Object || !Runtime::IsReferenceTypeName(InferExprType(Expr))) return;
				PrepareReferenceInitializer(*Call.Object, Pad, bThreadLocal);
				if (bFailed) return;
				const auto* Type = ResolveUserTypeForLookup(ResolveValueType(InferExprType(*Call.Object)));
				if (!Type) return;
				if (!Type->bIsClass)
				{
					if (IsTemporaryReferenceReceiver(*Call.Object))
					{
						Fail(
							"AOT: temporary value receiver requires managed owner retention; use the VM backend");
					}

					return;
				}
				if (ReferenceOwnerHasCleanup(*Type))
				{
					Fail("AOT: field reference owner requires managed reference cleanup; use the VM backend");
					return;
				}
				std::vector<std::string> Args;
				for (const auto* Arg : Call.GetArguments())
				{
					Args.push_back(InferExprType(*Arg));
				}

				const FUserTypeInfo* Declaring = nullptr;
				const auto* Method = ResolveMethod(*Type, Call.Method, Args, &Declaring);
				const bool bVirtual = Method && (Method->bVirtual || Method->bOverride
					|| MethodMatchesVirtualInterfaceSlot(*Type, *Method));
				std::string FieldName;
				if (Method && !Method->bIsStatic && Method->Body && Method->Body->Statements.size() == 1
					&& (!bVirtual || Type->bFinal || Method->bFinal))
				{
					const auto* Statement = Method->Body->Statements.front().get();
					if (Statement && Statement->Kind == EStmtKind::Return)
					{
						const FExpr* Value = static_cast<const FReturnStmt&>(*Statement).Value.get();
						if (Value && Value->Kind == EExprKind::Identifier)
						{
							FieldName = static_cast<const FIdentifierExpr&>(*Value).Name;
						}

						else if (Value && Value->Kind == EExprKind::MemberAccess)
						{
							const auto& Member = static_cast<const FMemberAccessExpr&>(*Value);
							if (Member.Object->Kind == EExprKind::This)
							{
								FieldName = Member.Member;
							}
						}
					}
				}
				const auto* Field = Declaring && Declaring->Fields ? FindFieldInList(Declaring->Fields, FieldName) : nullptr;
				const bool bShadowed = Method &&
					std::any_of(Method->Parameters.begin(), Method->Parameters.end(),
						[&](const auto& Param)
						{
							return Param.Name == FieldName;
						});
				if (!Field || Field->bIsStatic || bShadowed ||
					Runtime::IsReferenceTypeName(SubstForUserType(*Declaring, Field->TypeName)))
				{
					Fail("AOT: reference initializer requires managed owner retention; use the VM backend");
					return;
				}
				RetainReferenceReceiver(*Call.Object, Pad, bThreadLocal);
			}
