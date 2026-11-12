// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Controls.vs";
export import "Widget.vs";

namespace VS::Gui
{
	// Fixed-extent, data-backed virtualization. Existing authored widget trees are unchanged.
	namespace Internal
	{
		export class VirtualRows : public VS::Gui::BindingState
		{
		public:
			static void ValidateExtent(int64 count, double extent, int64 padding)
			{
				assert(extent > 0 && extent <= 16777216 && padding >= 0 && padding <= 256, "Invalid virtual row extent or overscan");
				assert(count * extent <= 16777216, "Virtual row extent exceeds the layout coordinate range");
			}
			VirtualRows(ScrollView view, Widget host, array<string> source, double extent, int64 padding, fn(string, Widget) -> Widget binder, fn(array<Widget>, bool) -> void publisher)
			{
				VS::Gui::Internal::VirtualRows::ValidateExtent(len(source), extent, padding);
				map<string, int64> positions = map<string, int64>();
				for (int64 index = 0; index < len(source); index++)
				{
					assert(source[index] != "" && !positions.has(source[index]), "Virtual row keys must be nonempty and unique");
					positions[source[index]] = index;
				}
				owner = weak<ScrollView>(view);
				contentHost = weak<Widget>(host);
				for (string key : source)
				{
					keys.push(key);
				}
				indices = positions;
				rowHeight = extent;
				overscan = padding;
				bind = binder;
				publish = publisher;
			}
			int64 Count() const
			{
				return len(keys);
			}
			void VsguiSynchronizeBindings() override
			{
				Synchronize();
			}
			void VsguiReleaseBindings() override
			{
				Dispose();
			}
			int64 RealizedCount() const
			{
				return len(rowKeys);
			}
			bool Has(string key) const
			{
				return indices.has(key);
			}
			int64 IndexOf(string key) const
			{
				return indices.has(key) ? indices[key] : -1i64;
			}
			string KeyAt(int64 index) const
			{
				return keys[index];
			}
			Widget ResolveInputRow(int64 index)
			{
				assert(!disposed && !applying && index >= 0 && index < len(keys), "Invalid virtual input row");
				string key = keys[index];
				int64 previous = inputProbe;
				try
				{
					inputProbe = index;
					Update();
					if (!disposed && rows.has(key)) return rows[key];
					return nullptr;
				}
				finally
				{
					inputProbe = previous;
					Schedule();
				}
			}
			bool IsApplying() const
			{
				return applying;
			}
			bool IsSpacer(Widget item) const
			{
				for (Widget spacer : spacers)
				{
					if (spacer == item) return true;
				}
				return false;
			}
			void Refresh()
			{
				revision++;
				refresh = true;
				Schedule();
			}
			void Reorder(array<string> order)
			{
				assert(!applying, "Virtual rows cannot be reordered during realization");
				assert(len(order) == len(keys), "Virtual row sort must preserve every key");
				map<string, int64> next = map<string, int64>();
				for (int64 index = 0; index < len(order); index++)
				{
					assert(indices.has(order[index]) && !next.has(order[index]), "Invalid virtual row sort keys");
					next[order[index]] = index;
				}
				array<string> snapshot = array<string>();
				for (string key : order)
				{
					snapshot.push(key);
				}
				keys = snapshot;
				indices = next;
				first = -1;
				revision++;
				Schedule();
			}
			array<string> GetKeys() const
			{
				array<string> snapshot = array<string>();
				for (string key : keys)
				{
					snapshot.push(key);
				}
				return snapshot;
			}
			void ScrollToIndex(int64 index, bool input = false)
			{
				assert(index >= 0 && index < len(keys), "Invalid virtual row index");
				ScrollView view = owner.lock();
				if (view == nullptr || view.IsDisposed()) return;
				pendingReveal = view.__GetNativeHandle().HasLayout() ? "" : keys[index];
				pendingInputReveal = input;
				Reveal(view, index, input);
				if (disposed || view.IsDisposed()) return;
				pendingRevealScroll = view.GetScrollY();
				Schedule();
			}
			void Schedule()
			{
				ScrollView view = owner.lock();
				if (disposed || view == nullptr || view.IsDisposed() || !view.IsMounted()) return;
				int64 mount = view.__GetNativeHandle().GetMountGeneration();
				if (queued && queuedMount == mount) return;
				queued = true;
				queuedMount = mount;
				int64 ticket =++queueRevision;
				weak<VirtualRows> state = weak<VirtualRows>(this);
				try
				{
					view.__GetNativeHandle().GetContext().Post([state, ticket, mount]() mutable -> void
					{
						VirtualRows rows = state.lock(); if (rows == nullptr || rows.disposed || rows.queueRevision != ticket) return; rows.queued = false; ScrollView owner = rows.owner.lock(); if (owner == nullptr || owner.IsDisposed() || !owner.IsMounted() || owner.__GetNativeHandle().GetMountGeneration() != mount) return; rows.Update();
					});
				}
				catch (...)
				{
					queued = false;
					throw;
				}
			}
			void Synchronize()
			{
				ScrollView view = owner.lock();
				if (disposed || view == nullptr || view.IsDisposed()) return;
				double viewport = ReadViewport(view);
				if (refresh || view.GetScrollY() != lastScroll || viewport != lastViewport || ReadOffset(view) != lastOffset || pinned || pendingReveal != "")
				{
					Schedule();
				}
			}
			void Update()
			{
				ScrollView view = owner.lock();
				if (disposed || applying || view == nullptr || view.IsDisposed()) return;
				if (pendingReveal != "" && view.__GetNativeHandle().HasLayout())
				{
					// The first real viewport replaces the bootstrap estimate. A
					// later explicit scroll remains authoritative over this request.
					if (view.GetScrollY() == pendingRevealScroll && indices.has(pendingReveal))
					{
						Reveal(view, indices[pendingReveal], pendingInputReveal);
					}
					pendingReveal = "";
					if (disposed || view.IsDisposed()) return;
				}
				double viewport = ReadViewport(view);
				double offset = ReadOffset(view);
				double scroll = min(max(0.0, view.GetScrollY()), max(0.0, len(keys) * rowHeight + offset - viewport)) - offset;
				int64 firstVisible = cast<int64>(min(cast<double>(len(keys)), max(0.0, scroll) / rowHeight));
				int64 afterVisible = cast<int64>(min(cast<double>(len(keys)), max(0.0, scroll + viewport) / rowHeight));
				int64 begin = max(0i64, firstVisible - overscan);
				int64 end = afterVisible + min(len(keys) - afterVisible, overscan + 1);
				array<int64> wanted = array<int64>();
				map<string, bool> keep = map<string, bool>();
				for (int64 index = begin; index < end; index++)
				{
					wanted.push(index);
					keep[keys[index]] = true;
				}
				// Keyboard navigation may inspect one offscreen candidate without
				// moving a bound scroll model or realizing the intervening rows.
				if (inputProbe >= 0 && inputProbe < len(keys) && !keep.has(keys[inputProbe]))
				{
					wanted.push(inputProbe);
					for (int64 at = len(wanted) - 1; at > 0 && wanted[at - 1] > wanted[at]; at--)
					{
						int64 previous = wanted[at - 1];
						wanted[at - 1] = wanted[at];
						wanted[at] = previous;
					}
					keep[keys[inputProbe]] = true;
				}
				bool nextPinned = false;
				for (string key : rowKeys)
				{
					if (!indices.has(key) || keep.has(key) || rows[key].IsDisposed() || !rows[key].__GetNativeHandle().HasActiveInput())
					{
						continue;
					}
					int64 index = indices[key];
					wanted.push(index);
					for (int64 at = len(wanted) - 1; at > 0 && wanted[at - 1] > wanted[at]; at--)
					{
						int64 previous = wanted[at - 1];
						wanted[at - 1] = wanted[at];
						wanted[at] = previous;
					}
					keep[key] = true;
					nextPinned = true;
				}
				bool unchanged = len(wanted) == len(rowKeys);
				for (int64 index = 0; unchanged && index < len(wanted); index++)
				{
					unchanged = keys[wanted[index]] == rowKeys[index] && !rows[rowKeys[index]].IsDisposed();
				}
				if (!refresh && first == begin && last == end && unchanged)
				{
					lastScroll = view.GetScrollY();
					lastViewport = viewport;
					lastOffset = offset;
					return;
				}
				applying = true;
				int64 startedRevision = revision;
				array<Widget> recycle = array<Widget>();
				try
				{
					fn(string, Widget) -> Widget resolve = bind;
					fn(array<Widget>, bool) -> void emit = publish;
					// Detach recycled rows before rebinding them; user callbacks see committed ownership.
					array<Widget> retained = array<Widget>();
					map<string, Widget> retainedRows = map<string, Widget>();
					array<string> retainedKeys = array<string>();
					for (string key : rowKeys)
					{
						Widget row = rows[key];
						if (row.IsDisposed())
						{
							continue;
						}
						if (keep.has(key))
						{
							retained.push(row);
							retainedRows[key] = row;
							retainedKeys.push(key);
						}
						else
						{
							recycle.push(row);
						}
					}
					if (len(recycle) != 0)
					{
						emit(retained, false);
					}
					if (disposed || view.IsDisposed()) return;
					rows = retainedRows;
					rowKeys = retainedKeys;
					map<string, Widget> next = map<string, Widget>();
					map<int64, bool> identities = map<int64, bool>();
					array<string> nextKeys = array<string>();
					array<Widget> children = array<Widget>();
					int64 cursor = 0;
					int64 gap = 0;
					for (int64 index : wanted)
					{
						string key = keys[index];
						bool existing = rows.has(key) && !rows[key].IsDisposed();
						Widget row = nullptr;
						if (existing)
						{
							row = rows[key];
						}
						if (row == nullptr && len(recycle) != 0)
						{
							row = recycle[len(recycle) - 1];
						}
						if (refresh || !existing)
						{
							Widget previous = row;
							row = resolve(key, previous);
							if (disposed || view.IsDisposed())
							{
								if (row != nullptr && !row.IsDisposed() && row != previous && row.GetParent() == nullptr)
								{
									row.Dispose();
								}
								return;
							}
							assert(row != nullptr && !row.IsDisposed(), "Virtual row factory returned an invalid widget");
							assert(!identities.has(row.__GetNativeHandle().GetIdentity()), "Virtual row factories must return distinct widgets");
							assert(row == previous || (row.GetParent() == nullptr && !row.IsMounted()), "A virtual row factory must return its row or an unattached widget");
							if (row != previous)
							{
								for (string held : rowKeys)
								{
									assert(held == key || rows[held] != row, "A virtual row belongs to another key");
								}
							}
							assert(previous == nullptr || previous == row || !previous.__GetNativeHandle().HasActiveInput(), "A virtual row with active input must be rebound in place");
							// Publish ownership before any operation that can fail. A retry
							// retains every successful factory result, including detached rows.
							rows[key] = row;
							if (!existing)
							{
								rowKeys.push(key);
							}
							if (!existing && len(recycle) != 0)
							{
								recycle.remove_at(len(recycle) - 1);
							}
							if (previous != nullptr && previous != row && !previous.IsDisposed())
							{
								previous.Dispose();
							}
						}
						int64 identity = row.__GetNativeHandle().GetIdentity();
						assert(!identities.has(identity), "Virtual row factories must return distinct widgets");
						identities[identity] = true;
						row.SetHeight(Length(rowHeight));
						row.SetClipContent(true);
						if (index > cursor)
						{
							children.push(VsguiGap(gap++, (index - cursor) * rowHeight));
						}
						children.push(row);
						cursor = index + 1;
						next[key] = row;
						nextKeys.push(key);
					}
					if (cursor < len(keys))
					{
						children.push(VsguiGap(gap++, (len(keys) - cursor) * rowHeight));
					}
					rows = next;
					rowKeys = nextKeys;
					emit(children, false);
					first = begin;
					last = end;
					pinned = nextPinned;
					lastScroll = view.GetScrollY();
					lastViewport = viewport;
					lastOffset = offset;
					refresh = revision != startedRevision;
				}
				catch (...)
				{
					refresh = true;
					first = -1;
					Schedule();
					throw;
				}
				finally
				{
					try
					{
						for (Widget row : recycle)
						{
							if (!row.IsDisposed())
							{
								row.Dispose();
							}
						}
					}
					finally
					{
						applying = false;
					}
				}
				if (disposed || view.IsDisposed()) return;
				fn(array<Widget>, bool) -> void complete = publish;
				complete(array<Widget>(), true);
			}
			void Dispose()
			{
				if (disposed) return;
				disposed = true;
				map<string, Widget> previous = rows;
				array<string> previousKeys = rowKeys;
				array<Widget> previousSpacers = spacers;
				rows = map<string, Widget>();
				rowKeys = array<string>();
				spacers = array<Widget>();
				keys = array<string>();
				indices = map<string, int64>();
				try
				{
					// A disposed view can remain referenced by application code.
					// Release captured application models independently of that reference.
					bind = [](string key, Widget previous) -> Widget
					{
						return previous;
					};
					publish = [](array<Widget> children, bool commit) -> void
					{
					};
				}
				finally
				{
					try
					{
						for (string key : previousKeys)
						{
							if (!previous[key].IsDisposed())
							{
								previous[key].Dispose();
							}
						}
					}
					finally
					{
						for (Widget spacer : previousSpacers)
						{
							if (!spacer.IsDisposed())
							{
								spacer.Dispose();
							}
						}
					}
				}
			}
		private:
			void Reveal(ScrollView view, int64 index, bool input = false)
			{
				double viewport = ReadViewport(view);
				bool laidOut = view.__GetNativeHandle().HasLayout();
				double model = view.GetScrollY();
				double scroll = input && laidOut ? view.GetActualScrollY() : model;
				double offset = ReadOffset(view);
				double start = offset + index * rowHeight;
				double next = scroll;
				if (start < scroll)
				{
					next = start;
				}
				else if (start + rowHeight > scroll + viewport)
				{
					next = max(0.0, start + rowHeight - viewport);
				}
				// Input reports a candidate just like native scrolling; the model may
				// accept or reject it without losing its attribute source.
				if (input)
				{
					next = min(next, max(0.0, offset + len(keys) * rowHeight - viewport));
					if (next != scroll || next != model)
					{
						view.VsguiControlAction(5, false, 0, "", laidOut ? view.GetActualScrollX() : view.GetScrollX(), next);
					}
				}
				else if (next != scroll)
				{
					view.SetScrollY(next);
				}
			}
			double ReadViewport(ScrollView view) const
			{
				return view.__GetNativeHandle().HasLayout() ? view.__GetNativeHandle().GetLayoutValue(7) : 400.0;
			}
			double ReadOffset(ScrollView view) const
			{
				Widget host = contentHost.lock();
				if (host == nullptr || !host.__GetNativeHandle().HasLayout() || !view.__GetNativeHandle().HasLayout()) return 0;
				return max(0.0, host.__GetNativeHandle().GetLayoutValue(5) - view.__GetNativeHandle().GetLayoutValue(5) + view.GetActualScrollY());
			}
			Widget VsguiGap(int64 index, double extent)
			{
				while (len(spacers) <= index)
				{
					spacers.push(VS::Gui::Spacer::Create().HitTest(HitTestMode::None));
				}
				Widget result = spacers[index];
				result.SetHeight(Length(extent));
				return result;
			}
			weak<ScrollView> owner;
			weak<Widget> contentHost;
			array<string> keys;
			map<string, int64> indices;
			map<string, Widget> rows;
			array<string> rowKeys;
			array<Widget> spacers;
			fn(string, Widget) -> Widget bind;
			fn(array<Widget>, bool) -> void publish;
			double rowHeight = 0;
			double lastScroll = -1;
			double lastViewport = -1;
			double lastOffset = -1;
			double pendingRevealScroll = 0;
			string pendingReveal = "";
			bool pendingInputReveal = false;
			int64 overscan = 2;
			int64 first = -1;
			int64 last = -1;
			int64 inputProbe = -1;
			int64 revision = 0;
			// A closed context discards its pending work. A new mount supersedes
			// that ticket so it cannot strand this source or run stale work later.
			int64 queuedMount = -1;
			int64 queueRevision = 0;
			bool refresh = true;
			bool queued = false;
			bool applying = false;
			bool disposed = false;
			bool pinned = false;
		};
	}
}
