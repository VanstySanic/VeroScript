// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "attribute.vs";
export import "Binding.vs";

namespace VS::Gui
{
	// Keep the widget reference out of the dependency-free evaluation state.
	namespace Internal
	{
		export class NativeEvaluation
		{
		public:
			static VS::Gui::Widget Widget = nullptr;
		};
	}

	export enum class Easing
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut
	};
	export struct AnimationOptions
	{
		double Duration = 0.25;
		double Delay = 0;
		double MaxStep = 0.25;
		Easing Easing = VS::Gui::Easing::Linear;
		bool HoldEnd = false;
	};

	// Each widget owns its caches. Snapshot writes are outputs, not input changes.
	namespace Internal
	{
		export class NativeAttributeCache<T>
		{
		public:
			void Set(std::attribute<T> value)
			{
				bool nextConfigured = value.is_set();
				bool nextBound = value.is_bound();
				std::attribute<T> previous = configuration;
				configured = nextConfigured;
				bound = nextBound;
				epoch = 0;
				// Retain the previous payload until the complete replacement is
				// visible. Its destructor may read or replace this cache again.
				configuration = value;
				previous.reset();
			}

			void Reset()
			{
				if (configured || bound)
				{
					Set(std::attribute<T>());
				}
			}

			bool IsSet() const
			{
				return configured;
			}

			optional<T> Read(int64 currentEpoch, bool safe) const
			{
				if (!configured)
				{
					return optional<T>();
				}
				if (epoch == currentEpoch)
				{
					return snapshot;
				}
				if (!safe && bound)
				{
					if (!committed.has_value())
					{
						throw "GUI binding has not been initialized";
					}
					return committed;
				}
				assert(!reading, "Recursive GUI property getter");
				try
				{
					reading = true;
					snapshot = configuration.try_get();
					if (snapshot.has_value())
					{
						committed = snapshot;
					}
					epoch = currentEpoch;
					return snapshot;
				}
				finally
				{
					reading = false;
				}
			}

			void Remember(T value) const
			{
				committed = optional<T>(value);
			}

		private:
			mutable optional<T> committed;
			std::attribute<T> configuration;
			bool configured;
			bool bound;
			mutable optional<T> snapshot;
			mutable int64 epoch;
			mutable bool reading;
		};

		// Unbound string snapshots can release their storage without changing generic value lifetimes.
		export void DiscardNativeStringSnapshot<T>(ref T value)
		{
		}
		export void DiscardNativeStringSnapshot(ref string value)
		{
			value = "";
		}

		// Builtin native values use direct snapshots; temporary handles only initialize typed storage.
		export class NativeValueAttributeCache<T>
		{
		public:
			void Set(std::attribute<T> value)
			{
				bool nextConfigured = value.is_set();
				bool nextBound = value.is_bound();
				std::attribute<T> previous = configuration;
				Binding<T> previousReader = sharedReader;
				sharedReader = nullptr;
				configured = nextConfigured;
				bound = nextBound;
				epoch = 0;
				configuration = value;
				if (!nextConfigured)
				{
					VS::Gui::Internal::DiscardNativeStringSnapshot(ref snapshotValue);
				}
				previous.reset();
			}
			void Reset()
			{
				if (configured || bound)
				{
					Set(std::attribute<T>());
				}
			}
			void SetSharedReader(std::attribute<T> expected, Binding<T> value)
			{
				// A previous payload destructor may have installed another source.
				if (configuration.has_same_configuration(ref expected))
				{
					sharedHasValue = value.VsguiAlwaysHasValue();
					sharedReader = value;
				}
			}
			bool IsSet() const
			{
				return configured;
			}

			optional<T> Read(int64 currentEpoch, bool safe) const
			{
				if (!configured)
				{
					return optional<T>();
				}
				if (epoch == currentEpoch)
				{
					if (hasSnapshot)
					{
						return optional<T>(snapshotValue);
					}
					return optional<T>();
				}
				if (!safe && bound)
				{
					if (!hasCommitted)
					{
						throw "GUI binding has not been initialized";
					}
					return optional<T>(committedValue);
				}
				T output = committedValue;
				if (Read(currentEpoch, safe, ref output))
				{
					return optional<T>(output);
				}
				return optional<T>();
			}

			bool Read(int64 currentEpoch, bool safe, ref T output) const
			{
				if (!configured)
				{
					return false;
				}
				if (epoch == currentEpoch)
				{
					if (hasSnapshot)
					{
						output = snapshotValue;
					}
					return hasSnapshot;
				}
				if (!safe && bound)
				{
					if (!hasCommitted)
					{
						throw "GUI binding has not been initialized";
					}
					output = committedValue;
					return true;
				}
				assert(!reading, "Recursive GUI property getter");
				int previousDepth = VS::Gui::PropertyEvaluation::Depth;
				try
				{
					reading = true;
					VS::Gui::PropertyEvaluation::Depth = previousDepth + 1;
					bool found = false;
					if (sharedReader == nullptr)
					{
						found = configuration.try_get(ref output);
					}
					else if (sharedHasValue)
					{
						output = sharedReader.VsguiReadValue();
						found = true;
					}
					else
					{
						optional<T> value = sharedReader.VsguiRead();
						found = value.has_value();
						if (found)
						{
							output = value.get();
						}
					}
					if (found)
					{
						snapshotValue = output;
					}
					hasSnapshot = found;
					if (found)
					{
						committedValue = output;
						hasCommitted = true;
					}
					epoch = currentEpoch;
					return found;
				}
				finally
				{
					VS::Gui::PropertyEvaluation::Depth = previousDepth;
					reading = false;
				}
			}

			void Remember(T value) const
			{
				committedValue = value;
				hasCommitted = true;
			}

		private:
			std::attribute<T> configuration;
			Binding<T> sharedReader = nullptr;
			bool sharedHasValue = false;
			bool configured;
			bool bound;
			mutable T snapshotValue = std::shared<T>().value;
			mutable T committedValue = std::shared<T>().value;
			mutable bool hasSnapshot;
			mutable bool hasCommitted;
			mutable int64 epoch;
			mutable bool reading;
		};
	}

	export class Subscription
	{
	public:
		bool IsConnected() const
		{
			if (!connected)
			{
				return false;
			}
			if (connectedQuery.has_value())
			{
				fn() -> bool query = connectedQuery.get();
				return query();
			}
			return true;
		}

		void Disconnect()
		{
			if (!connected)
			{
				return;
			}
			optional < fn() -> void > previous = disconnect;
			bool completed = false;
			try
			{
				connected = false;
				disconnect = optional < fn() -> void > ();
				if (previous.has_value())
				{
					fn() -> void callback = previous.get();
					callback();
				}
				completed = true;
			}
			finally
			{
				if (!completed)
				{
					if (!disconnect.has_value())
					{
						disconnect = previous;
					}
					connected = true;
				}
			}
		}

		void VsguiSetDisconnect(fn() -> void callback)
		{
			disconnect = optional < fn() -> void > (callback);
		}

		void VsguiSetQuery(fn() -> bool query)
		{
			connectedQuery = optional < fn() -> bool > (query);
		}

	private:
		bool connected = true;
		optional < fn() -> void > disconnect;
		optional < fn() -> bool > connectedQuery;
	};

	export class __BindingScope
	{
	public:
		__BindingScope()
		{
			elements = Elements();
		}

		Elements elements;

		virtual void Synchronize()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI synchronization is not allowed in a getter");
			VS::Gui::PropertyEvaluation::Epoch++;
		}
	};

	export class Construction
	{
	public:
		static int Depth = 0;
		static bool Failed = false;
		static array<weak<Widget>> Pending;
		static void Begin()
		{
			VS::Gui::Construction::Depth++;
		}
		static void Complete(Widget value)
		{
			array<weak<Widget>> pending = VS::Gui::Construction::Pending;
			pending.push(weak<Widget>(value));
		}
		static void End(bool success)
		{
			if (!success)
			{
				VS::Gui::Construction::Failed = true;
			}
			VS::Gui::Construction::Depth--;
			if (VS::Gui::Construction::Depth != 0)
			{
				return;
			}
			array<weak<Widget>> pending = VS::Gui::Construction::Pending;
			VS::Gui::Construction::Pending = array<weak<Widget>>();
			bool failed = VS::Gui::Construction::Failed;
			VS::Gui::Construction::Failed = false;
			if (failed)
			{
				return;
			}
			map<int64, bool> identities = map<int64, bool>();
			for (weak<Widget> observer : pending)
			{
				Widget widget = observer.lock();
				if (widget != nullptr)
				{
					identities[widget.__GetNativeHandle().GetIdentity()] = true;
				}
			}
			for (weak<Widget> observer : pending)
			{
				Widget widget = observer.lock();
				if (widget == nullptr)
				{
					continue;
				}
				Widget ancestor = widget.GetParent();
				bool covered = false;
				while (ancestor != nullptr)
				{
					if (identities.has(ancestor.__GetNativeHandle().GetIdentity()))
					{
						covered = true;
						break;
					}
					ancestor = ancestor.GetParent();
				}
				if (!covered)
				{
					widget.Initialize();
				}
			}
		}
	};

	// One holder preserves snapshot identity while allocating only used caches.
	// Cache creation is an output detail, not a change to widget configuration.
	// Initialized, unconfigured reads can query native values without a cache.
	namespace Internal
	{
		export class NativeAttributeCaches
		{
		public:
			mutable VS::Gui::Internal::NativeValueAttributeCache<Length> bindingWidth = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Length> bindingHeight = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Insets> bindingPadding = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingGap = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Layout> bindingLayout = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Visibility> bindingVisibility = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<HitTestMode> bindingHitTest = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Cursor> bindingCursor = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<bool> bindingEnabled = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<bool> bindingFocusable = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingOpacity = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Color> bindingBackground = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingText = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingImage = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingFontFamily = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingFontSize = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingFontWeight = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingFontStyle = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Color> bindingTextColor = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<Color> bindingBorderColor = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingTextAlign = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingWhiteSpace = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingTextOverflow = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingLineHeight = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingLetterSpacing = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingDirection = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingLanguage = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingCornerRadius = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<double> bindingBorderWidth = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<bool> bindingClipContent = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingFilter = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingBackdropFilter = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingBackgroundImage = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingMaskImage = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingBoxShadow = nullptr;
			mutable VS::Gui::Internal::NativeValueAttributeCache<string> bindingTransform = nullptr;
		};
	}

	export class Widget : public __BindingScope
	{
	public:
		void AnimateWidth(Length value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimateWidth(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimateWidth(Length value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimateWidth(value, options);
		}
		void StopWidthAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopWidthAnimation();
		}
		Length GetPresentedWidth() const
		{
			return handle.GetPresentedWidth();
		}

		void AnimateHeight(Length value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimateHeight(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimateHeight(Length value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimateHeight(value, options);
		}
		void StopHeightAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopHeightAnimation();
		}
		Length GetPresentedHeight() const
		{
			return handle.GetPresentedHeight();
		}

		void AnimatePadding(Insets value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimatePadding(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimatePadding(Insets value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimatePadding(value, options);
		}
		void StopPaddingAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopPaddingAnimation();
		}
		Insets GetPresentedPadding() const
		{
			return handle.GetPresentedPadding();
		}

		void AnimateGap(double value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimateGap(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimateGap(double value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimateGap(value, options);
		}
		void StopGapAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopGapAnimation();
		}
		double GetPresentedGap() const
		{
			return handle.GetPresentedGap();
		}

		void AnimateOpacity(double value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimateOpacity(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimateOpacity(double value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimateOpacity(value, options);
		}
		void StopOpacityAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopOpacityAnimation();
		}
		double GetPresentedOpacity() const
		{
			return handle.GetPresentedOpacity();
		}

		void AnimateBackground(Color value, AnimationOptions options)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI animation is not allowed in a getter");
			Initialize();
			handle.AnimateBackground(value, options.Duration, options.Delay, options.MaxStep, cast<int>(options.Easing), options.HoldEnd);
		}
		void AnimateBackground(Color value, double duration)
		{
			auto options = VS::Gui::AnimationOptions();
			options.Duration = duration;
			AnimateBackground(value, options);
		}
		void StopBackgroundAnimation()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.StopBackgroundAnimation();
		}
		Color GetPresentedBackground() const
		{
			return handle.GetPresentedBackground();
		}
		void VsguiPaint(VS::Gui::PaintContext canvas)
		{
			OnPaint(canvas);
		}

		Widget()
		{
			handle = VS::Gui::__NativeWidget();
			VsguiRegisterView(true);
			elements.VsguiBind(this);
		}

		Widget(__NativeWidget value)
		{
			handle = value;
			VsguiRegisterView(false);
			elements.VsguiBind(this);
		}

		static Widget VsguiWrap(__NativeWidget value)
		{
			if (value == nullptr)
			{
				return nullptr;
			}
			int64 identity = value.GetIdentity();
			map<int64, weak<Widget>> views = VS::Gui::Widget::VsguiViews;
			if (views.has(identity))
			{
				Widget found = views[identity].lock();
				if (found != nullptr)
				{
					return found;
				}
			}
			return Widget(value);
		}

		Length GetWidth() const
		{
			if (nativeAttributes.bindingWidth == nullptr)
			{
				if (initialized || synchronized) return handle.GetWidth();
				nativeAttributes.bindingWidth = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			if (!nativeAttributes.bindingWidth.IsSet())
			{
				Length value = handle.GetWidth();
				nativeAttributes.bindingWidth.Remember(value);
				return value;
			}
			optional<Length> current = optional<Length>();
			current = nativeAttributes.bindingWidth.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingWidth.IsSet())
			{
				Length fallback = handle.GetFallbackWidth();
				nativeAttributes.bindingWidth.Remember(fallback);
				return fallback;
			}
			Length value = handle.GetWidth();
			nativeAttributes.bindingWidth.Remember(value);
			return value;
		}

		void SetWidth(Length value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingWidth == nullptr)
			{
				nativeAttributes.bindingWidth = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			VsguiClearNotifiedBinding("Width");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingWidth != nullptr)
			{
				nativeAttributes.bindingWidth.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1i64;
			nativePollingMask = nativePollingMask & ~1i64;
			handle.SetWidth(value);
			nativeAttributes.bindingWidth.Remember(value);
		}

		void SetWidth(std::attribute<Length> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingWidth == nullptr)
			{
				nativeAttributes.bindingWidth = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			bool notified = VsguiClearNotifiedBinding("Width");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingWidth.IsSet())
			{
				nativeAttributes.bindingWidth.Remember(handle.GetWidth());
			}
			nativeAttributes.bindingWidth.Set(value);
			nativeAttributeMask = nativeAttributeMask | 1i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 1i64 : nativePollingMask & ~1i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearWidth();
			}
		}

		void SetWidth(Binding<Length> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Length>("Width", value, [owner, value](std::attribute<Length> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetWidth(configuration);
				widget.nativeAttributes.bindingWidth.SetSharedReader(configuration, value);
			});
		}

		void ClearWidth()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Width");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingWidth != nullptr)
			{
				nativeAttributes.bindingWidth.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1i64;
			nativePollingMask = nativePollingMask & ~1i64;
			handle.ClearWidth();
		}

		Length GetHeight() const
		{
			if (nativeAttributes.bindingHeight == nullptr)
			{
				if (initialized || synchronized) return handle.GetHeight();
				nativeAttributes.bindingHeight = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			if (!nativeAttributes.bindingHeight.IsSet())
			{
				Length value = handle.GetHeight();
				nativeAttributes.bindingHeight.Remember(value);
				return value;
			}
			optional<Length> current = optional<Length>();
			current = nativeAttributes.bindingHeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingHeight.IsSet())
			{
				Length fallback = handle.GetFallbackHeight();
				nativeAttributes.bindingHeight.Remember(fallback);
				return fallback;
			}
			Length value = handle.GetHeight();
			nativeAttributes.bindingHeight.Remember(value);
			return value;
		}

		void SetHeight(Length value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingHeight == nullptr)
			{
				nativeAttributes.bindingHeight = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			VsguiClearNotifiedBinding("Height");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingHeight != nullptr)
			{
				nativeAttributes.bindingHeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2i64;
			nativePollingMask = nativePollingMask & ~2i64;
			handle.SetHeight(value);
			nativeAttributes.bindingHeight.Remember(value);
		}

		void SetHeight(std::attribute<Length> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingHeight == nullptr)
			{
				nativeAttributes.bindingHeight = VS::Gui::Internal::NativeValueAttributeCache<Length>();
			}
			bool notified = VsguiClearNotifiedBinding("Height");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingHeight.IsSet())
			{
				nativeAttributes.bindingHeight.Remember(handle.GetHeight());
			}
			nativeAttributes.bindingHeight.Set(value);
			nativeAttributeMask = nativeAttributeMask | 2i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 2i64 : nativePollingMask & ~2i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearHeight();
			}
		}

		void SetHeight(Binding<Length> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Length>("Height", value, [owner, value](std::attribute<Length> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetHeight(configuration);
				widget.nativeAttributes.bindingHeight.SetSharedReader(configuration, value);
			});
		}

		void ClearHeight()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Height");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingHeight != nullptr)
			{
				nativeAttributes.bindingHeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2i64;
			nativePollingMask = nativePollingMask & ~2i64;
			handle.ClearHeight();
		}

		Insets GetPadding() const
		{
			if (nativeAttributes.bindingPadding == nullptr)
			{
				if (initialized || synchronized) return handle.GetPadding();
				nativeAttributes.bindingPadding = VS::Gui::Internal::NativeValueAttributeCache<Insets>();
			}
			if (!nativeAttributes.bindingPadding.IsSet())
			{
				Insets value = handle.GetPadding();
				nativeAttributes.bindingPadding.Remember(value);
				return value;
			}
			optional<Insets> current = optional<Insets>();
			current = nativeAttributes.bindingPadding.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingPadding.IsSet())
			{
				Insets fallback = handle.GetFallbackPadding();
				nativeAttributes.bindingPadding.Remember(fallback);
				return fallback;
			}
			Insets value = handle.GetPadding();
			nativeAttributes.bindingPadding.Remember(value);
			return value;
		}

		void SetPadding(Insets value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingPadding == nullptr)
			{
				nativeAttributes.bindingPadding = VS::Gui::Internal::NativeValueAttributeCache<Insets>();
			}
			VsguiClearNotifiedBinding("Padding");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingPadding != nullptr)
			{
				nativeAttributes.bindingPadding.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4i64;
			nativePollingMask = nativePollingMask & ~4i64;
			handle.SetPadding(value);
			nativeAttributes.bindingPadding.Remember(value);
		}

		void SetPadding(std::attribute<Insets> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingPadding == nullptr)
			{
				nativeAttributes.bindingPadding = VS::Gui::Internal::NativeValueAttributeCache<Insets>();
			}
			bool notified = VsguiClearNotifiedBinding("Padding");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingPadding.IsSet())
			{
				nativeAttributes.bindingPadding.Remember(handle.GetPadding());
			}
			nativeAttributes.bindingPadding.Set(value);
			nativeAttributeMask = nativeAttributeMask | 4i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 4i64 : nativePollingMask & ~4i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearPadding();
			}
		}

		void SetPadding(Binding<Insets> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Insets>("Padding", value, [owner, value](std::attribute<Insets> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetPadding(configuration);
				widget.nativeAttributes.bindingPadding.SetSharedReader(configuration, value);
			});
		}

		void ClearPadding()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Padding");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingPadding != nullptr)
			{
				nativeAttributes.bindingPadding.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4i64;
			nativePollingMask = nativePollingMask & ~4i64;
			handle.ClearPadding();
		}

		double GetGap() const
		{
			if (nativeAttributes.bindingGap == nullptr)
			{
				if (initialized || synchronized) return handle.GetGap();
				nativeAttributes.bindingGap = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingGap.IsSet())
			{
				double value = handle.GetGap();
				nativeAttributes.bindingGap.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingGap.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingGap.IsSet())
			{
				double fallback = handle.GetFallbackGap();
				nativeAttributes.bindingGap.Remember(fallback);
				return fallback;
			}
			double value = handle.GetGap();
			nativeAttributes.bindingGap.Remember(value);
			return value;
		}

		void SetGap(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingGap == nullptr)
			{
				nativeAttributes.bindingGap = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("Gap");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingGap != nullptr)
			{
				nativeAttributes.bindingGap.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8i64;
			nativePollingMask = nativePollingMask & ~8i64;
			handle.SetGap(value);
			nativeAttributes.bindingGap.Remember(value);
		}

		void SetGap(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingGap == nullptr)
			{
				nativeAttributes.bindingGap = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("Gap");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingGap.IsSet())
			{
				nativeAttributes.bindingGap.Remember(handle.GetGap());
			}
			nativeAttributes.bindingGap.Set(value);
			nativeAttributeMask = nativeAttributeMask | 8i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 8i64 : nativePollingMask & ~8i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearGap();
			}
		}

		void SetGap(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("Gap", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetGap(configuration);
				widget.nativeAttributes.bindingGap.SetSharedReader(configuration, value);
			});
		}

		void ClearGap()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Gap");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingGap != nullptr)
			{
				nativeAttributes.bindingGap.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8i64;
			nativePollingMask = nativePollingMask & ~8i64;
			handle.ClearGap();
		}

		Layout GetLayout() const
		{
			if (nativeAttributes.bindingLayout == nullptr)
			{
				if (initialized || synchronized) return handle.GetLayout();
				nativeAttributes.bindingLayout = VS::Gui::Internal::NativeValueAttributeCache<Layout>();
			}
			if (!nativeAttributes.bindingLayout.IsSet())
			{
				Layout value = handle.GetLayout();
				nativeAttributes.bindingLayout.Remember(value);
				return value;
			}
			optional<Layout> current = optional<Layout>();
			current = nativeAttributes.bindingLayout.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingLayout.IsSet())
			{
				Layout fallback = handle.GetFallbackLayout();
				nativeAttributes.bindingLayout.Remember(fallback);
				return fallback;
			}
			Layout value = handle.GetLayout();
			nativeAttributes.bindingLayout.Remember(value);
			return value;
		}

		void SetLayout(Layout value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLayout == nullptr)
			{
				nativeAttributes.bindingLayout = VS::Gui::Internal::NativeValueAttributeCache<Layout>();
			}
			VsguiClearNotifiedBinding("Layout");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLayout != nullptr)
			{
				nativeAttributes.bindingLayout.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16i64;
			nativePollingMask = nativePollingMask & ~16i64;
			handle.SetLayout(value);
			nativeAttributes.bindingLayout.Remember(value);
		}

		void SetLayout(std::attribute<Layout> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLayout == nullptr)
			{
				nativeAttributes.bindingLayout = VS::Gui::Internal::NativeValueAttributeCache<Layout>();
			}
			bool notified = VsguiClearNotifiedBinding("Layout");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingLayout.IsSet())
			{
				nativeAttributes.bindingLayout.Remember(handle.GetLayout());
			}
			nativeAttributes.bindingLayout.Set(value);
			nativeAttributeMask = nativeAttributeMask | 16i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 16i64 : nativePollingMask & ~16i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearLayout();
			}
		}

		void SetLayout(Binding<Layout> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Layout>("Layout", value, [owner, value](std::attribute<Layout> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetLayout(configuration);
				widget.nativeAttributes.bindingLayout.SetSharedReader(configuration, value);
			});
		}

		void ClearLayout()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Layout");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLayout != nullptr)
			{
				nativeAttributes.bindingLayout.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16i64;
			nativePollingMask = nativePollingMask & ~16i64;
			handle.ClearLayout();
		}

		Visibility GetVisibility() const
		{
			if (nativeAttributes.bindingVisibility == nullptr)
			{
				if (initialized || synchronized) return handle.GetVisibility();
				nativeAttributes.bindingVisibility = VS::Gui::Internal::NativeValueAttributeCache<Visibility>();
			}
			if (!nativeAttributes.bindingVisibility.IsSet())
			{
				Visibility value = handle.GetVisibility();
				nativeAttributes.bindingVisibility.Remember(value);
				return value;
			}
			optional<Visibility> current = optional<Visibility>();
			current = nativeAttributes.bindingVisibility.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingVisibility.IsSet())
			{
				Visibility fallback = handle.GetFallbackVisibility();
				nativeAttributes.bindingVisibility.Remember(fallback);
				return fallback;
			}
			Visibility value = handle.GetVisibility();
			nativeAttributes.bindingVisibility.Remember(value);
			return value;
		}

		void SetVisibility(Visibility value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingVisibility == nullptr)
			{
				nativeAttributes.bindingVisibility = VS::Gui::Internal::NativeValueAttributeCache<Visibility>();
			}
			VsguiClearNotifiedBinding("Visibility");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingVisibility != nullptr)
			{
				nativeAttributes.bindingVisibility.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~32i64;
			nativePollingMask = nativePollingMask & ~32i64;
			handle.SetVisibility(value);
			nativeAttributes.bindingVisibility.Remember(value);
		}

		void SetVisibility(std::attribute<Visibility> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingVisibility == nullptr)
			{
				nativeAttributes.bindingVisibility = VS::Gui::Internal::NativeValueAttributeCache<Visibility>();
			}
			bool notified = VsguiClearNotifiedBinding("Visibility");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingVisibility.IsSet())
			{
				nativeAttributes.bindingVisibility.Remember(handle.GetVisibility());
			}
			nativeAttributes.bindingVisibility.Set(value);
			nativeAttributeMask = nativeAttributeMask | 32i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 32i64 : nativePollingMask & ~32i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearVisibility();
			}
		}

		void SetVisibility(Binding<Visibility> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Visibility>("Visibility", value, [owner, value](std::attribute<Visibility> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetVisibility(configuration);
				widget.nativeAttributes.bindingVisibility.SetSharedReader(configuration, value);
			});
		}

		void ClearVisibility()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Visibility");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingVisibility != nullptr)
			{
				nativeAttributes.bindingVisibility.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~32i64;
			nativePollingMask = nativePollingMask & ~32i64;
			handle.ClearVisibility();
		}

		HitTestMode GetHitTest() const
		{
			if (nativeAttributes.bindingHitTest == nullptr)
			{
				if (initialized || synchronized) return handle.GetHitTest();
				nativeAttributes.bindingHitTest = VS::Gui::Internal::NativeValueAttributeCache<HitTestMode>();
			}
			if (!nativeAttributes.bindingHitTest.IsSet())
			{
				HitTestMode value = handle.GetHitTest();
				nativeAttributes.bindingHitTest.Remember(value);
				return value;
			}
			optional<HitTestMode> current = optional<HitTestMode>();
			current = nativeAttributes.bindingHitTest.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingHitTest.IsSet())
			{
				HitTestMode fallback = handle.GetFallbackHitTest();
				nativeAttributes.bindingHitTest.Remember(fallback);
				return fallback;
			}
			HitTestMode value = handle.GetHitTest();
			nativeAttributes.bindingHitTest.Remember(value);
			return value;
		}

		void SetHitTest(HitTestMode value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingHitTest == nullptr)
			{
				nativeAttributes.bindingHitTest = VS::Gui::Internal::NativeValueAttributeCache<HitTestMode>();
			}
			VsguiClearNotifiedBinding("HitTest");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingHitTest != nullptr)
			{
				nativeAttributes.bindingHitTest.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~64i64;
			nativePollingMask = nativePollingMask & ~64i64;
			handle.SetHitTest(value);
			nativeAttributes.bindingHitTest.Remember(value);
		}

		void SetHitTest(std::attribute<HitTestMode> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingHitTest == nullptr)
			{
				nativeAttributes.bindingHitTest = VS::Gui::Internal::NativeValueAttributeCache<HitTestMode>();
			}
			bool notified = VsguiClearNotifiedBinding("HitTest");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingHitTest.IsSet())
			{
				nativeAttributes.bindingHitTest.Remember(handle.GetHitTest());
			}
			nativeAttributes.bindingHitTest.Set(value);
			nativeAttributeMask = nativeAttributeMask | 64i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 64i64 : nativePollingMask & ~64i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearHitTest();
			}
		}

		void SetHitTest(Binding<HitTestMode> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<HitTestMode>("HitTest", value, [owner, value](std::attribute<HitTestMode> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetHitTest(configuration);
				widget.nativeAttributes.bindingHitTest.SetSharedReader(configuration, value);
			});
		}

		void ClearHitTest()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("HitTest");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingHitTest != nullptr)
			{
				nativeAttributes.bindingHitTest.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~64i64;
			nativePollingMask = nativePollingMask & ~64i64;
			handle.ClearHitTest();
		}

		Cursor GetCursor() const
		{
			if (nativeAttributes.bindingCursor == nullptr)
			{
				if (initialized || synchronized) return handle.GetCursor();
				nativeAttributes.bindingCursor = VS::Gui::Internal::NativeValueAttributeCache<Cursor>();
			}
			if (!nativeAttributes.bindingCursor.IsSet())
			{
				Cursor value = handle.GetCursor();
				nativeAttributes.bindingCursor.Remember(value);
				return value;
			}
			optional<Cursor> current = optional<Cursor>();
			current = nativeAttributes.bindingCursor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingCursor.IsSet())
			{
				Cursor fallback = handle.GetFallbackCursor();
				nativeAttributes.bindingCursor.Remember(fallback);
				return fallback;
			}
			Cursor value = handle.GetCursor();
			nativeAttributes.bindingCursor.Remember(value);
			return value;
		}

		void SetCursor(Cursor value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingCursor == nullptr)
			{
				nativeAttributes.bindingCursor = VS::Gui::Internal::NativeValueAttributeCache<Cursor>();
			}
			VsguiClearNotifiedBinding("Cursor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingCursor != nullptr)
			{
				nativeAttributes.bindingCursor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~128i64;
			nativePollingMask = nativePollingMask & ~128i64;
			handle.SetCursor(value);
			nativeAttributes.bindingCursor.Remember(value);
		}

		void SetCursor(std::attribute<Cursor> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingCursor == nullptr)
			{
				nativeAttributes.bindingCursor = VS::Gui::Internal::NativeValueAttributeCache<Cursor>();
			}
			bool notified = VsguiClearNotifiedBinding("Cursor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingCursor.IsSet())
			{
				nativeAttributes.bindingCursor.Remember(handle.GetCursor());
			}
			nativeAttributes.bindingCursor.Set(value);
			nativeAttributeMask = nativeAttributeMask | 128i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 128i64 : nativePollingMask & ~128i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearCursor();
			}
		}

		void SetCursor(Binding<Cursor> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Cursor>("Cursor", value, [owner, value](std::attribute<Cursor> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetCursor(configuration);
				widget.nativeAttributes.bindingCursor.SetSharedReader(configuration, value);
			});
		}

		void ClearCursor()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Cursor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingCursor != nullptr)
			{
				nativeAttributes.bindingCursor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~128i64;
			nativePollingMask = nativePollingMask & ~128i64;
			handle.ClearCursor();
		}

		bool GetEnabled() const
		{
			if (nativeAttributes.bindingEnabled == nullptr)
			{
				if (initialized || synchronized) return handle.GetEnabled();
				nativeAttributes.bindingEnabled = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			if (!nativeAttributes.bindingEnabled.IsSet())
			{
				bool value = handle.GetEnabled();
				nativeAttributes.bindingEnabled.Remember(value);
				return value;
			}
			bool current = false;
			bool found = false;
			found = nativeAttributes.bindingEnabled.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingEnabled.IsSet())
			{
				bool fallback = handle.GetFallbackEnabled();
				nativeAttributes.bindingEnabled.Remember(fallback);
				return fallback;
			}
			bool value = handle.GetEnabled();
			nativeAttributes.bindingEnabled.Remember(value);
			return value;
		}

		void SetEnabled(bool value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingEnabled == nullptr)
			{
				nativeAttributes.bindingEnabled = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			VsguiClearNotifiedBinding("Enabled");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingEnabled != nullptr)
			{
				nativeAttributes.bindingEnabled.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~256i64;
			nativePollingMask = nativePollingMask & ~256i64;
			handle.SetEnabled(value);
			nativeAttributes.bindingEnabled.Remember(value);
		}

		void SetEnabled(std::attribute<bool> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingEnabled == nullptr)
			{
				nativeAttributes.bindingEnabled = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			bool notified = VsguiClearNotifiedBinding("Enabled");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingEnabled.IsSet())
			{
				nativeAttributes.bindingEnabled.Remember(handle.GetEnabled());
			}
			nativeAttributes.bindingEnabled.Set(value);
			nativeAttributeMask = nativeAttributeMask | 256i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 256i64 : nativePollingMask & ~256i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearEnabled();
			}
		}

		void SetEnabled(Binding<bool> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<bool>("Enabled", value, [owner, value](std::attribute<bool> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetEnabled(configuration);
				widget.nativeAttributes.bindingEnabled.SetSharedReader(configuration, value);
			});
		}

		void ClearEnabled()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Enabled");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingEnabled != nullptr)
			{
				nativeAttributes.bindingEnabled.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~256i64;
			nativePollingMask = nativePollingMask & ~256i64;
			handle.ClearEnabled();
		}

		bool GetFocusable() const
		{
			if (nativeAttributes.bindingFocusable == nullptr)
			{
				if (initialized || synchronized) return handle.GetFocusable();
				nativeAttributes.bindingFocusable = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			if (!nativeAttributes.bindingFocusable.IsSet())
			{
				bool value = handle.GetFocusable();
				nativeAttributes.bindingFocusable.Remember(value);
				return value;
			}
			bool current = false;
			bool found = false;
			found = nativeAttributes.bindingFocusable.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFocusable.IsSet())
			{
				bool fallback = handle.GetFallbackFocusable();
				nativeAttributes.bindingFocusable.Remember(fallback);
				return fallback;
			}
			bool value = handle.GetFocusable();
			nativeAttributes.bindingFocusable.Remember(value);
			return value;
		}

		void SetFocusable(bool value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFocusable == nullptr)
			{
				nativeAttributes.bindingFocusable = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			VsguiClearNotifiedBinding("Focusable");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFocusable != nullptr)
			{
				nativeAttributes.bindingFocusable.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~512i64;
			nativePollingMask = nativePollingMask & ~512i64;
			handle.SetFocusable(value);
			nativeAttributes.bindingFocusable.Remember(value);
		}

		void SetFocusable(std::attribute<bool> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFocusable == nullptr)
			{
				nativeAttributes.bindingFocusable = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			bool notified = VsguiClearNotifiedBinding("Focusable");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFocusable.IsSet())
			{
				nativeAttributes.bindingFocusable.Remember(handle.GetFocusable());
			}
			nativeAttributes.bindingFocusable.Set(value);
			nativeAttributeMask = nativeAttributeMask | 512i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 512i64 : nativePollingMask & ~512i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFocusable();
			}
		}

		void SetFocusable(Binding<bool> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<bool>("Focusable", value, [owner, value](std::attribute<bool> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFocusable(configuration);
				widget.nativeAttributes.bindingFocusable.SetSharedReader(configuration, value);
			});
		}

		void ClearFocusable()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Focusable");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFocusable != nullptr)
			{
				nativeAttributes.bindingFocusable.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~512i64;
			nativePollingMask = nativePollingMask & ~512i64;
			handle.ClearFocusable();
		}

		double GetOpacity() const
		{
			if (nativeAttributes.bindingOpacity == nullptr)
			{
				if (initialized || synchronized) return handle.GetOpacity();
				nativeAttributes.bindingOpacity = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingOpacity.IsSet())
			{
				double value = handle.GetOpacity();
				nativeAttributes.bindingOpacity.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingOpacity.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingOpacity.IsSet())
			{
				double fallback = handle.GetFallbackOpacity();
				nativeAttributes.bindingOpacity.Remember(fallback);
				return fallback;
			}
			double value = handle.GetOpacity();
			nativeAttributes.bindingOpacity.Remember(value);
			return value;
		}

		void SetOpacity(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingOpacity == nullptr)
			{
				nativeAttributes.bindingOpacity = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("Opacity");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingOpacity != nullptr)
			{
				nativeAttributes.bindingOpacity.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1024i64;
			nativePollingMask = nativePollingMask & ~1024i64;
			handle.SetOpacity(value);
			nativeAttributes.bindingOpacity.Remember(value);
		}

		void SetOpacity(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingOpacity == nullptr)
			{
				nativeAttributes.bindingOpacity = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("Opacity");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingOpacity.IsSet())
			{
				nativeAttributes.bindingOpacity.Remember(handle.GetOpacity());
			}
			nativeAttributes.bindingOpacity.Set(value);
			nativeAttributeMask = nativeAttributeMask | 1024i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 1024i64 : nativePollingMask & ~1024i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearOpacity();
			}
		}

		void SetOpacity(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("Opacity", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetOpacity(configuration);
				widget.nativeAttributes.bindingOpacity.SetSharedReader(configuration, value);
			});
		}

		void ClearOpacity()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Opacity");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingOpacity != nullptr)
			{
				nativeAttributes.bindingOpacity.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1024i64;
			nativePollingMask = nativePollingMask & ~1024i64;
			handle.ClearOpacity();
		}

		Color GetBackground() const
		{
			if (nativeAttributes.bindingBackground == nullptr)
			{
				if (initialized || synchronized) return handle.GetBackground();
				nativeAttributes.bindingBackground = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			if (!nativeAttributes.bindingBackground.IsSet())
			{
				Color value = handle.GetBackground();
				nativeAttributes.bindingBackground.Remember(value);
				return value;
			}
			optional<Color> current = optional<Color>();
			current = nativeAttributes.bindingBackground.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingBackground.IsSet())
			{
				Color fallback = handle.GetFallbackBackground();
				nativeAttributes.bindingBackground.Remember(fallback);
				return fallback;
			}
			Color value = handle.GetBackground();
			nativeAttributes.bindingBackground.Remember(value);
			return value;
		}

		void SetBackground(Color value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackground == nullptr)
			{
				nativeAttributes.bindingBackground = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			VsguiClearNotifiedBinding("Background");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackground != nullptr)
			{
				nativeAttributes.bindingBackground.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2048i64;
			nativePollingMask = nativePollingMask & ~2048i64;
			handle.SetBackground(value);
			nativeAttributes.bindingBackground.Remember(value);
		}

		void SetBackground(std::attribute<Color> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackground == nullptr)
			{
				nativeAttributes.bindingBackground = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			bool notified = VsguiClearNotifiedBinding("Background");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBackground.IsSet())
			{
				nativeAttributes.bindingBackground.Remember(handle.GetBackground());
			}
			nativeAttributes.bindingBackground.Set(value);
			nativeAttributeMask = nativeAttributeMask | 2048i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 2048i64 : nativePollingMask & ~2048i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBackground();
			}
		}

		void SetBackground(Binding<Color> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Color>("Background", value, [owner, value](std::attribute<Color> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBackground(configuration);
				widget.nativeAttributes.bindingBackground.SetSharedReader(configuration, value);
			});
		}

		void ClearBackground()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Background");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackground != nullptr)
			{
				nativeAttributes.bindingBackground.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2048i64;
			nativePollingMask = nativePollingMask & ~2048i64;
			handle.ClearBackground();
		}

		string GetText() const
		{
			if (nativeAttributes.bindingText == nullptr)
			{
				if (initialized || synchronized) return handle.GetText();
				nativeAttributes.bindingText = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingText.IsSet())
			{
				string value = handle.GetText();
				nativeAttributes.bindingText.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingText.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingText.IsSet())
			{
				string fallback = handle.GetFallbackText();
				nativeAttributes.bindingText.Remember(fallback);
				return fallback;
			}
			string value = handle.GetText();
			nativeAttributes.bindingText.Remember(value);
			return value;
		}

		void SetText(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingText == nullptr)
			{
				nativeAttributes.bindingText = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Text");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingText != nullptr)
			{
				nativeAttributes.bindingText.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4096i64;
			nativePollingMask = nativePollingMask & ~4096i64;
			handle.SetText(value);
			nativeAttributes.bindingText.Remember(value);
		}

		void SetText(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingText == nullptr)
			{
				nativeAttributes.bindingText = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Text");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingText.IsSet())
			{
				nativeAttributes.bindingText.Remember(handle.GetText());
			}
			nativeAttributes.bindingText.Set(value);
			nativeAttributeMask = nativeAttributeMask | 4096i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 4096i64 : nativePollingMask & ~4096i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearText();
			}
		}

		void SetText(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Text", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetText(configuration);
				widget.nativeAttributes.bindingText.SetSharedReader(configuration, value);
			});
		}

		void ClearText()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Text");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingText != nullptr)
			{
				nativeAttributes.bindingText.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4096i64;
			nativePollingMask = nativePollingMask & ~4096i64;
			handle.ClearText();
		}

		string GetImage() const
		{
			if (nativeAttributes.bindingImage == nullptr)
			{
				if (initialized || synchronized) return handle.GetImage();
				nativeAttributes.bindingImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingImage.IsSet())
			{
				string value = handle.GetImage();
				nativeAttributes.bindingImage.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return VS::Gui::__NativeResources::ResolveFrom(imageRequester, current);
			}
			if (nativeAttributes.bindingImage.IsSet())
			{
				string fallback = handle.GetFallbackImage();
				nativeAttributes.bindingImage.Remember(fallback);
				return fallback;
			}
			string value = handle.GetImage();
			nativeAttributes.bindingImage.Remember(value);
			return value;
		}

		void SetImage(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingImage == nullptr)
			{
				nativeAttributes.bindingImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Image");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingImage != nullptr)
			{
				nativeAttributes.bindingImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8192i64;
			nativePollingMask = nativePollingMask & ~8192i64;
			handle.SetImage(value == "" ? "" : VS::Gui::__NativeResources::Resolve(value));
			nativeAttributes.bindingImage.Remember(value);
		}

		void SetImage(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingImage == nullptr)
			{
				nativeAttributes.bindingImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Image");
			imageRequester = VS::Gui::__NativeResources::CaptureRequester();
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingImage.IsSet())
			{
				nativeAttributes.bindingImage.Remember(handle.GetImage());
			}
			nativeAttributes.bindingImage.Set(value);
			nativeAttributeMask = nativeAttributeMask | 8192i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 8192i64 : nativePollingMask & ~8192i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearImage();
			}
		}

		void SetImage(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Image", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetImage(configuration);
				widget.nativeAttributes.bindingImage.SetSharedReader(configuration, value);
			});
		}

		void ClearImage()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Image");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingImage != nullptr)
			{
				nativeAttributes.bindingImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8192i64;
			nativePollingMask = nativePollingMask & ~8192i64;
			handle.ClearImage();
		}

		string GetFontFamily() const
		{
			if (nativeAttributes.bindingFontFamily == nullptr)
			{
				if (initialized || synchronized) return handle.GetFontFamily();
				nativeAttributes.bindingFontFamily = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingFontFamily.IsSet())
			{
				string value = handle.GetFontFamily();
				nativeAttributes.bindingFontFamily.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingFontFamily.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFontFamily.IsSet())
			{
				string fallback = handle.GetFallbackFontFamily();
				nativeAttributes.bindingFontFamily.Remember(fallback);
				return fallback;
			}
			string value = handle.GetFontFamily();
			nativeAttributes.bindingFontFamily.Remember(value);
			return value;
		}

		void SetFontFamily(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontFamily == nullptr)
			{
				nativeAttributes.bindingFontFamily = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("FontFamily");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontFamily != nullptr)
			{
				nativeAttributes.bindingFontFamily.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16384i64;
			nativePollingMask = nativePollingMask & ~16384i64;
			handle.SetFontFamily(value);
			nativeAttributes.bindingFontFamily.Remember(value);
		}

		void SetFontFamily(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontFamily == nullptr)
			{
				nativeAttributes.bindingFontFamily = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("FontFamily");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFontFamily.IsSet())
			{
				nativeAttributes.bindingFontFamily.Remember(handle.GetFontFamily());
			}
			nativeAttributes.bindingFontFamily.Set(value);
			nativeAttributeMask = nativeAttributeMask | 16384i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 16384i64 : nativePollingMask & ~16384i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFontFamily();
			}
		}

		void SetFontFamily(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("FontFamily", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFontFamily(configuration);
				widget.nativeAttributes.bindingFontFamily.SetSharedReader(configuration, value);
			});
		}

		void ClearFontFamily()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("FontFamily");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontFamily != nullptr)
			{
				nativeAttributes.bindingFontFamily.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16384i64;
			nativePollingMask = nativePollingMask & ~16384i64;
			handle.ClearFontFamily();
		}

		double GetFontSize() const
		{
			if (nativeAttributes.bindingFontSize == nullptr)
			{
				if (initialized || synchronized) return handle.GetFontSize();
				nativeAttributes.bindingFontSize = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingFontSize.IsSet())
			{
				double value = handle.GetFontSize();
				nativeAttributes.bindingFontSize.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingFontSize.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFontSize.IsSet())
			{
				double fallback = handle.GetFallbackFontSize();
				nativeAttributes.bindingFontSize.Remember(fallback);
				return fallback;
			}
			double value = handle.GetFontSize();
			nativeAttributes.bindingFontSize.Remember(value);
			return value;
		}

		void SetFontSize(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontSize == nullptr)
			{
				nativeAttributes.bindingFontSize = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("FontSize");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontSize != nullptr)
			{
				nativeAttributes.bindingFontSize.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~32768i64;
			nativePollingMask = nativePollingMask & ~32768i64;
			handle.SetFontSize(value);
			nativeAttributes.bindingFontSize.Remember(value);
		}

		void SetFontSize(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontSize == nullptr)
			{
				nativeAttributes.bindingFontSize = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("FontSize");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFontSize.IsSet())
			{
				nativeAttributes.bindingFontSize.Remember(handle.GetFontSize());
			}
			nativeAttributes.bindingFontSize.Set(value);
			nativeAttributeMask = nativeAttributeMask | 32768i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 32768i64 : nativePollingMask & ~32768i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFontSize();
			}
		}

		void SetFontSize(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("FontSize", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFontSize(configuration);
				widget.nativeAttributes.bindingFontSize.SetSharedReader(configuration, value);
			});
		}

		void ClearFontSize()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("FontSize");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontSize != nullptr)
			{
				nativeAttributes.bindingFontSize.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~32768i64;
			nativePollingMask = nativePollingMask & ~32768i64;
			handle.ClearFontSize();
		}

		double GetFontWeight() const
		{
			if (nativeAttributes.bindingFontWeight == nullptr)
			{
				if (initialized || synchronized) return handle.GetFontWeight();
				nativeAttributes.bindingFontWeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingFontWeight.IsSet())
			{
				double value = handle.GetFontWeight();
				nativeAttributes.bindingFontWeight.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingFontWeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFontWeight.IsSet())
			{
				double fallback = handle.GetFallbackFontWeight();
				nativeAttributes.bindingFontWeight.Remember(fallback);
				return fallback;
			}
			double value = handle.GetFontWeight();
			nativeAttributes.bindingFontWeight.Remember(value);
			return value;
		}

		void SetFontWeight(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontWeight == nullptr)
			{
				nativeAttributes.bindingFontWeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("FontWeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontWeight != nullptr)
			{
				nativeAttributes.bindingFontWeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~65536i64;
			nativePollingMask = nativePollingMask & ~65536i64;
			handle.SetFontWeight(value);
			nativeAttributes.bindingFontWeight.Remember(value);
		}

		void SetFontWeight(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontWeight == nullptr)
			{
				nativeAttributes.bindingFontWeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("FontWeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFontWeight.IsSet())
			{
				nativeAttributes.bindingFontWeight.Remember(handle.GetFontWeight());
			}
			nativeAttributes.bindingFontWeight.Set(value);
			nativeAttributeMask = nativeAttributeMask | 65536i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 65536i64 : nativePollingMask & ~65536i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFontWeight();
			}
		}

		void SetFontWeight(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("FontWeight", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFontWeight(configuration);
				widget.nativeAttributes.bindingFontWeight.SetSharedReader(configuration, value);
			});
		}

		void ClearFontWeight()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("FontWeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontWeight != nullptr)
			{
				nativeAttributes.bindingFontWeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~65536i64;
			nativePollingMask = nativePollingMask & ~65536i64;
			handle.ClearFontWeight();
		}

		string GetFontStyle() const
		{
			if (nativeAttributes.bindingFontStyle == nullptr)
			{
				if (initialized || synchronized) return handle.GetFontStyle();
				nativeAttributes.bindingFontStyle = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingFontStyle.IsSet())
			{
				string value = handle.GetFontStyle();
				nativeAttributes.bindingFontStyle.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingFontStyle.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFontStyle.IsSet())
			{
				string fallback = handle.GetFallbackFontStyle();
				nativeAttributes.bindingFontStyle.Remember(fallback);
				return fallback;
			}
			string value = handle.GetFontStyle();
			nativeAttributes.bindingFontStyle.Remember(value);
			return value;
		}

		void SetFontStyle(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontStyle == nullptr)
			{
				nativeAttributes.bindingFontStyle = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("FontStyle");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontStyle != nullptr)
			{
				nativeAttributes.bindingFontStyle.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~131072i64;
			nativePollingMask = nativePollingMask & ~131072i64;
			handle.SetFontStyle(value);
			nativeAttributes.bindingFontStyle.Remember(value);
		}

		void SetFontStyle(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFontStyle == nullptr)
			{
				nativeAttributes.bindingFontStyle = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("FontStyle");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFontStyle.IsSet())
			{
				nativeAttributes.bindingFontStyle.Remember(handle.GetFontStyle());
			}
			nativeAttributes.bindingFontStyle.Set(value);
			nativeAttributeMask = nativeAttributeMask | 131072i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 131072i64 : nativePollingMask & ~131072i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFontStyle();
			}
		}

		void SetFontStyle(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("FontStyle", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFontStyle(configuration);
				widget.nativeAttributes.bindingFontStyle.SetSharedReader(configuration, value);
			});
		}

		void ClearFontStyle()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("FontStyle");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFontStyle != nullptr)
			{
				nativeAttributes.bindingFontStyle.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~131072i64;
			nativePollingMask = nativePollingMask & ~131072i64;
			handle.ClearFontStyle();
		}

		Color GetTextColor() const
		{
			if (nativeAttributes.bindingTextColor == nullptr)
			{
				if (initialized || synchronized) return handle.GetTextColor();
				nativeAttributes.bindingTextColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			if (!nativeAttributes.bindingTextColor.IsSet())
			{
				Color value = handle.GetTextColor();
				nativeAttributes.bindingTextColor.Remember(value);
				return value;
			}
			optional<Color> current = optional<Color>();
			current = nativeAttributes.bindingTextColor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingTextColor.IsSet())
			{
				Color fallback = handle.GetFallbackTextColor();
				nativeAttributes.bindingTextColor.Remember(fallback);
				return fallback;
			}
			Color value = handle.GetTextColor();
			nativeAttributes.bindingTextColor.Remember(value);
			return value;
		}

		void SetTextColor(Color value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextColor == nullptr)
			{
				nativeAttributes.bindingTextColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			VsguiClearNotifiedBinding("TextColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextColor != nullptr)
			{
				nativeAttributes.bindingTextColor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~262144i64;
			nativePollingMask = nativePollingMask & ~262144i64;
			handle.SetTextColor(value);
			nativeAttributes.bindingTextColor.Remember(value);
		}

		void SetTextColor(std::attribute<Color> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextColor == nullptr)
			{
				nativeAttributes.bindingTextColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			bool notified = VsguiClearNotifiedBinding("TextColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingTextColor.IsSet())
			{
				nativeAttributes.bindingTextColor.Remember(handle.GetTextColor());
			}
			nativeAttributes.bindingTextColor.Set(value);
			nativeAttributeMask = nativeAttributeMask | 262144i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 262144i64 : nativePollingMask & ~262144i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearTextColor();
			}
		}

		void SetTextColor(Binding<Color> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Color>("TextColor", value, [owner, value](std::attribute<Color> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetTextColor(configuration);
				widget.nativeAttributes.bindingTextColor.SetSharedReader(configuration, value);
			});
		}

		void ClearTextColor()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("TextColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextColor != nullptr)
			{
				nativeAttributes.bindingTextColor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~262144i64;
			nativePollingMask = nativePollingMask & ~262144i64;
			handle.ClearTextColor();
		}

		Color GetBorderColor() const
		{
			if (nativeAttributes.bindingBorderColor == nullptr)
			{
				if (initialized || synchronized) return handle.GetBorderColor();
				nativeAttributes.bindingBorderColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			if (!nativeAttributes.bindingBorderColor.IsSet())
			{
				Color value = handle.GetBorderColor();
				nativeAttributes.bindingBorderColor.Remember(value);
				return value;
			}
			optional<Color> current = optional<Color>();
			current = nativeAttributes.bindingBorderColor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0);
			if (current.has_value())
			{
				return current.get();
			}
			if (nativeAttributes.bindingBorderColor.IsSet())
			{
				Color fallback = handle.GetFallbackBorderColor();
				nativeAttributes.bindingBorderColor.Remember(fallback);
				return fallback;
			}
			Color value = handle.GetBorderColor();
			nativeAttributes.bindingBorderColor.Remember(value);
			return value;
		}

		void SetBorderColor(Color value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBorderColor == nullptr)
			{
				nativeAttributes.bindingBorderColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			VsguiClearNotifiedBinding("BorderColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBorderColor != nullptr)
			{
				nativeAttributes.bindingBorderColor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~524288i64;
			nativePollingMask = nativePollingMask & ~524288i64;
			handle.SetBorderColor(value);
			nativeAttributes.bindingBorderColor.Remember(value);
		}

		void SetBorderColor(std::attribute<Color> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBorderColor == nullptr)
			{
				nativeAttributes.bindingBorderColor = VS::Gui::Internal::NativeValueAttributeCache<Color>();
			}
			bool notified = VsguiClearNotifiedBinding("BorderColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBorderColor.IsSet())
			{
				nativeAttributes.bindingBorderColor.Remember(handle.GetBorderColor());
			}
			nativeAttributes.bindingBorderColor.Set(value);
			nativeAttributeMask = nativeAttributeMask | 524288i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 524288i64 : nativePollingMask & ~524288i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBorderColor();
			}
		}

		void SetBorderColor(Binding<Color> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<Color>("BorderColor", value, [owner, value](std::attribute<Color> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBorderColor(configuration);
				widget.nativeAttributes.bindingBorderColor.SetSharedReader(configuration, value);
			});
		}

		void ClearBorderColor()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("BorderColor");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBorderColor != nullptr)
			{
				nativeAttributes.bindingBorderColor.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~524288i64;
			nativePollingMask = nativePollingMask & ~524288i64;
			handle.ClearBorderColor();
		}

		string GetTextAlign() const
		{
			if (nativeAttributes.bindingTextAlign == nullptr)
			{
				if (initialized || synchronized) return handle.GetTextAlign();
				nativeAttributes.bindingTextAlign = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingTextAlign.IsSet())
			{
				string value = handle.GetTextAlign();
				nativeAttributes.bindingTextAlign.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingTextAlign.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingTextAlign.IsSet())
			{
				string fallback = handle.GetFallbackTextAlign();
				nativeAttributes.bindingTextAlign.Remember(fallback);
				return fallback;
			}
			string value = handle.GetTextAlign();
			nativeAttributes.bindingTextAlign.Remember(value);
			return value;
		}

		void SetTextAlign(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextAlign == nullptr)
			{
				nativeAttributes.bindingTextAlign = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("TextAlign");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextAlign != nullptr)
			{
				nativeAttributes.bindingTextAlign.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1048576i64;
			nativePollingMask = nativePollingMask & ~1048576i64;
			handle.SetTextAlign(value);
			nativeAttributes.bindingTextAlign.Remember(value);
		}

		void SetTextAlign(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextAlign == nullptr)
			{
				nativeAttributes.bindingTextAlign = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("TextAlign");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingTextAlign.IsSet())
			{
				nativeAttributes.bindingTextAlign.Remember(handle.GetTextAlign());
			}
			nativeAttributes.bindingTextAlign.Set(value);
			nativeAttributeMask = nativeAttributeMask | 1048576i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 1048576i64 : nativePollingMask & ~1048576i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearTextAlign();
			}
		}

		void SetTextAlign(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("TextAlign", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetTextAlign(configuration);
				widget.nativeAttributes.bindingTextAlign.SetSharedReader(configuration, value);
			});
		}

		void ClearTextAlign()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("TextAlign");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextAlign != nullptr)
			{
				nativeAttributes.bindingTextAlign.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1048576i64;
			nativePollingMask = nativePollingMask & ~1048576i64;
			handle.ClearTextAlign();
		}

		string GetWhiteSpace() const
		{
			if (nativeAttributes.bindingWhiteSpace == nullptr)
			{
				if (initialized || synchronized) return handle.GetWhiteSpace();
				nativeAttributes.bindingWhiteSpace = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingWhiteSpace.IsSet())
			{
				string value = handle.GetWhiteSpace();
				nativeAttributes.bindingWhiteSpace.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingWhiteSpace.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingWhiteSpace.IsSet())
			{
				string fallback = handle.GetFallbackWhiteSpace();
				nativeAttributes.bindingWhiteSpace.Remember(fallback);
				return fallback;
			}
			string value = handle.GetWhiteSpace();
			nativeAttributes.bindingWhiteSpace.Remember(value);
			return value;
		}

		void SetWhiteSpace(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingWhiteSpace == nullptr)
			{
				nativeAttributes.bindingWhiteSpace = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("WhiteSpace");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingWhiteSpace != nullptr)
			{
				nativeAttributes.bindingWhiteSpace.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2097152i64;
			nativePollingMask = nativePollingMask & ~2097152i64;
			handle.SetWhiteSpace(value);
			nativeAttributes.bindingWhiteSpace.Remember(value);
		}

		void SetWhiteSpace(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingWhiteSpace == nullptr)
			{
				nativeAttributes.bindingWhiteSpace = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("WhiteSpace");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingWhiteSpace.IsSet())
			{
				nativeAttributes.bindingWhiteSpace.Remember(handle.GetWhiteSpace());
			}
			nativeAttributes.bindingWhiteSpace.Set(value);
			nativeAttributeMask = nativeAttributeMask | 2097152i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 2097152i64 : nativePollingMask & ~2097152i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearWhiteSpace();
			}
		}

		void SetWhiteSpace(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("WhiteSpace", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetWhiteSpace(configuration);
				widget.nativeAttributes.bindingWhiteSpace.SetSharedReader(configuration, value);
			});
		}

		void ClearWhiteSpace()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("WhiteSpace");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingWhiteSpace != nullptr)
			{
				nativeAttributes.bindingWhiteSpace.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2097152i64;
			nativePollingMask = nativePollingMask & ~2097152i64;
			handle.ClearWhiteSpace();
		}

		string GetTextOverflow() const
		{
			if (nativeAttributes.bindingTextOverflow == nullptr)
			{
				if (initialized || synchronized) return handle.GetTextOverflow();
				nativeAttributes.bindingTextOverflow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingTextOverflow.IsSet())
			{
				string value = handle.GetTextOverflow();
				nativeAttributes.bindingTextOverflow.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingTextOverflow.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingTextOverflow.IsSet())
			{
				string fallback = handle.GetFallbackTextOverflow();
				nativeAttributes.bindingTextOverflow.Remember(fallback);
				return fallback;
			}
			string value = handle.GetTextOverflow();
			nativeAttributes.bindingTextOverflow.Remember(value);
			return value;
		}

		void SetTextOverflow(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextOverflow == nullptr)
			{
				nativeAttributes.bindingTextOverflow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("TextOverflow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextOverflow != nullptr)
			{
				nativeAttributes.bindingTextOverflow.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4194304i64;
			nativePollingMask = nativePollingMask & ~4194304i64;
			handle.SetTextOverflow(value);
			nativeAttributes.bindingTextOverflow.Remember(value);
		}

		void SetTextOverflow(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTextOverflow == nullptr)
			{
				nativeAttributes.bindingTextOverflow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("TextOverflow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingTextOverflow.IsSet())
			{
				nativeAttributes.bindingTextOverflow.Remember(handle.GetTextOverflow());
			}
			nativeAttributes.bindingTextOverflow.Set(value);
			nativeAttributeMask = nativeAttributeMask | 4194304i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 4194304i64 : nativePollingMask & ~4194304i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearTextOverflow();
			}
		}

		void SetTextOverflow(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("TextOverflow", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetTextOverflow(configuration);
				widget.nativeAttributes.bindingTextOverflow.SetSharedReader(configuration, value);
			});
		}

		void ClearTextOverflow()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("TextOverflow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTextOverflow != nullptr)
			{
				nativeAttributes.bindingTextOverflow.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4194304i64;
			nativePollingMask = nativePollingMask & ~4194304i64;
			handle.ClearTextOverflow();
		}

		double GetLineHeight() const
		{
			if (nativeAttributes.bindingLineHeight == nullptr)
			{
				if (initialized || synchronized) return handle.GetLineHeight();
				nativeAttributes.bindingLineHeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingLineHeight.IsSet())
			{
				double value = handle.GetLineHeight();
				nativeAttributes.bindingLineHeight.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingLineHeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingLineHeight.IsSet())
			{
				double fallback = handle.GetFallbackLineHeight();
				nativeAttributes.bindingLineHeight.Remember(fallback);
				return fallback;
			}
			double value = handle.GetLineHeight();
			nativeAttributes.bindingLineHeight.Remember(value);
			return value;
		}

		void SetLineHeight(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLineHeight == nullptr)
			{
				nativeAttributes.bindingLineHeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("LineHeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLineHeight != nullptr)
			{
				nativeAttributes.bindingLineHeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8388608i64;
			nativePollingMask = nativePollingMask & ~8388608i64;
			handle.SetLineHeight(value);
			nativeAttributes.bindingLineHeight.Remember(value);
		}

		void SetLineHeight(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLineHeight == nullptr)
			{
				nativeAttributes.bindingLineHeight = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("LineHeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingLineHeight.IsSet())
			{
				nativeAttributes.bindingLineHeight.Remember(handle.GetLineHeight());
			}
			nativeAttributes.bindingLineHeight.Set(value);
			nativeAttributeMask = nativeAttributeMask | 8388608i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 8388608i64 : nativePollingMask & ~8388608i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearLineHeight();
			}
		}

		void SetLineHeight(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("LineHeight", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetLineHeight(configuration);
				widget.nativeAttributes.bindingLineHeight.SetSharedReader(configuration, value);
			});
		}

		void ClearLineHeight()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("LineHeight");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLineHeight != nullptr)
			{
				nativeAttributes.bindingLineHeight.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8388608i64;
			nativePollingMask = nativePollingMask & ~8388608i64;
			handle.ClearLineHeight();
		}

		double GetLetterSpacing() const
		{
			if (nativeAttributes.bindingLetterSpacing == nullptr)
			{
				if (initialized || synchronized) return handle.GetLetterSpacing();
				nativeAttributes.bindingLetterSpacing = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingLetterSpacing.IsSet())
			{
				double value = handle.GetLetterSpacing();
				nativeAttributes.bindingLetterSpacing.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingLetterSpacing.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingLetterSpacing.IsSet())
			{
				double fallback = handle.GetFallbackLetterSpacing();
				nativeAttributes.bindingLetterSpacing.Remember(fallback);
				return fallback;
			}
			double value = handle.GetLetterSpacing();
			nativeAttributes.bindingLetterSpacing.Remember(value);
			return value;
		}

		void SetLetterSpacing(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLetterSpacing == nullptr)
			{
				nativeAttributes.bindingLetterSpacing = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("LetterSpacing");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLetterSpacing != nullptr)
			{
				nativeAttributes.bindingLetterSpacing.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16777216i64;
			nativePollingMask = nativePollingMask & ~16777216i64;
			handle.SetLetterSpacing(value);
			nativeAttributes.bindingLetterSpacing.Remember(value);
		}

		void SetLetterSpacing(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLetterSpacing == nullptr)
			{
				nativeAttributes.bindingLetterSpacing = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("LetterSpacing");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingLetterSpacing.IsSet())
			{
				nativeAttributes.bindingLetterSpacing.Remember(handle.GetLetterSpacing());
			}
			nativeAttributes.bindingLetterSpacing.Set(value);
			nativeAttributeMask = nativeAttributeMask | 16777216i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 16777216i64 : nativePollingMask & ~16777216i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearLetterSpacing();
			}
		}

		void SetLetterSpacing(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("LetterSpacing", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetLetterSpacing(configuration);
				widget.nativeAttributes.bindingLetterSpacing.SetSharedReader(configuration, value);
			});
		}

		void ClearLetterSpacing()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("LetterSpacing");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLetterSpacing != nullptr)
			{
				nativeAttributes.bindingLetterSpacing.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~16777216i64;
			nativePollingMask = nativePollingMask & ~16777216i64;
			handle.ClearLetterSpacing();
		}

		string GetDirection() const
		{
			if (nativeAttributes.bindingDirection == nullptr)
			{
				if (initialized || synchronized) return handle.GetDirection();
				nativeAttributes.bindingDirection = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingDirection.IsSet())
			{
				string value = handle.GetDirection();
				nativeAttributes.bindingDirection.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingDirection.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingDirection.IsSet())
			{
				string fallback = handle.GetFallbackDirection();
				nativeAttributes.bindingDirection.Remember(fallback);
				return fallback;
			}
			string value = handle.GetDirection();
			nativeAttributes.bindingDirection.Remember(value);
			return value;
		}

		void SetDirection(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingDirection == nullptr)
			{
				nativeAttributes.bindingDirection = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Direction");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingDirection != nullptr)
			{
				nativeAttributes.bindingDirection.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~33554432i64;
			nativePollingMask = nativePollingMask & ~33554432i64;
			handle.SetDirection(value);
			nativeAttributes.bindingDirection.Remember(value);
		}

		void SetDirection(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingDirection == nullptr)
			{
				nativeAttributes.bindingDirection = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Direction");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingDirection.IsSet())
			{
				nativeAttributes.bindingDirection.Remember(handle.GetDirection());
			}
			nativeAttributes.bindingDirection.Set(value);
			nativeAttributeMask = nativeAttributeMask | 33554432i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 33554432i64 : nativePollingMask & ~33554432i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearDirection();
			}
		}

		void SetDirection(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Direction", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetDirection(configuration);
				widget.nativeAttributes.bindingDirection.SetSharedReader(configuration, value);
			});
		}

		void ClearDirection()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Direction");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingDirection != nullptr)
			{
				nativeAttributes.bindingDirection.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~33554432i64;
			nativePollingMask = nativePollingMask & ~33554432i64;
			handle.ClearDirection();
		}

		string GetLanguage() const
		{
			if (nativeAttributes.bindingLanguage == nullptr)
			{
				if (initialized || synchronized) return handle.GetLanguage();
				nativeAttributes.bindingLanguage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingLanguage.IsSet())
			{
				string value = handle.GetLanguage();
				nativeAttributes.bindingLanguage.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingLanguage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingLanguage.IsSet())
			{
				string fallback = handle.GetFallbackLanguage();
				nativeAttributes.bindingLanguage.Remember(fallback);
				return fallback;
			}
			string value = handle.GetLanguage();
			nativeAttributes.bindingLanguage.Remember(value);
			return value;
		}

		void SetLanguage(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLanguage == nullptr)
			{
				nativeAttributes.bindingLanguage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Language");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLanguage != nullptr)
			{
				nativeAttributes.bindingLanguage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~67108864i64;
			nativePollingMask = nativePollingMask & ~67108864i64;
			handle.SetLanguage(value);
			nativeAttributes.bindingLanguage.Remember(value);
		}

		void SetLanguage(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingLanguage == nullptr)
			{
				nativeAttributes.bindingLanguage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Language");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingLanguage.IsSet())
			{
				nativeAttributes.bindingLanguage.Remember(handle.GetLanguage());
			}
			nativeAttributes.bindingLanguage.Set(value);
			nativeAttributeMask = nativeAttributeMask | 67108864i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 67108864i64 : nativePollingMask & ~67108864i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearLanguage();
			}
		}

		void SetLanguage(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Language", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetLanguage(configuration);
				widget.nativeAttributes.bindingLanguage.SetSharedReader(configuration, value);
			});
		}

		void ClearLanguage()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Language");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingLanguage != nullptr)
			{
				nativeAttributes.bindingLanguage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~67108864i64;
			nativePollingMask = nativePollingMask & ~67108864i64;
			handle.ClearLanguage();
		}

		double GetCornerRadius() const
		{
			if (nativeAttributes.bindingCornerRadius == nullptr)
			{
				if (initialized || synchronized) return handle.GetCornerRadius();
				nativeAttributes.bindingCornerRadius = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingCornerRadius.IsSet())
			{
				double value = handle.GetCornerRadius();
				nativeAttributes.bindingCornerRadius.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingCornerRadius.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingCornerRadius.IsSet())
			{
				double fallback = handle.GetFallbackCornerRadius();
				nativeAttributes.bindingCornerRadius.Remember(fallback);
				return fallback;
			}
			double value = handle.GetCornerRadius();
			nativeAttributes.bindingCornerRadius.Remember(value);
			return value;
		}

		void SetCornerRadius(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingCornerRadius == nullptr)
			{
				nativeAttributes.bindingCornerRadius = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("CornerRadius");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingCornerRadius != nullptr)
			{
				nativeAttributes.bindingCornerRadius.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~134217728i64;
			nativePollingMask = nativePollingMask & ~134217728i64;
			handle.SetCornerRadius(value);
			nativeAttributes.bindingCornerRadius.Remember(value);
		}

		void SetCornerRadius(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingCornerRadius == nullptr)
			{
				nativeAttributes.bindingCornerRadius = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("CornerRadius");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingCornerRadius.IsSet())
			{
				nativeAttributes.bindingCornerRadius.Remember(handle.GetCornerRadius());
			}
			nativeAttributes.bindingCornerRadius.Set(value);
			nativeAttributeMask = nativeAttributeMask | 134217728i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 134217728i64 : nativePollingMask & ~134217728i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearCornerRadius();
			}
		}

		void SetCornerRadius(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("CornerRadius", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetCornerRadius(configuration);
				widget.nativeAttributes.bindingCornerRadius.SetSharedReader(configuration, value);
			});
		}

		void ClearCornerRadius()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("CornerRadius");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingCornerRadius != nullptr)
			{
				nativeAttributes.bindingCornerRadius.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~134217728i64;
			nativePollingMask = nativePollingMask & ~134217728i64;
			handle.ClearCornerRadius();
		}

		double GetBorderWidth() const
		{
			if (nativeAttributes.bindingBorderWidth == nullptr)
			{
				if (initialized || synchronized) return handle.GetBorderWidth();
				nativeAttributes.bindingBorderWidth = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			if (!nativeAttributes.bindingBorderWidth.IsSet())
			{
				double value = handle.GetBorderWidth();
				nativeAttributes.bindingBorderWidth.Remember(value);
				return value;
			}
			double current = 0;
			bool found = false;
			found = nativeAttributes.bindingBorderWidth.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingBorderWidth.IsSet())
			{
				double fallback = handle.GetFallbackBorderWidth();
				nativeAttributes.bindingBorderWidth.Remember(fallback);
				return fallback;
			}
			double value = handle.GetBorderWidth();
			nativeAttributes.bindingBorderWidth.Remember(value);
			return value;
		}

		void SetBorderWidth(double value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBorderWidth == nullptr)
			{
				nativeAttributes.bindingBorderWidth = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			VsguiClearNotifiedBinding("BorderWidth");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBorderWidth != nullptr)
			{
				nativeAttributes.bindingBorderWidth.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~268435456i64;
			nativePollingMask = nativePollingMask & ~268435456i64;
			handle.SetBorderWidth(value);
			nativeAttributes.bindingBorderWidth.Remember(value);
		}

		void SetBorderWidth(std::attribute<double> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBorderWidth == nullptr)
			{
				nativeAttributes.bindingBorderWidth = VS::Gui::Internal::NativeValueAttributeCache<double>();
			}
			bool notified = VsguiClearNotifiedBinding("BorderWidth");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBorderWidth.IsSet())
			{
				nativeAttributes.bindingBorderWidth.Remember(handle.GetBorderWidth());
			}
			nativeAttributes.bindingBorderWidth.Set(value);
			nativeAttributeMask = nativeAttributeMask | 268435456i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 268435456i64 : nativePollingMask & ~268435456i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBorderWidth();
			}
		}

		void SetBorderWidth(Binding<double> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<double>("BorderWidth", value, [owner, value](std::attribute<double> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBorderWidth(configuration);
				widget.nativeAttributes.bindingBorderWidth.SetSharedReader(configuration, value);
			});
		}

		void ClearBorderWidth()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("BorderWidth");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBorderWidth != nullptr)
			{
				nativeAttributes.bindingBorderWidth.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~268435456i64;
			nativePollingMask = nativePollingMask & ~268435456i64;
			handle.ClearBorderWidth();
		}

		bool GetClipContent() const
		{
			if (nativeAttributes.bindingClipContent == nullptr)
			{
				if (initialized || synchronized) return handle.GetClipContent();
				nativeAttributes.bindingClipContent = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			if (!nativeAttributes.bindingClipContent.IsSet())
			{
				bool value = handle.GetClipContent();
				nativeAttributes.bindingClipContent.Remember(value);
				return value;
			}
			bool current = false;
			bool found = false;
			found = nativeAttributes.bindingClipContent.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingClipContent.IsSet())
			{
				bool fallback = handle.GetFallbackClipContent();
				nativeAttributes.bindingClipContent.Remember(fallback);
				return fallback;
			}
			bool value = handle.GetClipContent();
			nativeAttributes.bindingClipContent.Remember(value);
			return value;
		}

		void SetClipContent(bool value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingClipContent == nullptr)
			{
				nativeAttributes.bindingClipContent = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			VsguiClearNotifiedBinding("ClipContent");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingClipContent != nullptr)
			{
				nativeAttributes.bindingClipContent.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~536870912i64;
			nativePollingMask = nativePollingMask & ~536870912i64;
			handle.SetClipContent(value);
			nativeAttributes.bindingClipContent.Remember(value);
		}

		void SetClipContent(std::attribute<bool> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingClipContent == nullptr)
			{
				nativeAttributes.bindingClipContent = VS::Gui::Internal::NativeValueAttributeCache<bool>();
			}
			bool notified = VsguiClearNotifiedBinding("ClipContent");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingClipContent.IsSet())
			{
				nativeAttributes.bindingClipContent.Remember(handle.GetClipContent());
			}
			nativeAttributes.bindingClipContent.Set(value);
			nativeAttributeMask = nativeAttributeMask | 536870912i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 536870912i64 : nativePollingMask & ~536870912i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearClipContent();
			}
		}

		void SetClipContent(Binding<bool> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<bool>("ClipContent", value, [owner, value](std::attribute<bool> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetClipContent(configuration);
				widget.nativeAttributes.bindingClipContent.SetSharedReader(configuration, value);
			});
		}

		void ClearClipContent()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("ClipContent");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingClipContent != nullptr)
			{
				nativeAttributes.bindingClipContent.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~536870912i64;
			nativePollingMask = nativePollingMask & ~536870912i64;
			handle.ClearClipContent();
		}

		string GetFilter() const
		{
			if (nativeAttributes.bindingFilter == nullptr)
			{
				if (initialized || synchronized) return handle.GetFilter();
				nativeAttributes.bindingFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingFilter.IsSet())
			{
				string value = handle.GetFilter();
				nativeAttributes.bindingFilter.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingFilter.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingFilter.IsSet())
			{
				string fallback = handle.GetFallbackFilter();
				nativeAttributes.bindingFilter.Remember(fallback);
				return fallback;
			}
			string value = handle.GetFilter();
			nativeAttributes.bindingFilter.Remember(value);
			return value;
		}

		void SetFilter(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFilter == nullptr)
			{
				nativeAttributes.bindingFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Filter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFilter != nullptr)
			{
				nativeAttributes.bindingFilter.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1073741824i64;
			nativePollingMask = nativePollingMask & ~1073741824i64;
			handle.SetFilter(value);
			nativeAttributes.bindingFilter.Remember(value);
		}

		void SetFilter(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingFilter == nullptr)
			{
				nativeAttributes.bindingFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Filter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingFilter.IsSet())
			{
				nativeAttributes.bindingFilter.Remember(handle.GetFilter());
			}
			nativeAttributes.bindingFilter.Set(value);
			nativeAttributeMask = nativeAttributeMask | 1073741824i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 1073741824i64 : nativePollingMask & ~1073741824i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearFilter();
			}
		}

		void SetFilter(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Filter", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetFilter(configuration);
				widget.nativeAttributes.bindingFilter.SetSharedReader(configuration, value);
			});
		}

		void ClearFilter()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Filter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingFilter != nullptr)
			{
				nativeAttributes.bindingFilter.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~1073741824i64;
			nativePollingMask = nativePollingMask & ~1073741824i64;
			handle.ClearFilter();
		}

		string GetBackdropFilter() const
		{
			if (nativeAttributes.bindingBackdropFilter == nullptr)
			{
				if (initialized || synchronized) return handle.GetBackdropFilter();
				nativeAttributes.bindingBackdropFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingBackdropFilter.IsSet())
			{
				string value = handle.GetBackdropFilter();
				nativeAttributes.bindingBackdropFilter.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingBackdropFilter.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingBackdropFilter.IsSet())
			{
				string fallback = handle.GetFallbackBackdropFilter();
				nativeAttributes.bindingBackdropFilter.Remember(fallback);
				return fallback;
			}
			string value = handle.GetBackdropFilter();
			nativeAttributes.bindingBackdropFilter.Remember(value);
			return value;
		}

		void SetBackdropFilter(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackdropFilter == nullptr)
			{
				nativeAttributes.bindingBackdropFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("BackdropFilter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackdropFilter != nullptr)
			{
				nativeAttributes.bindingBackdropFilter.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2147483648i64;
			nativePollingMask = nativePollingMask & ~2147483648i64;
			handle.SetBackdropFilter(value);
			nativeAttributes.bindingBackdropFilter.Remember(value);
		}

		void SetBackdropFilter(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackdropFilter == nullptr)
			{
				nativeAttributes.bindingBackdropFilter = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("BackdropFilter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBackdropFilter.IsSet())
			{
				nativeAttributes.bindingBackdropFilter.Remember(handle.GetBackdropFilter());
			}
			nativeAttributes.bindingBackdropFilter.Set(value);
			nativeAttributeMask = nativeAttributeMask | 2147483648i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 2147483648i64 : nativePollingMask & ~2147483648i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBackdropFilter();
			}
		}

		void SetBackdropFilter(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("BackdropFilter", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBackdropFilter(configuration);
				widget.nativeAttributes.bindingBackdropFilter.SetSharedReader(configuration, value);
			});
		}

		void ClearBackdropFilter()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("BackdropFilter");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackdropFilter != nullptr)
			{
				nativeAttributes.bindingBackdropFilter.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~2147483648i64;
			nativePollingMask = nativePollingMask & ~2147483648i64;
			handle.ClearBackdropFilter();
		}

		string GetBackgroundImage() const
		{
			if (nativeAttributes.bindingBackgroundImage == nullptr)
			{
				if (initialized || synchronized) return handle.GetBackgroundImage();
				nativeAttributes.bindingBackgroundImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingBackgroundImage.IsSet())
			{
				string value = handle.GetBackgroundImage();
				nativeAttributes.bindingBackgroundImage.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingBackgroundImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return VS::Gui::__NativeResources::ResolveStyleImages(backgroundRequester, current);
			}
			if (nativeAttributes.bindingBackgroundImage.IsSet())
			{
				string fallback = handle.GetFallbackBackgroundImage();
				nativeAttributes.bindingBackgroundImage.Remember(fallback);
				return fallback;
			}
			string value = handle.GetBackgroundImage();
			nativeAttributes.bindingBackgroundImage.Remember(value);
			return value;
		}

		void SetBackgroundImage(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackgroundImage == nullptr)
			{
				nativeAttributes.bindingBackgroundImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("BackgroundImage");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackgroundImage != nullptr)
			{
				nativeAttributes.bindingBackgroundImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4294967296i64;
			nativePollingMask = nativePollingMask & ~4294967296i64;
			handle.SetBackgroundImage(VS::Gui::__NativeResources::ResolveStyleImages(VS::Gui::__NativeResources::CaptureRequester(), value));
			nativeAttributes.bindingBackgroundImage.Remember(value);
		}

		void SetBackgroundImage(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBackgroundImage == nullptr)
			{
				nativeAttributes.bindingBackgroundImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("BackgroundImage");
			backgroundRequester = VS::Gui::__NativeResources::CaptureRequester();
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBackgroundImage.IsSet())
			{
				nativeAttributes.bindingBackgroundImage.Remember(handle.GetBackgroundImage());
			}
			nativeAttributes.bindingBackgroundImage.Set(value);
			nativeAttributeMask = nativeAttributeMask | 4294967296i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 4294967296i64 : nativePollingMask & ~4294967296i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBackgroundImage();
			}
		}

		void SetBackgroundImage(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("BackgroundImage", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBackgroundImage(configuration);
				widget.nativeAttributes.bindingBackgroundImage.SetSharedReader(configuration, value);
			});
		}

		void ClearBackgroundImage()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("BackgroundImage");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBackgroundImage != nullptr)
			{
				nativeAttributes.bindingBackgroundImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~4294967296i64;
			nativePollingMask = nativePollingMask & ~4294967296i64;
			handle.ClearBackgroundImage();
		}

		string GetMaskImage() const
		{
			if (nativeAttributes.bindingMaskImage == nullptr)
			{
				if (initialized || synchronized) return handle.GetMaskImage();
				nativeAttributes.bindingMaskImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingMaskImage.IsSet())
			{
				string value = handle.GetMaskImage();
				nativeAttributes.bindingMaskImage.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingMaskImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return VS::Gui::__NativeResources::ResolveStyleImages(maskRequester, current);
			}
			if (nativeAttributes.bindingMaskImage.IsSet())
			{
				string fallback = handle.GetFallbackMaskImage();
				nativeAttributes.bindingMaskImage.Remember(fallback);
				return fallback;
			}
			string value = handle.GetMaskImage();
			nativeAttributes.bindingMaskImage.Remember(value);
			return value;
		}

		void SetMaskImage(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingMaskImage == nullptr)
			{
				nativeAttributes.bindingMaskImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("MaskImage");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingMaskImage != nullptr)
			{
				nativeAttributes.bindingMaskImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8589934592i64;
			nativePollingMask = nativePollingMask & ~8589934592i64;
			handle.SetMaskImage(VS::Gui::__NativeResources::ResolveStyleImages(VS::Gui::__NativeResources::CaptureRequester(), value));
			nativeAttributes.bindingMaskImage.Remember(value);
		}

		void SetMaskImage(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingMaskImage == nullptr)
			{
				nativeAttributes.bindingMaskImage = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("MaskImage");
			maskRequester = VS::Gui::__NativeResources::CaptureRequester();
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingMaskImage.IsSet())
			{
				nativeAttributes.bindingMaskImage.Remember(handle.GetMaskImage());
			}
			nativeAttributes.bindingMaskImage.Set(value);
			nativeAttributeMask = nativeAttributeMask | 8589934592i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 8589934592i64 : nativePollingMask & ~8589934592i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearMaskImage();
			}
		}

		void SetMaskImage(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("MaskImage", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetMaskImage(configuration);
				widget.nativeAttributes.bindingMaskImage.SetSharedReader(configuration, value);
			});
		}

		void ClearMaskImage()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("MaskImage");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingMaskImage != nullptr)
			{
				nativeAttributes.bindingMaskImage.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~8589934592i64;
			nativePollingMask = nativePollingMask & ~8589934592i64;
			handle.ClearMaskImage();
		}

		string GetBoxShadow() const
		{
			if (nativeAttributes.bindingBoxShadow == nullptr)
			{
				if (initialized || synchronized) return handle.GetBoxShadow();
				nativeAttributes.bindingBoxShadow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingBoxShadow.IsSet())
			{
				string value = handle.GetBoxShadow();
				nativeAttributes.bindingBoxShadow.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingBoxShadow.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingBoxShadow.IsSet())
			{
				string fallback = handle.GetFallbackBoxShadow();
				nativeAttributes.bindingBoxShadow.Remember(fallback);
				return fallback;
			}
			string value = handle.GetBoxShadow();
			nativeAttributes.bindingBoxShadow.Remember(value);
			return value;
		}

		void SetBoxShadow(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBoxShadow == nullptr)
			{
				nativeAttributes.bindingBoxShadow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("BoxShadow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBoxShadow != nullptr)
			{
				nativeAttributes.bindingBoxShadow.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~17179869184i64;
			nativePollingMask = nativePollingMask & ~17179869184i64;
			handle.SetBoxShadow(value);
			nativeAttributes.bindingBoxShadow.Remember(value);
		}

		void SetBoxShadow(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingBoxShadow == nullptr)
			{
				nativeAttributes.bindingBoxShadow = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("BoxShadow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingBoxShadow.IsSet())
			{
				nativeAttributes.bindingBoxShadow.Remember(handle.GetBoxShadow());
			}
			nativeAttributes.bindingBoxShadow.Set(value);
			nativeAttributeMask = nativeAttributeMask | 17179869184i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 17179869184i64 : nativePollingMask & ~17179869184i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearBoxShadow();
			}
		}

		void SetBoxShadow(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("BoxShadow", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetBoxShadow(configuration);
				widget.nativeAttributes.bindingBoxShadow.SetSharedReader(configuration, value);
			});
		}

		void ClearBoxShadow()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("BoxShadow");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingBoxShadow != nullptr)
			{
				nativeAttributes.bindingBoxShadow.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~17179869184i64;
			nativePollingMask = nativePollingMask & ~17179869184i64;
			handle.ClearBoxShadow();
		}

		string GetTransform() const
		{
			if (nativeAttributes.bindingTransform == nullptr)
			{
				if (initialized || synchronized) return handle.GetTransform();
				nativeAttributes.bindingTransform = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			if (!nativeAttributes.bindingTransform.IsSet())
			{
				string value = handle.GetTransform();
				nativeAttributes.bindingTransform.Remember(value);
				return value;
			}
			string current = "";
			bool found = false;
			found = nativeAttributes.bindingTransform.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
			if (found)
			{
				return current;
			}
			if (nativeAttributes.bindingTransform.IsSet())
			{
				string fallback = handle.GetFallbackTransform();
				nativeAttributes.bindingTransform.Remember(fallback);
				return fallback;
			}
			string value = handle.GetTransform();
			nativeAttributes.bindingTransform.Remember(value);
			return value;
		}

		void SetTransform(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTransform == nullptr)
			{
				nativeAttributes.bindingTransform = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			VsguiClearNotifiedBinding("Transform");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTransform != nullptr)
			{
				nativeAttributes.bindingTransform.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~34359738368i64;
			nativePollingMask = nativePollingMask & ~34359738368i64;
			handle.SetTransform(value);
			nativeAttributes.bindingTransform.Remember(value);
		}

		void SetTransform(std::attribute<string> value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			if (nativeAttributes.bindingTransform == nullptr)
			{
				nativeAttributes.bindingTransform = VS::Gui::Internal::NativeValueAttributeCache<string>();
			}
			bool notified = VsguiClearNotifiedBinding("Transform");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (value.is_bound() && (synchronized || handle.IsInitialized()) && !nativeAttributes.bindingTransform.IsSet())
			{
				nativeAttributes.bindingTransform.Remember(handle.GetTransform());
			}
			nativeAttributes.bindingTransform.Set(value);
			nativeAttributeMask = nativeAttributeMask | 34359738368i64;
			nativePollingMask = value.is_bound() && !notified ? nativePollingMask | 34359738368i64 : nativePollingMask & ~34359738368i64;
			handle.RequestSynchronization();
			if (!value.is_set())
			{
				handle.ClearTransform();
			}
		}

		void SetTransform(Binding<string> value)
		{
			weak<Widget> owner = weak<Widget>(this);
			VsguiBind<string>("Transform", value, [owner, value](std::attribute<string> configuration) -> void
			{
				Widget widget = owner.lock();
				widget.SetTransform(configuration);
				widget.nativeAttributes.bindingTransform.SetSharedReader(configuration, value);
			});
		}

		void ClearTransform()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			VsguiClearNotifiedBinding("Transform");
			VS::Gui::PropertyEvaluation::Epoch++;
			if (nativeAttributes.bindingTransform != nullptr)
			{
				nativeAttributes.bindingTransform.Reset();
			}
			nativeAttributeMask = nativeAttributeMask & ~34359738368i64;
			nativePollingMask = nativePollingMask & ~34359738368i64;
			handle.ClearTransform();
		}

		string GetId() const
		{
			return handle.GetId();
		}

		void SetId(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetId(value);
		}

		void SetChildren(array<Widget> values)
		{
			Widget content = contentHost.lock();
			if (content != nullptr)
			{
				content.SetChildren(values);
				return;
			}
			array<Widget> next = array<Widget>();
			for (Widget child : VsguiInternalChildren)
			{
				next.push(child);
			}
			for (Widget child : values)
			{
				next.push(child);
			}
			VsguiSetAllChildren(next);
		}

		void AddChild(Widget child)
		{
			Widget content = contentHost.lock();
			if (content != nullptr)
			{
				content.AddChild(child);
				return;
			}
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			assert(child != nullptr, "GUI child must not be null");
			array<__NativeWidget> internal = array<__NativeWidget>();
			for (Widget item : VsguiInternalChildren)
			{
				internal.push(item.__GetNativeHandle());
			}
			uint64 token = handle.GetMutationToken();
			bool canAppend = len(internal) == 0 && token > 0u64 && token < 18446744073709551615u64 && childViewsToken == token;
			bool completed = false;
			int64 revision = childViewsRevision;
			try
			{
				handle.AppendChild(child.__GetNativeHandle(), internal);
				completed = true;
			}
			finally
			{
				if (childViewsRevision == revision)
				{
					if (completed && canAppend && handle.GetMutationToken() == token + 1u64 && handle.GetChildCount() == len(children) + 1)
					{
						children.push(child);
						childViewsToken = token + 1u64;
						childViewsRevision++;
					}
					else
					{
						VsguiRefreshChildViews();
					}
				}
			}
		}

		void RemoveChild(Widget child)
		{
			Widget content = contentHost.lock();
			if (content != nullptr)
			{
				content.RemoveChild(child);
				return;
			}
			array<Widget> next = array<Widget>();
			bool found = false;
			for (int64 index = 0; index < GetChildCount(); index++)
			{
				Widget existing = GetChild(index);
				if (existing == child)
				{
					found = true;
				}
				else
				{
					next.push(existing);
				}
			}
			assert(found, "Widget is not a child of this parent");
			VsguiSetAllChildren(next);
			VsguiInternalChildren.remove(child);
		}

		void VsguiSetAllChildren(array<Widget> values)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			array<Widget> next = array<Widget>();
			array<__NativeWidget> native = array<__NativeWidget>();
			for (Widget child : values)
			{
				assert(child != nullptr, "GUI child must not be null");
				next.push(child);
				native.push(child.__GetNativeHandle());
			}
			int64 revision = childViewsRevision;
			try
			{
				handle.SetChildren(native);
			}
			finally
			{
				if (childViewsRevision == revision)
				{
					VsguiRefreshChildViews();
				}
			}
		}
		void VsguiRefreshChildViews()
		{
			uint64 token = handle.GetMutationToken();
			int64 count = handle.GetChildCount();
			if (token != 0u64 && childViewsToken == token && len(children) == count) return;
			array<Widget> actual = array<Widget>();
			for (int64 index = 0; index < count; index++)
			{
				actual.push(VS::Gui::Widget::VsguiWrap(handle.GetChild(index)));
			}
			children = actual;
			childViewsToken = token;
			childViewsRevision++;
		}

		array<Widget> GetChildren() const
		{
			array<Widget> result = array<Widget>();
			int64 count = handle.GetChildCount();
			if (count == 0) return result;
			uint64 token = handle.GetMutationToken();
			if (token != 0u64 && childViewsToken == token && len(children) == count)
			{
				for (Widget child : children)
				{
					result.push(child);
				}
				return result;
			}
			for (int64 index = 0; index < count; index++)
			{
				result.push(VS::Gui::Widget::VsguiWrap(handle.GetChild(index)));
			}
			return result;
		}

		array<Widget> GetContentChildren() const
		{
			Widget content = contentHost.lock();
			return content != nullptr ? content.GetChildren() : GetChildren();
		}

	protected:
		// Composite controls keep authored content in an owned internal container.
		// This affects insertion only; parent and child queries retain the actual element tree.
		void VsguiSetContentHost(Widget content)
		{
			assert(content != nullptr && content != this, "Invalid GUI content host");
			Widget ancestor = content.GetParent();
			while (ancestor != nullptr && ancestor != this)
			{
				ancestor = ancestor.GetParent();
			}
			assert(ancestor == this, "GUI content host must be an owned descendant");
			contentHost = weak<Widget>(content);
		}

		virtual void OnChildrenChanged()
		{
		}
		void VsguiObserveChildChanges()
		{
			weak<Widget> owner = weak<Widget>(this);
			handle.SetChildrenChangedCallback([owner](bool retry) -> void
			{
				Widget widget = owner.lock(); if (widget == nullptr || widget.IsDisposed()) return; widget.VsguiRefreshChildViews(); if (retry)
				{
					widget.Synchronize();
				}
				widget.OnChildrenChanged();
			});
		}

		void AddInternalChild(Widget child)
		{
			array<Widget> values = {child};
			AddInternalChildren(values);
		}
		void PrependInternalChild(Widget child)
		{
			array<Widget> internal = {child};
			for (Widget existing : VsguiInternalChildren)
			{
				internal.push(existing);
			}
			array<Widget> next = array<Widget>();
			for (Widget existing : internal)
			{
				next.push(existing);
			}
			for (Widget existing : children)
			{
				if (!VsguiIsInternal(existing))
				{
					next.push(existing);
				}
			}
			VsguiSetAllChildren(next);
			VsguiInternalChildren = internal;
		}

		void AddInternalChildren(array<Widget> values)
		{
			array<Widget> next = array<Widget>();
			array<Widget> internal = array<Widget>();
			for (Widget child : VsguiInternalChildren)
			{
				next.push(child);
				internal.push(child);
			}
			for (Widget child : values)
			{
				next.push(child);
				internal.push(child);
			}
			for (int64 index = 0; index < GetChildCount(); index++)
			{
				Widget child = GetChild(index);
				if (!VsguiIsInternal(child))
				{
					next.push(child);
				}
			}
			VsguiSetAllChildren(next);
			VsguiInternalChildren = internal;
		}

	private:
		bool VsguiIsInternal(Widget value)
		{
			for (Widget child : VsguiInternalChildren)
			{
				if (child == value)
				{
					return true;
				}
			}
			return false;
		}

	public:
		int64 GetChildCount()
		{
			return handle.GetChildCount();
		}

		Widget GetChild(int64 index)
		{
			return VS::Gui::Widget::VsguiWrap(handle.GetChild(index));
		}

		Widget GetParent()
		{
			return VS::Gui::Widget::VsguiWrap(handle.GetParent());
		}

		Widget Find(string id)
		{
			if (id != "" && GetId() == id)
			{
				return this;
			}
			for (int64 index = 0; index < GetChildCount(); index++)
			{
				Widget child = GetChild(index);
				Widget result = child.Find(id);
				if (result != nullptr)
				{
					return result;
				}
			}
			return nullptr;
		}

		Widget VsguiResolveNative(__NativeWidget value)
		{
			return VS::Gui::Widget::VsguiWrap(value);
		}

		void VsguiPrepareTemplate()
		{
			if (templateInstalled)
			{
				return;
			}
			bool completed = false;
			try
			{
				templateInstalled = true;
				VsguiRegisterStyleProperties();
				VsguiInstallTemplate();
				completed = true;
			}
			finally
			{
				if (!completed)
				{
					templateInstalled = false;
				}
			}
		}

		void Initialize()
		{
			if (initialized)
			{
				return;
			}
			assert(!initializing, "Recursive GUI initialization");
			try
			{
				initializing = true;
				VsguiPrepareTemplate();
				VsguiFinalizeConfiguration();
				Synchronize();
				VsguiInitializeTree();
			}
			finally
			{
				initializing = false;
			}
		}

		void VsguiInitializeTree()
		{
			for (Widget child : children)
			{
				child.VsguiInitializeTree();
			}
			if (!initialized)
			{
				bool previous = initializing;
				try
				{
					initializing = true;
					OnInitialized();
					if (!VsguiHasPaintHook())
					{
						handle.DisablePaint();
					}
					initialized = true;
				}
				finally
				{
					initializing = previous;
				}
				if (VsguiHasInitializationHook())
				{
					Synchronize();
				}
			}
		}

		void VsguiLifecycle(int stage)
		{
			if (stage == 0)
			{
				if (!initialized)
				{
					assert(!initializing, "Recursive GUI initialization");
					try
					{
						initializing = true;
						OnInitialized();
						if (!VsguiHasPaintHook())
						{
							handle.DisablePaint();
						}
						initialized = true;
					}
					finally
					{
						initializing = false;
					}
					if (VsguiHasInitializationHook())
					{
						handle.Invalidate();
					}
				}
			}
			else if (stage == 1)
			{
				OnMounted();
				if (VsguiHasMountedHook())
				{
					handle.Invalidate();
				}
			}
			else if (stage == 2)
			{
				OnUnmounted();
			}
			else if (stage == 3)
			{
				OnLayoutChanged();
				if (VsguiHasLayoutChangedHook())
				{
					handle.Invalidate();
				}
			}
			else if (stage == 4)
			{
				VsguiSynchronizeOwn();
			}
		}

		bool IsInitialized() const
		{
			return initialized;
		}

	public:
		virtual void VsguiInstallTemplate()
		{
		}
		virtual void VsguiFinalizeConfiguration()
		{
		}
		virtual void VsguiRegisterStyleProperties()
		{
		}
		virtual void VsguiPullStyles()
		{
		}
		virtual bool VsguiHasCustomSynchronization()
		{
			if (!baseSynchronizationTypeKnown)
			{
				exactBaseSynchronizationType = VS::Gui::__NativeWidget::IsScriptType(fn() -> void
				{
					VsguiSynchronizeControl();
				}, "VS.Gui.Widget");
				baseSynchronizationTypeKnown = true;
			}
			return !exactBaseSynchronizationType;
		}
		virtual bool VsguiHasInitializationHook() const
		{
			return false;
		}
		virtual bool VsguiHasPaintHook() const
		{
			return false;
		}
		virtual bool VsguiHasMountedHook() const
		{
			return false;
		}
		virtual bool VsguiHasLayoutChangedHook() const
		{
			return false;
		}
		void VsguiInvalidate()
		{
			handle.Invalidate();
		}

	protected:
		// Exact framework controls may read the properties just synchronized by
		// this native callback. Queries inside getters keep the regular guards.
		bool VsguiCanUseControlSnapshot() const
		{
			return VS::Gui::Internal::NativeEvaluation::Widget == this && VS::Gui::PropertyEvaluation::NativeSourcesStable && VS::Gui::PropertyEvaluation::SafeDepth > 0 && VS::Gui::PropertyEvaluation::Depth == 0;
		}
		bool VsguiControlConfigurationDirty = true;
		uint64 VsguiControlMutationToken;
		bool VsguiNeedsControlConfiguration() const
		{
			return VsguiControlConfigurationDirty || VsguiControlMutationToken == 0u64 || VsguiControlMutationToken != handle.GetMutationToken();
		}
		void VsguiCommitControlConfiguration()
		{
			VsguiControlMutationToken = handle.GetMutationToken();
			VsguiControlConfigurationDirty = false;
		}
		virtual void VsguiDisposeProperties()
		{
		}
		virtual void OnInitialized()
		{
		}
		virtual void OnMounted()
		{
		}
		virtual void OnPaint(VS::Gui::PaintContext canvas)
		{
		}
		virtual void OnUnmounted()
		{
		}
		virtual void OnLayoutChanged()
		{
		}

	public:
		void Synchronize() override
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && VS::Gui::PropertyEvaluation::SafeDepth == 0, "GUI synchronization is not allowed in a getter");
			int previousSafeDepth = VS::Gui::PropertyEvaluation::SafeDepth;
			try
			{
				VS::Gui::PropertyEvaluation::SafeDepth = previousSafeDepth + 1;
				synchronized = true;
				VS::Gui::PropertyEvaluation::Epoch++;
				for (int pass = 0; pass < 32; pass++)
				{
					int64 epoch = VS::Gui::PropertyEvaluation::Epoch;
					VsguiSynchronizeTree();
					if (epoch == VS::Gui::PropertyEvaluation::Epoch)
					{
						return;
					}
				}
				throw "GUI property synchronization did not stabilize after 32 passes";
			}
			finally
			{
				VS::Gui::PropertyEvaluation::SafeDepth = previousSafeDepth;
			}
		}

		void VsguiSynchronizeTree()
		{
			for (BindingState state : VsguiBindingStates)
			{
				state.VsguiSynchronizeBindings();
			}
			VsguiPullStyles();
			if (nativeAttributeMask != 0)
			{
				VsguiSynchronizeNativeProperties();
			}
			VsguiSynchronizeProperties();
			VsguiSynchronizeControl();
			for (Widget child : children)
			{
				child.VsguiSynchronizeTree();
			}
		}

		void VsguiSynchronizeOwn()
		{
			VsguiSynchronizeOwn(handle.GetSynchronizationToken());
		}

		bool VsguiSynchronizeOwn(int64 token)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && VS::Gui::PropertyEvaluation::SafeDepth == 0, "GUI synchronization cannot be nested");
			if (!synchronized || !templateInstalled || nativeAttributeMask != 0 || len(VsguiBindingStates) != 0)
			{
				VS::Gui::PropertyEvaluation::NativeSourcesStable = false;
			}
			bool customSynchronization = VsguiHasCustomSynchronization();
			// Unknown derived types keep the full path. Generated exact-type checks
			// allow only initialized widgets without script work to return early.
			if (synchronized && templateInstalled && nativeAttributeMask == 0 && len(VsguiBindingStates) == 0 && !customSynchronization)
			{
				return false;
			}
			if (VS::Gui::PropertyEvaluation::NativeToken != token)
			{
				VS::Gui::PropertyEvaluation::NativeToken = token;
				VS::Gui::PropertyEvaluation::Epoch++;
			}
			int previousSafeDepth = VS::Gui::PropertyEvaluation::SafeDepth;
			try
			{
				VS::Gui::PropertyEvaluation::SafeDepth = previousSafeDepth + 1;
				for (BindingState state : VsguiBindingStates)
				{
					state.VsguiSynchronizeBindings();
				}
				if (!templateInstalled)
				{
					VsguiPrepareTemplate();
				}
				if (customSynchronization)
				{
					VsguiPullStyles();
				}
				if (nativeAttributeMask != 0)
				{
					VsguiSynchronizeNativeProperties();
				}
				if (customSynchronization)
				{
					VsguiSynchronizeProperties();
					VsguiSynchronizeControl();
				}
				synchronized = true;
			}
			finally
			{
				VS::Gui::PropertyEvaluation::SafeDepth = previousSafeDepth;
			}
			return nativePollingMask != 0 || len(VsguiBindingStates) != 0 || (customSynchronization && !VsguiCanSuspendSynchronization());
		}

		Subscription Subscribe(string eventName, fn(Widget, Event) -> void callback, bool capture)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			int64 connection = handle.Subscribe(eventName, capture);
			Subscription token = Subscription();
			try
			{
				eventHandlers[connection] = callback;
				weak<Widget> owner = weak<Widget>(this);
				token.VsguiSetQuery([owner, connection]() -> bool
				{
					Widget widget = owner.lock(); return widget != nullptr && widget.__GetNativeHandle().IsConnected(connection);
				});
				token.VsguiSetDisconnect([owner, connection]() -> void
				{
					Widget widget = owner.lock(); if (widget != nullptr)
					{
						widget.VsguiDisconnect(connection);
					}
				});
			}
			catch (...)
			{
				handle.Disconnect(connection);
				throw;
			}
			return token;
		}

		Subscription Subscribe(string eventName, fn(Widget, Event) -> void callback)
		{
			return Subscribe(eventName, callback, false);
		}

		void VsguiDisconnect(int64 connection)
		{
			handle.Disconnect(connection);
			eventHandlers.remove(connection);
		}

		void VsguiDispatch(int64 connection, Event value)
		{
			if (eventHandlers.has(connection))
			{
				fn(Widget, Event) -> void callback = eventHandlers[connection];
				callback(this, value);
			}
		}

		bool IsMounted() const
		{
			return handle.IsMounted();
		}
		bool IsDisposed() const
		{
			if (VS::Gui::Internal::NativeEvaluation::Widget == this)
			{
				return false;
			}
			return handle.IsDisposed();
		}
		void SetClasses(string value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetClassesText(value);
		}
		void SetClass(string name, bool enabled)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			handle.SetClass(name, enabled);
		}

		void Dispose()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			try
			{
				handle.Dispose();
			}
			finally
			{
				if (handle.IsDisposed())
				{
					VsguiReleaseTree();
				}
			}
		}

		void VsguiReleaseTree()
		{
			map<string, __NativeWatch> previousBindings = notifiedBindings;
			notifiedBindings = map<string, __NativeWatch>();
			for (__NativeWatch binding : previousBindings.values())
			{
				binding.Disconnect();
			}
			for (BindingState state : VsguiBindingStates)
			{
				state.VsguiReleaseBindings();
			}
			VsguiBindingStates = array<BindingState>();
			for (Widget child : children)
			{
				child.VsguiReleaseTree();
			}
			children = array<Widget>();
			VsguiInternalChildren = array<Widget>();
			eventHandlers.clear();
			if (nativeAttributes.bindingWidth != nullptr)
			{
				nativeAttributes.bindingWidth.Reset();
			}
			if (nativeAttributes.bindingHeight != nullptr)
			{
				nativeAttributes.bindingHeight.Reset();
			}
			if (nativeAttributes.bindingPadding != nullptr)
			{
				nativeAttributes.bindingPadding.Reset();
			}
			if (nativeAttributes.bindingGap != nullptr)
			{
				nativeAttributes.bindingGap.Reset();
			}
			if (nativeAttributes.bindingLayout != nullptr)
			{
				nativeAttributes.bindingLayout.Reset();
			}
			if (nativeAttributes.bindingVisibility != nullptr)
			{
				nativeAttributes.bindingVisibility.Reset();
			}
			if (nativeAttributes.bindingHitTest != nullptr)
			{
				nativeAttributes.bindingHitTest.Reset();
			}
			if (nativeAttributes.bindingCursor != nullptr)
			{
				nativeAttributes.bindingCursor.Reset();
			}
			if (nativeAttributes.bindingEnabled != nullptr)
			{
				nativeAttributes.bindingEnabled.Reset();
			}
			if (nativeAttributes.bindingFocusable != nullptr)
			{
				nativeAttributes.bindingFocusable.Reset();
			}
			if (nativeAttributes.bindingOpacity != nullptr)
			{
				nativeAttributes.bindingOpacity.Reset();
			}
			if (nativeAttributes.bindingBackground != nullptr)
			{
				nativeAttributes.bindingBackground.Reset();
			}
			if (nativeAttributes.bindingText != nullptr)
			{
				nativeAttributes.bindingText.Reset();
			}
			if (nativeAttributes.bindingImage != nullptr)
			{
				nativeAttributes.bindingImage.Reset();
			}
			if (nativeAttributes.bindingFontFamily != nullptr)
			{
				nativeAttributes.bindingFontFamily.Reset();
			}
			if (nativeAttributes.bindingFontSize != nullptr)
			{
				nativeAttributes.bindingFontSize.Reset();
			}
			if (nativeAttributes.bindingFontWeight != nullptr)
			{
				nativeAttributes.bindingFontWeight.Reset();
			}
			if (nativeAttributes.bindingFontStyle != nullptr)
			{
				nativeAttributes.bindingFontStyle.Reset();
			}
			if (nativeAttributes.bindingTextColor != nullptr)
			{
				nativeAttributes.bindingTextColor.Reset();
			}
			if (nativeAttributes.bindingBorderColor != nullptr)
			{
				nativeAttributes.bindingBorderColor.Reset();
			}
			if (nativeAttributes.bindingTextAlign != nullptr)
			{
				nativeAttributes.bindingTextAlign.Reset();
			}
			if (nativeAttributes.bindingWhiteSpace != nullptr)
			{
				nativeAttributes.bindingWhiteSpace.Reset();
			}
			if (nativeAttributes.bindingTextOverflow != nullptr)
			{
				nativeAttributes.bindingTextOverflow.Reset();
			}
			if (nativeAttributes.bindingLineHeight != nullptr)
			{
				nativeAttributes.bindingLineHeight.Reset();
			}
			if (nativeAttributes.bindingLetterSpacing != nullptr)
			{
				nativeAttributes.bindingLetterSpacing.Reset();
			}
			if (nativeAttributes.bindingDirection != nullptr)
			{
				nativeAttributes.bindingDirection.Reset();
			}
			if (nativeAttributes.bindingLanguage != nullptr)
			{
				nativeAttributes.bindingLanguage.Reset();
			}
			if (nativeAttributes.bindingCornerRadius != nullptr)
			{
				nativeAttributes.bindingCornerRadius.Reset();
			}
			if (nativeAttributes.bindingBorderWidth != nullptr)
			{
				nativeAttributes.bindingBorderWidth.Reset();
			}
			if (nativeAttributes.bindingClipContent != nullptr)
			{
				nativeAttributes.bindingClipContent.Reset();
			}
			if (nativeAttributes.bindingFilter != nullptr)
			{
				nativeAttributes.bindingFilter.Reset();
			}
			if (nativeAttributes.bindingBackdropFilter != nullptr)
			{
				nativeAttributes.bindingBackdropFilter.Reset();
			}
			if (nativeAttributes.bindingBackgroundImage != nullptr)
			{
				nativeAttributes.bindingBackgroundImage.Reset();
			}
			if (nativeAttributes.bindingMaskImage != nullptr)
			{
				nativeAttributes.bindingMaskImage.Reset();
			}
			if (nativeAttributes.bindingBoxShadow != nullptr)
			{
				nativeAttributes.bindingBoxShadow.Reset();
			}
			if (nativeAttributes.bindingTransform != nullptr)
			{
				nativeAttributes.bindingTransform.Reset();
			}
			nativeAttributeMask = 0;
			VsguiDisposeProperties();
		}

		__NativeWidget __GetNativeHandle() const
		{
			return handle;
		}

		void VsguiRetainBindings(BindingState state)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid GUI binding state attachment");
			assert(state != nullptr, "GUI binding state must not be null");
			VsguiBindingStates.push(state);
			handle.RequestSynchronization();
		}
		void VsguiRemoveRetainedBindings(BindingState state)
		{
			for (int64 index = 0; index < len(VsguiBindingStates); index++)
			{
				if (VsguiBindingStates[index] == state)
				{
					VsguiBindingStates.remove_at(index);
					return;
				}
			}
		}

		void VsguiBind<T>(string property, Binding<T> value, fn(std::attribute<T>) -> void install)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0 && !IsDisposed(), "Invalid GUI notification binding");
			assert(value != nullptr, "GUI notification binding must not be null");
			std::attribute<T> configuration = std::attribute<T>();
			if (value.VsguiAlwaysHasValue())
			{
				configuration.bind([value]() -> T
				{
					return value.VsguiReadValue();
				});
			}
			else
			{
				configuration.bind_optional([value]() -> optional<T>
				{
					return value.VsguiRead();
				});
			}
			__NativeWatch watch = VS::Gui::__NativeWatch(handle, value.VsguiGetSources(), value.VsguiAlwaysHasValue());
			__NativeWatch previous = nullptr;
			if (notifiedBindings.has(property))
			{
				previous = notifiedBindings[property];
			}
			notifiedBindings[property] = watch;
			int64 installing = VS::Gui::BindingUpdates::VsguiInstalling;
			try
			{
				VS::Gui::BindingUpdates::VsguiInstalling = watch.GetId();
				install(configuration);
			}
			catch (...)
			{
				if (notifiedBindings.has(property) && notifiedBindings[property] == watch)
				{
					if (previous == nullptr)
					{
						notifiedBindings.remove(property);
					}
					else
					{
						notifiedBindings[property] = previous;
					}
				}
				watch.Disconnect();
				throw;
			}
			finally
			{
				VS::Gui::BindingUpdates::VsguiInstalling = installing;
			}
			if (previous != nullptr)
			{
				previous.Disconnect();
			}
		}

		bool VsguiHasStableBinding(string property) const
		{
			return len(notifiedBindings) != 0 && notifiedBindings.has(property) && notifiedBindings[property].AllowsSuspension();
		}

		bool VsguiClearNotifiedBinding(string property)
		{
			if (len(notifiedBindings) == 0 || !notifiedBindings.has(property))
			{
				return false;
			}
			__NativeWatch previous = notifiedBindings[property];
			if (previous.GetId() == VS::Gui::BindingUpdates::VsguiInstalling)
			{
				VS::Gui::BindingUpdates::VsguiInstalling = 0;
				return previous.AllowsSuspension();
			}
			notifiedBindings.remove(property);
			previous.Disconnect();
			return false;
		}

		virtual void VsguiSynchronizeProperties()
		{
		}
		virtual void VsguiSynchronizeControl()
		{
		}
		virtual bool VsguiCanSuspendSynchronization()
		{
			return false;
		}
		virtual bool VsguiAllowsSourceSnapshot()
		{
			return false;
		}
		virtual void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y)
		{
		}

	protected:
		void VsguiRegisterControlAction()
		{
			weak<Widget> owner = weak<Widget>(this);
			handle.SetControlActionCallback([owner](int action, bool checkedValue, double number, string text, double x, double y) -> bool
			{
				Widget widget = owner.lock(); if (widget == nullptr) return false; // Host dispatch and entry cleanup may dispose the target after the native check.
				if (!widget.IsDisposed())
				{
					widget.VsguiControlAction(action, checkedValue, number, text, x, y);
				}
				return true;
			});
		}

	private:
		void VsguiRegisterView(bool installCallback)
		{
			int64 identity = handle.GetIdentity();
			map<int64, weak<Widget>> views = VS::Gui::Widget::VsguiViews;
			array<int64> keys = VS::Gui::Widget::VsguiViewKeys;
			if (views.has(identity) && !views[identity].expired())
			{
				return;
			}
			if (!views.has(identity))
			{
				keys.push(identity);
			}
			views[identity] = weak<Widget>(this);
			weak<Widget> owner = weak<Widget>(this);
			fn(int, int64, bool) -> bool callback = [owner](int stage, int64 token, bool sourcesStable) -> bool
			{
				Widget widget = owner.lock();
				if (widget == nullptr)
				{
					return false;
				}
				if (stage == 4)
				{
					Widget previous = VS::Gui::Internal::NativeEvaluation::Widget;
					bool previousSources = VS::Gui::PropertyEvaluation::NativeSourcesStable;
					try
					{
						VS::Gui::Internal::NativeEvaluation::Widget = widget;
						VS::Gui::PropertyEvaluation::NativeSourcesStable = sourcesStable;
						return widget.VsguiSynchronizeOwn(token);
					}
					finally
					{
						VS::Gui::Internal::NativeEvaluation::Widget = previous;
						VS::Gui::PropertyEvaluation::NativeSourcesStable = previousSources;
					}
				}
				widget.VsguiLifecycle(stage);
				return true;
			};
			if (installCallback)
			{
				handle.SetLifecycleCallback(callback, fn() -> void
				{
					VsguiSynchronizeControl();
				}, VsguiAllowsSourceSnapshot());
			}
			else
			{
				// Rebind existing metadata without mutating the native scene during a query.
				handle.SetBorrowedLifecycleCallback(callback, fn() -> void
				{
					VsguiSynchronizeControl();
				}, VsguiAllowsSourceSnapshot());
				VsguiRegisterControlAction();
			}
			for (int checked = 0; checked < 8 && len(keys) > 0; checked++)
			{
				if (VS::Gui::Widget::VsguiSweep >= len(keys))
				{
					VS::Gui::Widget::VsguiSweep = 0;
				}
				int64 index = VS::Gui::Widget::VsguiSweep;
				int64 key = keys[index];
				if (views[key].expired())
				{
					views.remove(key);
					keys[index] = keys[len(keys) - 1];
					keys.remove_at(len(keys) - 1);
				}
				else
				{
					VS::Gui::Widget::VsguiSweep++;
				}
			}
		}

		static map<int64, weak<Widget>> VsguiViews = map<int64, weak<Widget>>();
		static array<int64> VsguiViewKeys = array<int64>();
		static int64 VsguiSweep = 0;

		void VsguiSynchronizeNativeProperties()
		{
			if ((nativeAttributeMask & 15i64) != 0)
			{
				VsguiSynchronizeNativeGroup0();
			}
			if ((nativeAttributeMask & 240i64) != 0)
			{
				VsguiSynchronizeNativeGroup1();
			}
			if ((nativeAttributeMask & 3840i64) != 0)
			{
				VsguiSynchronizeNativeGroup2();
			}
			if ((nativeAttributeMask & 61440i64) != 0)
			{
				VsguiSynchronizeNativeGroup3();
			}
			if ((nativeAttributeMask & 983040i64) != 0)
			{
				VsguiSynchronizeNativeGroup4();
			}
			if ((nativeAttributeMask & 15728640i64) != 0)
			{
				VsguiSynchronizeNativeGroup5();
			}
			if ((nativeAttributeMask & 251658240i64) != 0)
			{
				VsguiSynchronizeNativeGroup6();
			}
			if ((nativeAttributeMask & 4026531840i64) != 0)
			{
				VsguiSynchronizeNativeGroup7();
			}
			if ((nativeAttributeMask & 64424509440i64) != 0)
			{
				VsguiSynchronizeNativeGroup8();
			}
		}

		// Skip unattached native attributes while preserving polling of live configurations.
		VS::Gui::Internal::NativeAttributeCaches nativeAttributes = VS::Gui::Internal::NativeAttributeCaches();
		int64 nativeAttributeMask = 0;
		// Fixed configurations wake on source or native changes; bound getters keep polling.
		int64 nativePollingMask;
		bool baseSynchronizationTypeKnown;
		bool exactBaseSynchronizationType;
		bool synchronized;
		bool initialized;
		bool initializing;
		bool templateInstalled;
		array<Widget> VsguiInternalChildren;
		array<BindingState> VsguiBindingStates;
		map<string, __NativeWatch> notifiedBindings;
		map < int64, fn(Widget, Event) -> void > eventHandlers;
		string imageRequester;
		string backgroundRequester;
		string maskRequester;
		void VsguiSynchronizeNativeGroup0()
		{
			bool found = false;
			if ((nativeAttributeMask & 1i64) != 0)
			{
				Length current = Length();
				found = nativeAttributes.bindingWidth.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyWidth(current);
				}
				else if (nativeAttributes.bindingWidth.IsSet())
				{
					handle.ApplyClearWidth();
					nativeAttributes.bindingWidth.Remember(handle.GetFallbackWidth());
				}
			}
			if ((nativeAttributeMask & 2i64) != 0)
			{
				Length current = Length();
				found = nativeAttributes.bindingHeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyHeight(current);
				}
				else if (nativeAttributes.bindingHeight.IsSet())
				{
					handle.ApplyClearHeight();
					nativeAttributes.bindingHeight.Remember(handle.GetFallbackHeight());
				}
			}
			if ((nativeAttributeMask & 4i64) != 0)
			{
				Insets current = Insets();
				found = nativeAttributes.bindingPadding.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyPadding(current);
				}
				else if (nativeAttributes.bindingPadding.IsSet())
				{
					handle.ApplyClearPadding();
					nativeAttributes.bindingPadding.Remember(handle.GetFallbackPadding());
				}
			}
			if ((nativeAttributeMask & 8i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingGap.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyGap(current);
				}
				else if (nativeAttributes.bindingGap.IsSet())
				{
					handle.ApplyClearGap();
					nativeAttributes.bindingGap.Remember(handle.GetFallbackGap());
				}
			}
		}

		void VsguiSynchronizeNativeGroup1()
		{
			bool found = false;
			if ((nativeAttributeMask & 16i64) != 0)
			{
				Layout current = Layout::Vertical;
				found = nativeAttributes.bindingLayout.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyLayout(current);
				}
				else if (nativeAttributes.bindingLayout.IsSet())
				{
					handle.ApplyClearLayout();
					nativeAttributes.bindingLayout.Remember(handle.GetFallbackLayout());
				}
			}
			if ((nativeAttributeMask & 32i64) != 0)
			{
				Visibility current = Visibility::Visible;
				found = nativeAttributes.bindingVisibility.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyVisibility(current);
				}
				else if (nativeAttributes.bindingVisibility.IsSet())
				{
					handle.ApplyClearVisibility();
					nativeAttributes.bindingVisibility.Remember(handle.GetFallbackVisibility());
				}
			}
			if ((nativeAttributeMask & 64i64) != 0)
			{
				HitTestMode current = HitTestMode::SelfAndChildren;
				found = nativeAttributes.bindingHitTest.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyHitTest(current);
				}
				else if (nativeAttributes.bindingHitTest.IsSet())
				{
					handle.ApplyClearHitTest();
					nativeAttributes.bindingHitTest.Remember(handle.GetFallbackHitTest());
				}
			}
			if ((nativeAttributeMask & 128i64) != 0)
			{
				Cursor current = Cursor::Default;
				found = nativeAttributes.bindingCursor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyCursor(current);
				}
				else if (nativeAttributes.bindingCursor.IsSet())
				{
					handle.ApplyClearCursor();
					nativeAttributes.bindingCursor.Remember(handle.GetFallbackCursor());
				}
			}
		}

		void VsguiSynchronizeNativeGroup2()
		{
			bool found = false;
			if ((nativeAttributeMask & 256i64) != 0)
			{
				bool current = false;
				found = nativeAttributes.bindingEnabled.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyEnabled(current);
				}
				else if (nativeAttributes.bindingEnabled.IsSet())
				{
					handle.ApplyClearEnabled();
					nativeAttributes.bindingEnabled.Remember(handle.GetFallbackEnabled());
				}
			}
			if ((nativeAttributeMask & 512i64) != 0)
			{
				bool current = false;
				found = nativeAttributes.bindingFocusable.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFocusable(current);
				}
				else if (nativeAttributes.bindingFocusable.IsSet())
				{
					handle.ApplyClearFocusable();
					nativeAttributes.bindingFocusable.Remember(handle.GetFallbackFocusable());
				}
			}
			if ((nativeAttributeMask & 1024i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingOpacity.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyOpacity(current);
				}
				else if (nativeAttributes.bindingOpacity.IsSet())
				{
					handle.ApplyClearOpacity();
					nativeAttributes.bindingOpacity.Remember(handle.GetFallbackOpacity());
				}
			}
			if ((nativeAttributeMask & 2048i64) != 0)
			{
				Color current = Color();
				found = nativeAttributes.bindingBackground.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBackground(current);
				}
				else if (nativeAttributes.bindingBackground.IsSet())
				{
					handle.ApplyClearBackground();
					nativeAttributes.bindingBackground.Remember(handle.GetFallbackBackground());
				}
			}
		}

		void VsguiSynchronizeNativeGroup3()
		{
			bool found = false;
			if ((nativeAttributeMask & 4096i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingText.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyText(current);
				}
				else if (nativeAttributes.bindingText.IsSet())
				{
					handle.ApplyClearText();
					nativeAttributes.bindingText.Remember(handle.GetFallbackText());
				}
			}
			if ((nativeAttributeMask & 8192i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyImage(VS::Gui::__NativeResources::ResolveFrom(imageRequester, current));
				}
				else if (nativeAttributes.bindingImage.IsSet())
				{
					handle.ApplyClearImage();
					nativeAttributes.bindingImage.Remember(handle.GetFallbackImage());
				}
			}
			if ((nativeAttributeMask & 16384i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingFontFamily.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFontFamily(current);
				}
				else if (nativeAttributes.bindingFontFamily.IsSet())
				{
					handle.ApplyClearFontFamily();
					nativeAttributes.bindingFontFamily.Remember(handle.GetFallbackFontFamily());
				}
			}
			if ((nativeAttributeMask & 32768i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingFontSize.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFontSize(current);
				}
				else if (nativeAttributes.bindingFontSize.IsSet())
				{
					handle.ApplyClearFontSize();
					nativeAttributes.bindingFontSize.Remember(handle.GetFallbackFontSize());
				}
			}
		}

		void VsguiSynchronizeNativeGroup4()
		{
			bool found = false;
			if ((nativeAttributeMask & 65536i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingFontWeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFontWeight(current);
				}
				else if (nativeAttributes.bindingFontWeight.IsSet())
				{
					handle.ApplyClearFontWeight();
					nativeAttributes.bindingFontWeight.Remember(handle.GetFallbackFontWeight());
				}
			}
			if ((nativeAttributeMask & 131072i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingFontStyle.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFontStyle(current);
				}
				else if (nativeAttributes.bindingFontStyle.IsSet())
				{
					handle.ApplyClearFontStyle();
					nativeAttributes.bindingFontStyle.Remember(handle.GetFallbackFontStyle());
				}
			}
			if ((nativeAttributeMask & 262144i64) != 0)
			{
				Color current = Color();
				found = nativeAttributes.bindingTextColor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyTextColor(current);
				}
				else if (nativeAttributes.bindingTextColor.IsSet())
				{
					handle.ApplyClearTextColor();
					nativeAttributes.bindingTextColor.Remember(handle.GetFallbackTextColor());
				}
			}
			if ((nativeAttributeMask & 524288i64) != 0)
			{
				Color current = Color();
				found = nativeAttributes.bindingBorderColor.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBorderColor(current);
				}
				else if (nativeAttributes.bindingBorderColor.IsSet())
				{
					handle.ApplyClearBorderColor();
					nativeAttributes.bindingBorderColor.Remember(handle.GetFallbackBorderColor());
				}
			}
		}

		void VsguiSynchronizeNativeGroup5()
		{
			bool found = false;
			if ((nativeAttributeMask & 1048576i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingTextAlign.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyTextAlign(current);
				}
				else if (nativeAttributes.bindingTextAlign.IsSet())
				{
					handle.ApplyClearTextAlign();
					nativeAttributes.bindingTextAlign.Remember(handle.GetFallbackTextAlign());
				}
			}
			if ((nativeAttributeMask & 2097152i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingWhiteSpace.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyWhiteSpace(current);
				}
				else if (nativeAttributes.bindingWhiteSpace.IsSet())
				{
					handle.ApplyClearWhiteSpace();
					nativeAttributes.bindingWhiteSpace.Remember(handle.GetFallbackWhiteSpace());
				}
			}
			if ((nativeAttributeMask & 4194304i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingTextOverflow.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyTextOverflow(current);
				}
				else if (nativeAttributes.bindingTextOverflow.IsSet())
				{
					handle.ApplyClearTextOverflow();
					nativeAttributes.bindingTextOverflow.Remember(handle.GetFallbackTextOverflow());
				}
			}
			if ((nativeAttributeMask & 8388608i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingLineHeight.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyLineHeight(current);
				}
				else if (nativeAttributes.bindingLineHeight.IsSet())
				{
					handle.ApplyClearLineHeight();
					nativeAttributes.bindingLineHeight.Remember(handle.GetFallbackLineHeight());
				}
			}
		}

		void VsguiSynchronizeNativeGroup6()
		{
			bool found = false;
			if ((nativeAttributeMask & 16777216i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingLetterSpacing.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyLetterSpacing(current);
				}
				else if (nativeAttributes.bindingLetterSpacing.IsSet())
				{
					handle.ApplyClearLetterSpacing();
					nativeAttributes.bindingLetterSpacing.Remember(handle.GetFallbackLetterSpacing());
				}
			}
			if ((nativeAttributeMask & 33554432i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingDirection.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyDirection(current);
				}
				else if (nativeAttributes.bindingDirection.IsSet())
				{
					handle.ApplyClearDirection();
					nativeAttributes.bindingDirection.Remember(handle.GetFallbackDirection());
				}
			}
			if ((nativeAttributeMask & 67108864i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingLanguage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyLanguage(current);
				}
				else if (nativeAttributes.bindingLanguage.IsSet())
				{
					handle.ApplyClearLanguage();
					nativeAttributes.bindingLanguage.Remember(handle.GetFallbackLanguage());
				}
			}
			if ((nativeAttributeMask & 134217728i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingCornerRadius.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyCornerRadius(current);
				}
				else if (nativeAttributes.bindingCornerRadius.IsSet())
				{
					handle.ApplyClearCornerRadius();
					nativeAttributes.bindingCornerRadius.Remember(handle.GetFallbackCornerRadius());
				}
			}
		}

		void VsguiSynchronizeNativeGroup7()
		{
			bool found = false;
			if ((nativeAttributeMask & 268435456i64) != 0)
			{
				double current = 0;
				found = nativeAttributes.bindingBorderWidth.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBorderWidth(current);
				}
				else if (nativeAttributes.bindingBorderWidth.IsSet())
				{
					handle.ApplyClearBorderWidth();
					nativeAttributes.bindingBorderWidth.Remember(handle.GetFallbackBorderWidth());
				}
			}
			if ((nativeAttributeMask & 536870912i64) != 0)
			{
				bool current = false;
				found = nativeAttributes.bindingClipContent.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyClipContent(current);
				}
				else if (nativeAttributes.bindingClipContent.IsSet())
				{
					handle.ApplyClearClipContent();
					nativeAttributes.bindingClipContent.Remember(handle.GetFallbackClipContent());
				}
			}
			if ((nativeAttributeMask & 1073741824i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingFilter.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyFilter(current);
				}
				else if (nativeAttributes.bindingFilter.IsSet())
				{
					handle.ApplyClearFilter();
					nativeAttributes.bindingFilter.Remember(handle.GetFallbackFilter());
				}
			}
			if ((nativeAttributeMask & 2147483648i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingBackdropFilter.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBackdropFilter(current);
				}
				else if (nativeAttributes.bindingBackdropFilter.IsSet())
				{
					handle.ApplyClearBackdropFilter();
					nativeAttributes.bindingBackdropFilter.Remember(handle.GetFallbackBackdropFilter());
				}
			}
		}

		void VsguiSynchronizeNativeGroup8()
		{
			bool found = false;
			if ((nativeAttributeMask & 4294967296i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingBackgroundImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBackgroundImage(VS::Gui::__NativeResources::ResolveStyleImages(backgroundRequester, current));
				}
				else if (nativeAttributes.bindingBackgroundImage.IsSet())
				{
					handle.ApplyClearBackgroundImage();
					nativeAttributes.bindingBackgroundImage.Remember(handle.GetFallbackBackgroundImage());
				}
			}
			if ((nativeAttributeMask & 8589934592i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingMaskImage.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyMaskImage(VS::Gui::__NativeResources::ResolveStyleImages(maskRequester, current));
				}
				else if (nativeAttributes.bindingMaskImage.IsSet())
				{
					handle.ApplyClearMaskImage();
					nativeAttributes.bindingMaskImage.Remember(handle.GetFallbackMaskImage());
				}
			}
			if ((nativeAttributeMask & 17179869184i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingBoxShadow.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyBoxShadow(current);
				}
				else if (nativeAttributes.bindingBoxShadow.IsSet())
				{
					handle.ApplyClearBoxShadow();
					nativeAttributes.bindingBoxShadow.Remember(handle.GetFallbackBoxShadow());
				}
			}
			if ((nativeAttributeMask & 34359738368i64) != 0)
			{
				string current = "";
				found = nativeAttributes.bindingTransform.Read(VS::Gui::PropertyEvaluation::Epoch, VS::Gui::PropertyEvaluation::SafeDepth > 0, ref current);
				if (found)
				{
					handle.ApplyTransform(current);
				}
				else if (nativeAttributes.bindingTransform.IsSet())
				{
					handle.ApplyClearTransform();
					nativeAttributes.bindingTransform.Remember(handle.GetFallbackTransform());
				}
			}
		}

		__NativeWidget handle;
		array<Widget> children;
		int64 childViewsRevision = 0;
		uint64 childViewsToken = 0u64;
		weak<Widget> contentHost;
	};

	export class Event : public __NativeEvent
	{
	public:
		Event(__NativeEvent value, Widget sender)
		{
			VsguiAdopt(value);
			Target = sender.VsguiResolveNative(value.NativeTarget);
			CurrentTarget = sender;
		}

		Widget Target;
		Widget CurrentTarget;
	};

	export class Elements
	{
	public:
		Elements()
		{
			handle = VS::Gui::__NativeElements();
		}

		Elements(Widget root, int64 declarationScope)
		{
			observer = weak<Widget>(root);
			scope = declarationScope;
			scoped = true;
		}

		Widget Find(string id)
		{
			Widget root = observer.lock();
			if (scoped)
			{
				if (root == nullptr)
				{
					return nullptr;
				}
				return VS::Gui::Widget::VsguiWrap(root.__GetNativeHandle().FindInScope(id, scope));
			}
			if (handle.HasLookup())
			{
				return VS::Gui::Widget::VsguiWrap(handle.Find(id));
			}
			if (root != nullptr)
			{
				return root.Find(id);
			}
			return nullptr;
		}

		Widget Require(string id)
		{
			Widget found = Find(id);
			assert(found != nullptr, "Required GUI element was not found");
			return found;
		}

		void VsguiBind(Widget value)
		{
			observer = weak<Widget>(value);
		}

	private:
		__NativeElements handle;
		weak<Widget> observer;
		int64 scope;
		bool scoped;
	};
} // namespace VS::Gui

namespace VS::Gui
{
	namespace Internal
	{
		export void DispatchEvent(__NativeWidget sender, __NativeEvent value, int64 connection)
		{
			Widget widget = VS::Gui::Widget::VsguiWrap(sender);
			if (widget != nullptr)
			{
				widget.VsguiDispatch(connection, Event(value, widget));
			}
		}
	}
} // namespace VS::Gui

namespace VS::Gui
{
	namespace Internal
	{
		export bool DispatchLifecycleCallback(fn(int, int64) -> bool callback, int stage, int64 token)
		{
			return callback(stage, token);
		}

		export bool DispatchLifecycleCallback(fn(int, int64, bool) -> bool callback, int stage, int64 token, bool sourcesStable)
		{
			return callback(stage, token, sourcesStable);
		}

		export void DispatchLifecycle(__NativeWidget value, int stage)
		{
			Widget widget = VS::Gui::Widget::VsguiWrap(value);
			if (widget != nullptr)
			{
				widget.VsguiLifecycle(stage);
			}
		}
	}
} // namespace VS::Gui

namespace VS::Gui
{
	namespace Internal
	{
		export void DispatchPaint(__NativeWidget value, PaintContext canvas)
		{
			Widget widget = VS::Gui::Widget::VsguiWrap(value);
			if (widget != nullptr)
			{
				int previousDepth = VS::Gui::PropertyEvaluation::Depth;
				try
				{
					VS::Gui::PropertyEvaluation::Depth = previousDepth + 1;
					widget.VsguiPaint(canvas);
				}
				finally
				{
					VS::Gui::PropertyEvaluation::Depth = previousDepth;
				}
			}
		}
	}
}

namespace VS::Gui
{
	namespace Internal
	{
		export void DispatchChildrenCallback(fn(bool) -> void callback, bool retry)
		{
			callback(retry);
		}
		export bool DispatchControlCallback(fn(int, bool, double, string, double, double) -> bool callback, int action, bool checkedValue, double number, string text, double x, double y)
		{
			return callback(action, checkedValue, number, text, x, y);
		}

		export void DispatchControl(__NativeWidget sender, int action, bool checkedValue, double number, string text, double x, double y)
		{
			Widget widget = VS::Gui::Widget::VsguiWrap(sender);
			if (widget != nullptr && !widget.IsDisposed())
			{
				widget.VsguiControlAction(action, checkedValue, number, text, x, y);
			}
		}
	}
}
