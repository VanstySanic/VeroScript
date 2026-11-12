// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "attribute.vs";

namespace VS::Gui
{
	export class PropertyEvaluation
	{
	public:
		static int64 Epoch = 1;
		static int Depth = 0;
		static int SafeDepth = 0;
		static int64 NativeToken = 0;
		static bool NativeSourcesStable = false;
	};

	namespace Internal
	{
		export optional<T> BindingValue<T>(T value)
		{
			return optional<T>(value);
		}
		export optional<T> BindingValue<T>(optional<T> value)
		{
			return value;
		}
	}
	// Internal page state is owned by its root widget and keeps only weak widget references.
	export class BindingState
	{
	public:
		virtual void VsguiSynchronizeBindings()
		{
		}
		virtual void VsguiReleaseBindings()
		{
		}
	};

	// The native graph holds weak subscriptions and coalesces dirty widgets.
	export class ChangeSignal
	{
	public:
		void Notify()
		{
			if (VS::Gui::PropertyEvaluation::Depth != 0)
			{
				throw "GUI notifications are not allowed in a getter";
			}
			// Publish the cache invalidation first. Execution can stop after
			// the native signal changes, before a following script assignment.
			VS::Gui::BindingUpdates::VsguiRevision++;
			signal.Notify();
		}

		int64 GetRevision() const
		{
			return signal.GetRevision();
		}

		__NativeSignal VsguiGetNativeSignal() const
		{
			return signal;
		}

	private:
		__NativeSignal signal = VS::Gui::__NativeSignal();
	};

	// The explicit notification contract permits result caching across widgets.
	// Every changing input must be listed, or represented by a manual signal.
	export class Binding<T>
	{
	public:
		Binding(fn() -> T getter, array<ChangeSignal> sources)
		{
			reader = getter;
			hasReader = true;
			VsguiSetSources(sources);
		}

		Binding()
		{
		}

		static Binding<T> FromAttribute(std::attribute<T> value, array<ChangeSignal> sources)
		{
			Binding<T> result = VS::Gui::Binding<T>();
			result.configuration = value;
			result.VsguiSetSources(sources);
			return result;
		}

		optional<T> VsguiRead() const
		{
			if (reading)
			{
				throw "Recursive GUI notification binding";
			}
			if (!cached || checkedRevision != VS::Gui::BindingUpdates::VsguiRevision)
			{
				VsguiRefresh();
			}
			return hasReader ? optional<T>(valueSnapshot.value) : snapshot;
		}

		T VsguiReadValue() const
		{
			if (reading)
			{
				throw "Recursive GUI notification binding";
			}
			if (!cached || checkedRevision != VS::Gui::BindingUpdates::VsguiRevision)
			{
				VsguiRefresh();
			}
			return hasReader ? valueSnapshot.value : snapshot.get();
		}

		bool VsguiAlwaysHasValue() const
		{
			return hasReader;
		}

		array<__NativeSignal> VsguiGetSources() const
		{
			return nativeSources;
		}

	private:
		void VsguiRefresh() const
		{
			if (reading)
			{
				throw "Recursive GUI notification binding";
			}
			int64 current = VS::Gui::BindingUpdates::VsguiRevision;
			if (cached && checkedRevision == current)
			{
				return;
			}
			int64 revision = VS::Gui::__NativeSignal::ReadRevision(nativeSources);
			if (cached && revision == snapshotRevision)
			{
				checkedRevision = current;
				return;
			}
			int previousDepth = VS::Gui::PropertyEvaluation::Depth;
			try
			{
				reading = true;
				VS::Gui::PropertyEvaluation::Depth = previousDepth + 1;
				if (hasReader)
				{
					fn() -> T read = reader;
					valueSnapshot = std::shared<T>(read());
				}
				else
				{
					snapshot = configuration.try_get();
				}
				snapshotRevision = revision;
				checkedRevision = current;
				cached = true;
			}
			finally
			{
				VS::Gui::PropertyEvaluation::Depth = previousDepth;
				reading = false;
			}
		}

		void VsguiSetSources(array<ChangeSignal> sources)
		{
			for (ChangeSignal source : sources)
			{
				assert(source != nullptr, "GUI binding dependency must not be null");
				bool duplicate = false;
				for (ChangeSignal existing : dependencies)
				{
					duplicate = duplicate || source == existing;
				}
				if (!duplicate)
				{
					dependencies.push(source);
					nativeSources.push(source.VsguiGetNativeSignal());
				}
			}
		}

		std::attribute<T> configuration;
		fn() -> T reader;
		bool hasReader;
		array<ChangeSignal> dependencies;
		array<__NativeSignal> nativeSources;
		mutable int64 snapshotRevision;
		mutable int64 checkedRevision;
		// Value readers keep one typed payload; optional sources retain absence.
		mutable std::shared<T> valueSnapshot = nullptr;
		mutable optional<T> snapshot;
		mutable bool cached;
		mutable bool reading;
	};

	export class Observable<T> : public ChangeSignal
	{
	public:
		Observable(T value)
		{
			stored = optional<T>(value);
		}

		T Get() const
		{
			return stored.get();
		}

		void Set(T value)
		{
			if (VS::Gui::PropertyEvaluation::Depth != 0)
			{
				throw "GUI observable mutation is not allowed in a getter";
			}
			if (stored.get() == value)
			{
				return;
			}
			try
			{
				stored = optional<T>(value);
			}
			finally
			{
				// Publish even if execution stops after the value was committed.
				// Core rejects reuse if this cleanup itself cannot complete.
				Notify();
			}
		}

		Binding<T> AsBinding()
		{
			Binding<T> existing = cachedBinding.lock();
			if (existing != nullptr)
			{
				return existing;
			}
			Observable<T> source = this;
			ChangeSignal signal = source;
			array<ChangeSignal> sources = {signal};
			Binding<T> result = VS::Gui::Binding<T>([source]() -> T
			{
				return source.Get();
			}, sources);
			cachedBinding = weak<Binding<T>>(result);
			return result;
		}

	private:
		optional<T> stored;
		weak<Binding<T>> cachedBinding;
	};

	export class BindingUpdates
	{
	public:
		static void Batch(fn() -> void work)
		{
			if (VS::Gui::PropertyEvaluation::Depth != 0)
			{
				throw "GUI batches are not allowed in a getter";
			}
			int previousDepth = batchDepth;
			int64 previousNativeDepth = VS::Gui::__NativeSignal::GetBatchDepth();
			try
			{
				VS::Gui::__NativeSignal::BeginBatch();
				batchDepth = previousDepth + 1;
				work();
			}
			finally
			{
				batchDepth = previousDepth;
				VS::Gui::__NativeSignal::RestoreBatchDepth(previousNativeDepth);
			}
		}

		static void VsguiRequireBoundary()
		{
			if (batchDepth != 0)
			{
				throw "GUI updates cannot run inside a notification batch";
			}
		}

		static int64 VsguiInstalling;
		// Every supported notification goes through ChangeSignal.Notify().
		// Repeated readers can avoid even the native dependency query.
		static int64 VsguiRevision;

	private:
		static int batchDepth;
	};
} // namespace VS::Gui
