// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMobile.h"

#include <jni.h>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <unordered_map>

namespace
{
	using namespace VeroScript::Mobile;

	std::mutex CatalogMutex;
	std::unordered_map<jlong, std::shared_ptr<const FAotProgramCatalog>> Catalogs;
	jlong NextCatalog = 1;

	std::string ReadBytes(JNIEnv* Env, jbyteArray Value, jsize Limit = 16 * 1024 * 1024)
	{
		if (!Value)
		{
			throw std::invalid_argument("null UTF-8 byte array");
		}
		const auto Size = Env->GetArrayLength(Value);
		if (Size > Limit)
		{
			throw std::invalid_argument("mobile UTF-8 input exceeds 16 MiB");
		}
		std::string Text(static_cast<std::size_t>(Size), '\0');
		Env->GetByteArrayRegion(Value, 0, Size, reinterpret_cast<jbyte*>(Text.data()));
		if (Env->ExceptionCheck())
		{
			throw std::runtime_error("cannot copy UTF-8 input");
		}
		return Text;
	}

	jbyteArray WriteBytes(JNIEnv* Env, const std::string& Text)
	{
		if (Text.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
		{
			throw std::length_error("mobile output is too large for Java");
		}
		auto Array = Env->NewByteArray(static_cast<jsize>(Text.size()));
		if (!Array)
		{
			throw std::bad_alloc();
		}
		Env->SetByteArrayRegion(Array, 0, static_cast<jsize>(Text.size()), reinterpret_cast<const jbyte*>(Text.data()));
		if (Env->ExceptionCheck())
		{
			throw std::runtime_error("cannot copy mobile output");
		}
		return Array;
	}

	void ThrowUtf8Exception(JNIEnv* Env, const char* Message) noexcept
	{
		std::string Localized;
		try
		{
			Localized = TranslateMessage(Message);
			Message = Localized.c_str();
		}
		catch (...)
		{
			// Preserve the original error if allocating a translated message fails.
		}
		if (Env->PushLocalFrame(8) < 0) return;
		const auto Length = std::strlen(Message);
		auto Type = Env->FindClass("java/lang/IllegalStateException");
		const auto Throw = [&]
		{
			if (!Type || Length > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) return;
			auto StringType = Env->FindClass("java/lang/String");
			if (!StringType) return;
			auto Data = Env->NewByteArray(static_cast<jsize>(Length));
			if (!Data) return;
			Env->SetByteArrayRegion(Data, 0, static_cast<jsize>(Length), reinterpret_cast<const jbyte*>(Message));
			if (Env->ExceptionCheck()) return;
			auto Encoding = Env->NewStringUTF("UTF-8");
			if (!Encoding) return;
			auto StringConstructor = Env->GetMethodID(StringType, "<init>", "([BLjava/lang/String;)V");
			if (!StringConstructor) return;
			auto Constructor = Env->GetMethodID(Type, "<init>", "(Ljava/lang/String;)V");
			if (!Constructor) return;
			auto Text = Env->NewObject(StringType, StringConstructor, Data, Encoding);
			if (!Text || Env->ExceptionCheck()) return;
			auto Exception = static_cast<jthrowable>(Env->NewObject(Type, Constructor, Text));
			if (Exception && !Env->ExceptionCheck())
			{
				Env->Throw(Exception);
			}
		};
		Throw();
		if (!Env->ExceptionCheck() && Type)
		{
			Env->ThrowNew(Type, "VeroScript native operation failed");
		}
		Env->PopLocalFrame(nullptr);
	}

	void TranslateException(JNIEnv* Env) noexcept
	{
		if (Env->ExceptionCheck()) return;
		try
		{
			throw;
		}
		catch (const std::bad_alloc&)
		{
			auto Type = Env->FindClass("java/lang/OutOfMemoryError");
			if (Type)
			{
				Env->ThrowNew(Type, "VeroScript allocation failed");
			}
		}
		catch (const std::exception& Error)
		{
			ThrowUtf8Exception(Env, Error.what());
		}
		catch (...)
		{
			ThrowUtf8Exception(Env, "VeroScript native operation failed");
		}
	}

	FRunner& Runner(const jlong Handle)
	{
		if (!Handle)
		{
			throw std::invalid_argument("closed VeroScript runner");
		}
		return *reinterpret_cast<FRunner*>(static_cast<std::uintptr_t>(Handle));
	}
}

extern "C" JNIEXPORT jlong JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeCreate(
    JNIEnv* Env, jclass, jobjectArray Paths, jobjectArray Sources, jbyteArray CacheDirectory,
    jlong Instructions, jlong Timeout, jlong Depth, jlong MaxBuffer, jlong MaxRetained,
    jint Backend, jbyteArray VmPath, jbyteArray JitPath, jlong Catalog, jbyteArray AotId)
{
	try
	{
		if (!Paths || !Sources || Env->GetArrayLength(Paths) != Env->GetArrayLength(Sources) ||
		    Env->GetArrayLength(Paths) > 4096 || Instructions < 0 || Timeout < 0 || Depth <= 0)
		{
			throw std::invalid_argument("invalid mobile configuration");
		}
		FSessionOptions Options;
		Options.StdlibDirectory = "/veroscript/stdlib";
		Options.CacheDirectory = ReadBytes(Env, CacheDirectory);
		Options.ExecutionBackend = static_cast<vs_interop_execution_backend>(Backend);
		Options.JitBackendPath = ReadBytes(Env, JitPath, 4096);
		Options.VmBackendPath = ReadBytes(Env, VmPath, 4096);
		Options.AotProgramId = ReadBytes(Env, AotId, 4096);
		if (Catalog)
		{
			const std::lock_guard<std::mutex> Lock(CatalogMutex);
			const auto I = Catalogs.find(Catalog);
			if (I == Catalogs.end())
			{
				throw std::invalid_argument("closed or invalid AOT program catalog");
			}
			Options.AotPrograms = I->second;
		}
		Options.MaxInstructions = static_cast<std::uint64_t>(Instructions);
		Options.TimeoutMilliseconds = static_cast<std::uint64_t>(Timeout);
		Options.MaxCallDepth = static_cast<std::uint64_t>(Depth);
		Options.MaxBufferBytes = static_cast<std::uint64_t>(MaxBuffer);
		Options.MaxRetainedBytes = static_cast<std::uint64_t>(MaxRetained);
		std::size_t Total = 0;
		for (jsize Index = 0; Index < Env->GetArrayLength(Paths); ++Index)
		{
			auto Path = static_cast<jbyteArray>(Env->GetObjectArrayElement(Paths, Index));
			if (Env->ExceptionCheck())
			{
				throw std::runtime_error("cannot read mobile resource path");
			}
			auto Source = static_cast<jbyteArray>(Env->GetObjectArrayElement(Sources, Index));
			if (Env->ExceptionCheck() || !Path || !Source)
			{
				throw std::invalid_argument("invalid source resource arrays");
			}
			if (Env->GetArrayLength(Path) > 4096 || static_cast<std::size_t>(Env->GetArrayLength(Source)) > Options.MaxSourceBytes - Total)
			{
				throw std::invalid_argument("mobile source resources exceed the input budget");
			}
			Total += static_cast<std::size_t>(Env->GetArrayLength(Source));
			Options.Sources.push_back({ReadBytes(Env, Path), ReadBytes(Env, Source)});
			Env->DeleteLocalRef(Path);
			Env->DeleteLocalRef(Source);
		}
		return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(new FRunner(std::move(Options))));
	}
	catch (...)
	{
		TranslateException(Env);
		return 0;
	}
}

extern "C" JNIEXPORT jobject JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeExecute(
    JNIEnv* Env, jclass, jlong Handle, jbyteArray Source, jbyteArray Label)
{
	if (Env->PushLocalFrame(8) < 0) return nullptr;
	try
	{
		const auto Result = Runner(Handle).Execute(ReadBytes(Env, Source), ReadBytes(Env, Label));
		auto Type = Env->FindClass("com/vanstysanic/veroscript/android/VeroScriptEngine$Result");
		if (!Type)
		{
			throw std::runtime_error("missing Java result class");
		}
		auto Constructor = Env->GetMethodID(Type, "<init>", "(ZIZ[B[B[B)V");
		if (!Constructor)
		{
			throw std::runtime_error("missing Java result constructor");
		}
		auto Output = WriteBytes(Env, Result.Output);
		auto Diagnostics = WriteBytes(Env, Result.Diagnostics);
		auto Error = WriteBytes(Env, Result.Error);
		auto Object = Env->NewObject(Type, Constructor, static_cast<jboolean>(Result.bSuccess),
		    static_cast<jint>(Result.ExitCode), static_cast<jboolean>(Result.bOutputTruncated), Output, Diagnostics, Error);
		return Env->PopLocalFrame(Object);
	}
	catch (...)
	{
		TranslateException(Env);
		Env->PopLocalFrame(nullptr);
		return nullptr;
	}
}

extern "C" JNIEXPORT jobject JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeExecutePackage(
    JNIEnv* Env, jclass, jlong Handle, jbyteArray Source, jbyteArray Label)
{
	if (Env->PushLocalFrame(8) < 0) return nullptr;
	try
	{
		const auto Bytes = ReadBytes(Env, Source, 64 * 1024 * 1024);
		const auto Result = Runner(Handle).ExecutePackage({reinterpret_cast<const std::uint8_t*>(Bytes.data()), Bytes.size()}, ReadBytes(Env, Label, 4096));
		auto Type = Env->FindClass("com/vanstysanic/veroscript/android/VeroScriptEngine$Result");
		if (!Type)
		{
			throw std::runtime_error("missing Java result class");
		}
		auto Constructor = Env->GetMethodID(Type, "<init>", "(ZIZ[B[B[B)V");
		if (!Constructor)
		{
			throw std::runtime_error("missing Java result constructor");
		}
		auto Output = WriteBytes(Env, Result.Output);
		auto Diagnostics = WriteBytes(Env, Result.Diagnostics);
		auto Error = WriteBytes(Env, Result.Error);
		auto Object = Env->NewObject(Type, Constructor, static_cast<jboolean>(Result.bSuccess),
		    static_cast<jint>(Result.ExitCode), static_cast<jboolean>(Result.bOutputTruncated), Output, Diagnostics, Error);
		return Env->PopLocalFrame(Object);
	}
	catch (...)
	{
		TranslateException(Env);
		Env->PopLocalFrame(nullptr);
		return nullptr;
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeCancel(
    JNIEnv* Env, jclass, jlong Handle)
{
	try
	{
		Runner(Handle).RequestCancel();
	}
	catch (...)
	{
		TranslateException(Env);
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeDestroy(
    JNIEnv*, jclass, jlong Handle)
{
	delete reinterpret_cast<FRunner*>(static_cast<std::uintptr_t>(Handle));
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeIsExecuting(
    JNIEnv* Env, jclass, jlong Handle)
{
	try
	{
		return static_cast<jboolean>(Runner(Handle).IsExecuting());
	}
	catch (...)
	{
		TranslateException(Env);
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT jbyteArray JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeTranslate(
    JNIEnv* Env, jclass, jbyteArray Message)
{
	try
	{
		return WriteBytes(Env, VeroScript::Mobile::TranslateMessage(ReadBytes(Env, Message)));
	}
	catch (...)
	{
		TranslateException(Env);
		return nullptr;
	}
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeSetLanguage(
    JNIEnv* Env, jclass, jbyteArray Language)
{
	try
	{
		const auto Text = ReadBytes(Env, Language);
		return vs_interop_locale_set_language({Text.data(), Text.size()}) == VS_INTEROP_STATUS_OK;
	}
	catch (...)
	{
		TranslateException(Env);
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeLoadCatalogs(
    JNIEnv* Env, jclass, jbyteArray Directory)
{
	try
	{
		const auto Text = ReadBytes(Env, Directory);
		vs_interop_buffer* Error = nullptr;
		const auto Status = vs_interop_locale_load_catalog_directory({Text.data(), Text.size()}, &Error);
		vs_interop_buffer_destroy(Error);
		return Status == VS_INTEROP_STATUS_OK;
	}
	catch (...)
	{
		TranslateException(Env);
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeReleaseAotPrograms(
    JNIEnv*, jclass, jlong Id)
{
	std::shared_ptr<const FAotProgramCatalog> Owner;
	{
		const std::lock_guard<std::mutex> Lock(CatalogMutex);
		const auto I = Catalogs.find(Id);
		if (I != Catalogs.end())
		{
			Owner = std::move(I->second);
			Catalogs.erase(I);
		}
	}
}

extern "C" JNIEXPORT jobject JNICALL veroscript_android_create_aot_programs(JNIEnv* Env,
    const vs_interop_string_view* Ids, const vs_interop_aot_program* const* Programs, std::uint64_t Count)
{
	if (Env->PushLocalFrame(4) < 0) return nullptr;
	jlong Id = 0;
	try
	{
		if (Count > 4096 || (Count && (!Ids || !Programs)))
		{
			throw std::invalid_argument("invalid AOT program catalog");
		}
		std::vector<FAotProgramRegistration> Entries;
		Entries.reserve(static_cast<std::size_t>(Count));
		for (std::size_t I = 0; I < Count; ++I)
		{
			if (!Ids[I].data || !Ids[I].size || Ids[I].size > 4096)
			{
				throw std::invalid_argument("invalid AOT program ID");
			}
			Entries.push_back({std::string(Ids[I].data, static_cast<std::size_t>(Ids[I].size)), RetainAotProgram(Programs[I])});
		}
		auto Catalog = std::make_shared<const FAotProgramCatalog>(std::move(Entries));
		auto Type = Env->FindClass("com/vanstysanic/veroscript/android/VeroScriptEngine$AotPrograms");
		if (!Type)
		{
			throw std::runtime_error("missing Java AOT program catalog class");
		}
		auto Constructor = Env->GetMethodID(Type, "<init>", "(J)V");
		if (!Constructor)
		{
			throw std::runtime_error("missing Java AOT catalog constructor");
		}
		{
			const std::lock_guard<std::mutex> Lock(CatalogMutex);
			if (NextCatalog == std::numeric_limits<jlong>::max())
			{
				throw std::overflow_error("AOT catalog IDs exhausted");
			}
			Id = NextCatalog++;
			Catalogs.emplace(Id, std::move(Catalog));
		}
		auto Object = Env->NewObject(Type, Constructor, Id);
		if (!Object || Env->ExceptionCheck())
		{
			throw std::runtime_error("cannot create Java AOT catalog");
		}
		return Env->PopLocalFrame(Object);
	}
	catch (...)
	{
		if (Id)
		{
			Java_com_vanstysanic_veroscript_android_VeroScriptEngine_nativeReleaseAotPrograms(Env, nullptr, Id);
		}
		TranslateException(Env);
		Env->PopLocalFrame(nullptr);
		return nullptr;
	}
}
