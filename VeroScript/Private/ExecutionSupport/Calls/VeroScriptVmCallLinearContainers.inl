// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		if (const auto* QueueHandle = std::get_if<FQueueHandle>(&ThisValue))
		{
			if (!*QueueHandle)
			{
				ReportError("method call requires object", SourceLocation);
				return false;
			}
			const bool bPriorityQueue =
				Runtime::IsPriorityQueueTypeName((*QueueHandle)->TypeName);
			if (MethodName == "push")
			{
				if (ArgumentCount != 1)
				{
					ReportError("push expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(Arguments[0], (*QueueHandle)->ElementTypeName, SourceLocation))
				{
					ReportError(
						std::string("push argument type mismatch for ")
							+ (bPriorityQueue ? "priority_queue<" : "queue<")
							+ (*QueueHandle)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				if (!bPriorityQueue)
				{
					if (!AppendClonedValue(
							(*QueueHandle)->Elements,
							Arguments[0],
							SourceLocation))
					{
						return false;
					}
				}
				else
				{
					FValue Cloned;
					if (!CloneValueOrReport(Arguments[0], Cloned, SourceLocation))
					{
						return false;
					}
					// Keep the maximum at the front with the same binary-heap model
					// used by AOT. A sorted insertion here made every push O(n).
					(*QueueHandle)->Elements.push_back(std::move(Cloned));
					std::push_heap(
						(*QueueHandle)->Elements.begin(),
						(*QueueHandle)->Elements.end(),
						VmDetail::PriorityValueLess);
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "pop")
			{
				if (ArgumentCount != 0)
				{
					ReportError("pop expects no arguments", SourceLocation);
					return false;
				}
				if ((*QueueHandle)->Elements.empty())
				{
					ReportError("pop from empty queue", SourceLocation);
					return false;
				}
				if (bPriorityQueue)
				{
					std::pop_heap(
						(*QueueHandle)->Elements.begin(),
						(*QueueHandle)->Elements.end(),
						VmDetail::PriorityValueLess);
					FValue Top = std::move((*QueueHandle)->Elements.back());
					(*QueueHandle)->Elements.pop_back();
					Push(std::move(Top));
					return true;
				}
				FValue Front = std::move((*QueueHandle)->Elements.front());
				(*QueueHandle)->Elements.pop_front();
				Push(std::move(Front));
				return true;
			}
			if ((MethodName == "front" && !bPriorityQueue)
				|| (MethodName == "top" && bPriorityQueue))
			{
				if (ArgumentCount != 0)
				{
					ReportError(
						bPriorityQueue ? "top expects no arguments" : "front expects no arguments",
						SourceLocation);
					return false;
				}
				if ((*QueueHandle)->Elements.empty())
				{
					ReportError(
						bPriorityQueue
							? "top of empty priority_queue"
							: "front of empty queue",
						SourceLocation);
					return false;
				}
				Push((*QueueHandle)->Elements.front());
				return true;
			}
			if (MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				const std::string ValuesTypeName =
					"array<" + (*QueueHandle)->ElementTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				ReserveDynamicArrayStorage(*ValuesArray, (*QueueHandle)->Elements.size());
				if (bPriorityQueue)
				{
					auto Pending = (*QueueHandle)->Elements;
					while (!Pending.empty())
					{
						std::pop_heap(
							Pending.begin(),
							Pending.end(),
							VmDetail::PriorityValueLess);
						if (!AppendDynamicArrayValue(
								*ValuesArray, Pending.back(), SourceLocation))
						{
							return false;
						}
						Pending.pop_back();
					}
				}
				else
				{
					for (const FValue& Element : (*QueueHandle)->Elements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Element, SourceLocation))
						{
							return false;
						}
					}
				}
				return true;
			}
			if (MethodName == "clear")
			{
				if (ArgumentCount != 0)
				{
					ReportError("clear expects no arguments", SourceLocation);
					return false;
				}
				if (!ReleaseDetachedValueList((*QueueHandle)->Elements, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
		if (const auto* StackHandle = std::get_if<FStackHandle>(&ThisValue))
		{
			if (!*StackHandle)
			{
				ReportError("method call requires object", SourceLocation);
				return false;
			}
			if (MethodName == "push")
			{
				if (ArgumentCount != 1)
				{
					ReportError("push expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(Arguments[0], (*StackHandle)->ElementTypeName, SourceLocation))
				{
					ReportError(
						"push argument type mismatch for stack<" + (*StackHandle)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				if (!AppendClonedValue((*StackHandle)->Elements, Arguments[0], SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "pop")
			{
				if (ArgumentCount != 0)
				{
					ReportError("pop expects no arguments", SourceLocation);
					return false;
				}
				if ((*StackHandle)->Elements.empty())
				{
					ReportError("pop from empty stack", SourceLocation);
					return false;
				}
				FValue Top = std::move((*StackHandle)->Elements.back());
				(*StackHandle)->Elements.pop_back();
				Push(std::move(Top));
				return true;
			}
			if (MethodName == "top")
			{
				if (ArgumentCount != 0)
				{
					ReportError("top expects no arguments", SourceLocation);
					return false;
				}
				if ((*StackHandle)->Elements.empty())
				{
					ReportError("top of empty stack", SourceLocation);
					return false;
				}
				Push((*StackHandle)->Elements.back());
				return true;
			}
			if (MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				const std::string ValuesTypeName =
					"array<" + (*StackHandle)->ElementTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				ReserveDynamicArrayStorage(*ValuesArray, (*StackHandle)->Elements.size());
				for (const FValue& Element : (*StackHandle)->Elements)
				{
					if (!AppendDynamicArrayValue(*ValuesArray, Element, SourceLocation))
					{
						return false;
					}
				}
				return true;
			}
			if (MethodName == "clear")
			{
				if (ArgumentCount != 0)
				{
					ReportError("clear expects no arguments", SourceLocation);
					return false;
				}
				if (!ReleaseDetachedValueList((*StackHandle)->Elements, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
		if (const auto* DequeHandle = std::get_if<FDequeHandle>(&ThisValue))
		{
			if (!*DequeHandle)
			{
				ReportError("method call requires object", SourceLocation);
				return false;
			}
			const auto PushVisibleElement = [&](const FValue& Stored) -> bool
			{
				const FValue* const Visible = UnwrapClosureReferenceCell(&Stored);
				if (!Visible)
				{
					ReportError("invalid captured reference cell", SourceLocation);
					return false;
				}
				Push(*Visible);
				return !bHasError;
			};
			const auto PushRemovedElement = [&](FValue Removed) -> bool
			{
				if (!IsClosureReferenceCell(Removed))
				{
					Push(std::move(Removed));
					return !bHasError;
				}
				const FValue* const Visible = UnwrapClosureReferenceCell(&Removed);
				if (!Visible)
				{
					ReportError("invalid captured reference cell", SourceLocation);
					return false;
				}
				FValue Result = *Visible;
				if (!ReleaseValue(Removed, SourceLocation))
				{
					return false;
				}
				Push(std::move(Result));
				return !bHasError;
			};
			if (MethodName == "push_back")
			{
				if (ArgumentCount != 1)
				{
					ReportError("push_back expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(
						Arguments[0],
						(*DequeHandle)->ElementTypeName,
						SourceLocation))
				{
					ReportError(
						"push_back argument type mismatch for deque<"
							+ (*DequeHandle)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				if (!AppendClonedValue((*DequeHandle)->Elements, Arguments[0], SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "push_front")
			{
				if (ArgumentCount != 1)
				{
					ReportError("push_front expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(
						Arguments[0],
						(*DequeHandle)->ElementTypeName,
						SourceLocation))
				{
					ReportError(
						"push_front argument type mismatch for deque<"
							+ (*DequeHandle)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				FValue DequeFront;
				if (!CloneValueOrReport(Arguments[0], DequeFront, SourceLocation))
				{
					return false;
				}
				(*DequeHandle)->Elements.push_front(std::move(DequeFront));
				Push(ThisValue);
				return true;
			}
			if (MethodName == "pop_back")
			{
				if (ArgumentCount != 0)
				{
					ReportError("pop_back expects no arguments", SourceLocation);
					return false;
				}
				if ((*DequeHandle)->Elements.empty())
				{
					ReportError("pop_back from empty deque", SourceLocation);
					return false;
				}
				FValue Back = std::move((*DequeHandle)->Elements.back());
				(*DequeHandle)->Elements.pop_back();
				return PushRemovedElement(std::move(Back));
			}
			if (MethodName == "pop_front")
			{
				if (ArgumentCount != 0)
				{
					ReportError("pop_front expects no arguments", SourceLocation);
					return false;
				}
				if ((*DequeHandle)->Elements.empty())
				{
					ReportError("pop_front from empty deque", SourceLocation);
					return false;
				}
				FValue Front = std::move((*DequeHandle)->Elements.front());
				(*DequeHandle)->Elements.pop_front();
				return PushRemovedElement(std::move(Front));
			}
			if (MethodName == "front")
			{
				if (ArgumentCount != 0)
				{
					ReportError("front expects no arguments", SourceLocation);
					return false;
				}
				if ((*DequeHandle)->Elements.empty())
				{
					ReportError("front from empty deque", SourceLocation);
					return false;
				}
				return PushVisibleElement((*DequeHandle)->Elements.front());
			}
			if (MethodName == "back")
			{
				if (ArgumentCount != 0)
				{
					ReportError("back expects no arguments", SourceLocation);
					return false;
				}
				if ((*DequeHandle)->Elements.empty())
				{
					ReportError("back from empty deque", SourceLocation);
					return false;
				}
				return PushVisibleElement((*DequeHandle)->Elements.back());
			}
			if (MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				const std::string ValuesTypeName =
					"array<" + (*DequeHandle)->ElementTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				ReserveDynamicArrayStorage(*ValuesArray, (*DequeHandle)->Elements.size());
				for (const FValue& Element : (*DequeHandle)->Elements)
				{
					if (!AppendDynamicArrayValue(*ValuesArray, Element, SourceLocation))
					{
						return false;
					}
				}
				return true;
			}
			if (MethodName == "clear")
			{
				if (ArgumentCount != 0)
				{
					ReportError("clear expects no arguments", SourceLocation);
					return false;
				}
				if (!ReleaseDetachedValueList((*DequeHandle)->Elements, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
