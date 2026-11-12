// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMobile.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#if defined(__APPLE__)
#include <exception>
#include <pthread.h>
#endif

namespace
{
	using namespace VeroScript::Mobile;

	void Require(const bool Condition, const std::string& Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	std::string Utf8Path(const std::filesystem::path& Path)
	{
		const auto Bytes = Path.generic_u8string();
		return {Bytes.begin(), Bytes.end()};
	}

	std::string Resource(const char* Name)
	{
		return Utf8Path(std::filesystem::temp_directory_path() / "veroscript-mobile-resources" / Name);
	}

	FSessionOptions Options()
	{
		FSessionOptions Result;
		Result.StdlibDirectory = VEROSCRIPT_TEST_STDLIB;
		Result.TimeoutMilliseconds = 5000;
		Result.Sources = {{Resource("a.vs"), "export import \"b.vs\"; export int value(){return dep();}"},
		    {Resource("b.vs"), "export int dep(){return 42;}"}};
		return Result;
	}

	struct FCacheDirectory
	{
		FCacheDirectory()
		    : Path(std::filesystem::temp_directory_path() / ("veroscript-mobile-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(Path);
		}
		~FCacheDirectory()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
		std::filesystem::path Path;
	};
}

int main()
{
	try
	{
		#if defined(VEROSCRIPT_TEST_VM_LIBRARY)
		auto ExplicitVm = Options();
		ExplicitVm.VmBackendPath = VEROSCRIPT_TEST_VM_LIBRARY;
		FSession ExplicitSession(ExplicitVm);
		vs_interop_vm_backend_info VmInfo{};
		VmInfo.struct_size = sizeof(VmInfo);
		Require(vs_interop_session_get_vm_backend_info(ExplicitSession.GetNativeSession(), &VmInfo) == VS_INTEROP_STATUS_OK && VmInfo.library_path.size != 0, "mobile VM path was not applied");
		const auto VmResult = ExplicitSession.Execute("void main(){print(29);}", Resource("explicit-vm.vs"));
		Require(VmResult.bSuccess && VmResult.Output == "29\n", VmResult.Error);
		#endif
		auto MissingVm = Options();
		MissingVm.VmBackendPath = Resource("missing-vm-library");
		FRunner MissingRunner(MissingVm);
		bool RejectedVm = false;
		try
		{
			RejectedVm = !MissingRunner.Execute("void main(){print(1);}", Resource("missing-vm.vs")).bSuccess;
		}
		catch (const std::exception&)
		{
			RejectedVm = true;
		}
		Require(RejectedVm, "invalid explicit VM selection fell back to the product VM");
		FRunner Runner(Options());
		const std::string UnicodeText = "\xE7\xA7\xBB\xE5\x8A\xA8 \xF0\x9F\x93\xA6";
		auto Result = Runner.Execute("import \"a.vs\"; void main(){print(\"" + UnicodeText + "\");print(value());}", Resource("main.vs"));
		Require(Result.bSuccess && Result.Output == UnicodeText + "\n42\n", Result.Diagnostics + Result.Error);
		Result = Runner.Execute("void main(){std::console::write(\"A\");std::console::write(\"B\");"
		                        "std::console::flush();assert(!std::console::supports_color());assert(!std::console::clear());}",
		    Resource("console.vs"));
		Require(Result.bSuccess && Result.Output == "AB", "mobile console must preserve exact plain text without escapes or added newlines");
		Result = Runner.Execute("import \"io.vs\"; void main(){std::io::read_line();}", Resource("main.vs"));
		Require(!Result.bSuccess && Result.Error.find("console input capability") != std::string::npos,
		    "mobile input policy failed: " + Result.Diagnostics + Result.Error);
		Result = Runner.Execute("import \"io.vs\"; void main(){std::io::read_file(\"forbidden\");}", Resource("main.vs"));
		Require(!Result.bSuccess && Result.Error.find("file read capability") != std::string::npos,
		    "mobile file policy failed: " + Result.Diagnostics + Result.Error);
		Result = Runner.Execute("void main(){while(true){}}", Resource("main.vs"));
		Require(!Result.bSuccess, "mobile instruction budget was not enforced");
		Result = Runner.Execute("void main(){print(7);}", Resource("main.vs"));
		Require(Result.bSuccess && Result.Output == "7\n", "failed run poisoned the next mobile execution");
		Result = Runner.Execute("void main(){throw \"" + std::string(70000, 'x') + "\";}", Resource("large-error.vs"));
		Require(!Result.bSuccess && Result.Error.size() <= 65536, "mobile error copy was unbounded");

		auto Circular = Options();
		Circular.Sources.push_back({Resource("cycle-a.vs"), "export import \"cycle-b.vs\"; export int a(){return b();}"});
		Circular.Sources.push_back({Resource("cycle-b.vs"), "import \"cycle-a.vs\"; export int b(){return 42;}"});
		FRunner CircularRunner(std::move(Circular));
		Result = CircularRunner.Execute("import \"cycle-a.vs\"; void main(){print(a());}", Resource("main.vs"));
		Require(Result.bSuccess && Result.Output == "42\n", "circular resource imports failed: " + Result.Diagnostics + Result.Error);

		const auto Blocker = std::filesystem::temp_directory_path() / "veroscript-mobile-cache-blocker";
		std::ofstream(Blocker).put('x');
		auto Unwritable = Options();
		Unwritable.CacheDirectory = Utf8Path(Blocker / "cache");
		FRunner UnwritableCache(std::move(Unwritable));
		Result = UnwritableCache.Execute("void main(){print(3);}", Resource("main.vs"));
		std::filesystem::remove(Blocker);
		Require(Result.bSuccess && Result.Output == "3\n", "unwritable cache prevented source execution");
		Require(Result.Diagnostics.find("failed to write bytecode cache") != std::string::npos,
		    "explicit source cache was not attempted");

		FCacheDirectory Cache;
		auto CachedOptions = Options();
		CachedOptions.CacheDirectory = Utf8Path(Cache.Path);
		const std::string CachedSource = "import \"a.vs\"; void main(){print(value());}";
		FRunner Cached(CachedOptions);
		Result = Cached.Execute(CachedSource, Resource("cached.vs"));
		Require(Result.bSuccess && Result.Output == "42\n", Result.Diagnostics + Result.Error);
		std::vector<std::filesystem::path> CacheFiles;
		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Cache.Path))
		{
			if (Entry.is_regular_file())
			{
				CacheFiles.push_back(Entry.path());
			}
		}
		Require(CacheFiles.size() == 1, "mobile source execution did not create its bytecode cache");
		const auto& CacheFile = CacheFiles.front();
		std::filesystem::last_write_time(CacheFile,
		    std::filesystem::file_time_type::clock::now() - std::chrono::hours(24));
		const auto SavedTime = std::filesystem::last_write_time(CacheFile);
		Result = Cached.Execute(CachedSource, Resource("cached.vs"));
		Require(Result.bSuccess && Result.Output == "42\n" && std::filesystem::last_write_time(CacheFile) == SavedTime, "valid mobile cache was not reused");
		// Keep both catalogs alive so cache reuse cannot depend on the allocator
		// returning the same address for successive short-lived runner sessions.
		FSession FirstCachedSession(CachedOptions);
		FSession SecondCachedSession(CachedOptions);
		for (auto* Session : {&FirstCachedSession, &SecondCachedSession})
		{
			Result = Session->Execute(CachedSource, Resource("cached.vs"));
			Require(Result.bSuccess && Result.Output == "42\n" && std::filesystem::last_write_time(CacheFile) == SavedTime,
			    "mobile cache must be reusable across distinct live sessions");
		}
		std::ofstream(CacheFile, std::ios::binary | std::ios::trunc) << "broken";
		Result = Cached.Execute(CachedSource, Resource("cached.vs"));
		Require(Result.bSuccess && Result.Output == "42\n" && std::filesystem::file_size(CacheFile) > 6,
		    "damaged mobile cache was not safely recompiled");
		CachedOptions.Sources[1].Source = "export int dep(){return 43;}";
		FRunner ChangedImport(CachedOptions);
		Result = ChangedImport.Execute(CachedSource, Resource("cached.vs"));
		Require(Result.bSuccess && Result.Output == "43\n", "cache ignored a changed imported resource");
		Result = ChangedImport.Execute("void main(){print(100);}", Resource("cached.vs"));
		Require(Result.bSuccess && Result.Output == "100\n", "cache ignored a changed entry source");
		FSession PreCancelledCache(CachedOptions);
		PreCancelledCache.RequestCancel();
		Result = PreCancelledCache.Execute("void main(){print(100);}", Resource("cached.vs"));
		Require(!Result.bSuccess && Result.Output.empty(), "cache loading discarded pending cancellation");
		for (int Attempt = 0; Attempt < 2; ++Attempt)
		{
			Result = Cached.Execute("void main(){while(true){}}", Resource("cached-limit.vs"));
			Require(!Result.bSuccess &&
			        (Result.Error.find("diag.runtime_execution_instruction_limit_exceeded") != std::string::npos ||
			            Result.Error.find("execution instruction limit exceeded") != std::string::npos),
			    "cached mobile execution bypassed its instruction budget: " + Result.Diagnostics + Result.Error);
			Result = Cached.Execute("import \"io.vs\"; void main(){std::io::read_line();}", Resource("cached-input.vs"));
			Require(!Result.bSuccess && Result.Error.find("console input capability") != std::string::npos,
			    "cached mobile input policy failed: " + Result.Diagnostics + Result.Error);
		}

		auto Small = Options();
		Small.MaxCapturedOutputBytes = 5;
		FRunner Bounded(std::move(Small));
		Result = Bounded.Execute("void main(){print(\"123456789\");print(\"again\");}", Resource("main.vs"));
		Require(Result.bSuccess && Result.bOutputTruncated && Result.Output.size() == 5, "output capture exceeded its budget");

		auto Unlimited = Options();
		Unlimited.MaxInstructions = 0;
		#if defined(VEROSCRIPT_TEST_EMULATOR)
		// Cancellation must finish before the independent execution timeout.
		Unlimited.TimeoutMilliseconds = 30000;
		#endif
		FRunner Cancelled(std::move(Unlimited));
		auto Worker = std::async(std::launch::async, [&Cancelled]
		{
			return Cancelled.Execute("void main(){while(true){}}", Resource("cancel.vs"));
		});
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!Cancelled.IsExecuting() && std::chrono::steady_clock::now() < Deadline)
		{
			std::this_thread::yield();
		}
		Require(Cancelled.IsExecuting(), "mobile worker did not start");
		bool bRejectedConcurrent = false;
		try
		{
			Cancelled.Execute("void main(){}", Resource("parallel.vs"));
		}
		catch (const std::logic_error&)
		{
			bRejectedConcurrent = true;
		}
		Require(bRejectedConcurrent, "one mobile runner accepted concurrent execution");
		Cancelled.RequestCancel();
		#if defined(VEROSCRIPT_TEST_EMULATOR)
		constexpr auto CancellationBudget = std::chrono::seconds(10);
		#else
		constexpr auto CancellationBudget = std::chrono::seconds(1);
		#endif
		Require(Worker.wait_for(CancellationBudget) == std::future_status::ready, "mobile cancellation did not settle before the timeout budget");
		Require(!Worker.get().bSuccess && !Cancelled.IsExecuting(), "cancelled execution did not release its owner session");
		Require(Cancelled.Execute("void main(){}", Resource("next.vs")).bSuccess, "cancellation leaked into the next run");

		#if defined(__APPLE__)
		// Fix the caller stack at 512 KiB instead of relying on libc++ defaults.
		// Valid expressions, parser-limit errors and recovery must all survive it.
		FRunner SmallStackRunner(Options());
		using FSmallStackTask = std::packaged_task<void()>;
		FSmallStackTask SmallStackTask([&]
		{
			Require(pthread_get_stacksize_np(pthread_self()) == 512 * 1024, "mobile regression caller stack is not 512 KiB");
			for (const std::size_t Depth : {200u, 512u})
			{
				const auto Source = "void main(){print(" + std::string(Depth, '(') + "42" + std::string(Depth, ')') + ");}";
				const auto Nested = SmallStackRunner.Execute(Source, Resource("nested.vs"));
				Require(Depth == 200 ? Nested.bSuccess && Nested.Output == "42\n" : !Nested.bSuccess && !Nested.Diagnostics.empty(),
				    "mobile worker lost bounded parsing or parser-limit diagnostics");
			}
			bool bForwarded = false;
			try
			{
				SmallStackRunner.Execute("void main(){}", "relative.vs");
			}
			catch (const std::invalid_argument&)
			{
				bForwarded = true;
			}
			Require(bForwarded && !SmallStackRunner.IsExecuting(), "mobile worker lost its exception or execution cleanup");
			const auto Recovered = SmallStackRunner.Execute("void main(){print(7);}", Resource("recovered.vs"));
			Require(Recovered.bSuccess && Recovered.Output == "7\n", "mobile worker failed to recover after compilation errors");
		});
		auto SmallStackResult = SmallStackTask.get_future();
		pthread_attr_t StackAttributes;
		int StackError = pthread_attr_init(&StackAttributes);
		Require(StackError == 0, "mobile small-stack thread attributes failed");
		StackError = pthread_attr_setstacksize(&StackAttributes, 512 * 1024);
		pthread_t StackThread{};
		if (!StackError)
		{
			StackError = pthread_create(&StackThread, &StackAttributes, [](void* Data) noexcept -> void*
			{
				(*static_cast<FSmallStackTask*>(Data))();
				return nullptr;
			}, &SmallStackTask);
		}
		pthread_attr_destroy(&StackAttributes);
		Require(StackError == 0, "mobile small-stack regression thread creation failed");
		// Do not unwind the borrowed task if an unexpected join failure leaves it running.
		if (pthread_join(StackThread, nullptr))
		{
			std::terminate();
		}
		SmallStackResult.get();
		#endif

		FSession Session(Options());
		auto WrongThread = std::async(std::launch::async, [&Session]
		{
			try
			{
				Session.Execute("void main(){}", Resource("wrong.vs"));
			}
			catch (const std::logic_error&)
			{
				return true;
			}
			return false;
		});
		Require(WrongThread.get(), "mobile session accepted a non-owner execution");
		Result = Session.Execute("void main(){auto b=new "
		                         "std::memory::buffer(8);b.set(0,7);assert(b.get(0)==7u);assert(!std::memory:"
		                         ":native_addresses_available());}",
		    Resource("memory.vs"));
		Require(Result.bSuccess, "mobile managed memory and address capability");
		auto BudgetOptions = Options();
		BudgetOptions.MaxBufferBytes = 4;
		FSession Budgeted(BudgetOptions);
		Result = Budgeted.Execute("void main(){auto b=new std::memory::buffer(5);}", Resource("quota.vs"));
		Require(!Result.bSuccess, "mobile buffer quota");
		Session.RequestCancel();
		Result = Session.Execute("void main(){print(99);}", Resource("pre-cancelled.vs"));
		Require(!Result.bSuccess && Result.Output.empty(), "source loading discarded a pending mobile cancellation");
		vs_interop_buffer* CatalogError = nullptr;
		const std::string CatalogDirectory = VEROSCRIPT_TEST_MOBILE_CATALOGS;
		Require(vs_interop_locale_load_catalog_directory(
		            {CatalogDirectory.data(), CatalogDirectory.size()}, &CatalogError) == VS_INTEROP_STATUS_OK,
		    "load explicit mobile catalogs");
		vs_interop_buffer_destroy(CatalogError);
		for (const char* Tag : {"en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"})
		{
			Require(vs_interop_locale_set_language({Tag, std::char_traits<char>::length(Tag)}) == VS_INTEROP_STATUS_OK, "select mobile diagnostic language");
			const auto Message = TranslateMessage("mobile session operation failed");
			Require(!Message.empty() && (std::string(Tag) == "en-US" || Message != "mobile session operation failed"), "mobile error is localized");
			try
			{
				auto Invalid = Options();
				Invalid.MaxCallDepth = 0;
				FRunner Rejected(std::move(Invalid));
				Require(false, "invalid runner options accepted");
			}
			catch (const std::invalid_argument& Error)
			{
				Require(std::string(Error.what()) == TranslateMessage("mobile call depth must be positive and resource count cannot exceed 4096"),
				    "mobile validation uses selected catalog");
			}
		}
		Require(TranslateMessage(std::string("opaque\0message", 14)) == std::string("opaque\0message", 14),
		    "translation preserves unknown bytes");
		(void)vs_interop_locale_set_language({"en-US", 5});
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
