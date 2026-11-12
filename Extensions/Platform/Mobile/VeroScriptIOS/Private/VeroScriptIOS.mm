// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#import "VeroScriptIOS.h"
#include "VeroScriptMobile.h"

#include <stdexcept>

NSErrorDomain const VSMErrorDomain = @"dev.veroscript.mobile";

namespace
{
	std::string Bytes(NSData* Data)
	{
		if (!Data || !Data.length) return {};
		return {static_cast<const char*>(Data.bytes), static_cast<std::size_t>(Data.length)};
	}

	std::string Utf8(NSString* Text)
	{
		NSData* Data = [Text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
		if (!Data)
		{
			throw std::invalid_argument("mobile text cannot be encoded as UTF-8");
		}
		return Bytes(Data);
	}

	void SetError(NSError** Error, const char* Message)
	{
		if (Error)
		{
			std::string Localized;
			try
			{
				Localized = VeroScript::Mobile::TranslateMessage(Message);
			}
			catch (...)
			{
			}
			if (!Localized.empty())
			{
				Message = Localized.c_str();
			}
			NSString* Text = [NSString stringWithUTF8String:Message] ?: @"VeroScript mobile operation failed";
			*Error = [NSError errorWithDomain:VSMErrorDomain code:1 userInfo:@{NSLocalizedDescriptionKey : Text}];
		}
	}
}

@interface VSMAotProgram ()
- (instancetype)initWithNativeProgram:(const vs_interop_aot_program*)program;
- (const vs_interop_aot_program*)nativeProgram;
@end

@implementation VSMAotProgram
{
	std::shared_ptr<vs_interop_aot_program> _program;
}
- (instancetype)initWithNativeProgram:(const vs_interop_aot_program*)program
{
	self = [super init];
	if (self)
	{
		_program = VeroScript::Mobile::RetainAotProgram(program);
	}
	return self;
}
- (const vs_interop_aot_program*)nativeProgram
{
	return _program.get();
}
+ (nullable instancetype)programWithFactory:(VSMAotProgramFactory)factory error:(NSError**)error
{
	try
	{
		if (!factory)
		{
			throw std::invalid_argument("AOT program factory is required");
		}
		vs_interop_aot_program* Program = nullptr;
		vs_interop_buffer* Failure = nullptr;
		const auto Status = factory(&Program, &Failure);
		const std::unique_ptr<vs_interop_aot_program, decltype(&vs_interop_aot_program_release)> Owner(Program, &vs_interop_aot_program_release);
		const std::unique_ptr<vs_interop_buffer, decltype(&vs_interop_buffer_destroy)> ErrorOwner(Failure, &vs_interop_buffer_destroy);
		if (Status != VS_INTEROP_STATUS_OK || !Program)
		{
			if (Failure)
			{
				const auto View = vs_interop_buffer_view(Failure);
				throw std::runtime_error(std::string(View.size ? reinterpret_cast<const char*>(View.data) : "", static_cast<std::size_t>(View.size)));
			}
			throw std::runtime_error("cannot create AOT program");
		}
		return [[self alloc] initWithNativeProgram:Program];
	}
	catch (const std::exception& Exception)
	{
		SetError(error, Exception.what());
	}
	catch (...)
	{
		SetError(error, "cannot create AOT program");
	}
	return nil;
}
@end

@implementation VSMConfiguration
- (instancetype)init
{
	self = [super init];
	if (self)
	{
		_stdlibDirectory = @"/veroscript/stdlib";
		_sourceResources = @{};
		_aotPrograms = @{};
		_executionBackend = VSMExecutionBackendVM;
		_maxInstructions = 1000000;
		_timeoutMilliseconds = 100;
		_maxCallDepth = 256;
		_maxBufferBytes = 16 * 1024 * 1024;
		_maxRetainedBytes = 32 * 1024 * 1024;
	}
	return self;
}
@end

@interface VSMExecutionResult ()
@property(nonatomic, readwrite) BOOL success;
@property(nonatomic, readwrite) BOOL outputTruncated;
@property(nonatomic, readwrite) int32_t exitCode;
@property(nonatomic, copy, readwrite) NSData* outputUTF8;
@property(nonatomic, copy, readwrite) NSData* diagnosticsUTF8;
@property(nonatomic, copy, readwrite) NSString* errorMessage;
@end

@implementation VSMExecutionResult
@end

@interface VSMEngine ()
- (nullable VSMExecutionResult*)executeData:(NSData*)source fileLabel:(NSString*)fileLabel package:(BOOL)isPackage error:(NSError**)error;
@end

@implementation VSMEngine
{
	std::unique_ptr<VeroScript::Mobile::FRunner> _runner;
}

- (nullable instancetype)initWithConfiguration:(VSMConfiguration*)configuration error:(NSError**)error
{
	self = [super init];
	if (!self) return nil;
	try
	{
		VeroScript::Mobile::FSessionOptions Options;
		Options.StdlibDirectory = Utf8(configuration.stdlibDirectory);
		if (configuration.vmBackendPath)
		{
			Options.VmBackendPath = Utf8(configuration.vmBackendPath);
		}
		if (configuration.executionBackend != VSMExecutionBackendVM && configuration.executionBackend != VSMExecutionBackendMixed)
		{
			throw std::invalid_argument("iOS supports VM or static AOT mixed execution");
		}
		Options.ExecutionBackend = static_cast<vs_interop_execution_backend>(configuration.executionBackend);
		if (configuration.aotProgramId)
		{
			Options.AotProgramId = Utf8(configuration.aotProgramId);
		}
		std::vector<VeroScript::Mobile::FAotProgramRegistration> Registrations;
		for (NSString *Id in configuration.aotPrograms)
		{
			Registrations.push_back({Utf8(Id), VeroScript::Mobile::RetainAotProgram([configuration.aotPrograms[Id] nativeProgram])});
		}
		Options.AotPrograms = std::make_shared<const VeroScript::Mobile::FAotProgramCatalog>(std::move(Registrations));
		if (configuration.cacheDirectory)
		{
			Options.CacheDirectory = Utf8(configuration.cacheDirectory);
		}
		Options.MaxInstructions = configuration.maxInstructions;
		Options.TimeoutMilliseconds = configuration.timeoutMilliseconds;
		Options.MaxCallDepth = configuration.maxCallDepth;
		Options.MaxBufferBytes = configuration.maxBufferBytes;
		Options.MaxRetainedBytes = configuration.maxRetainedBytes;
		std::size_t Total = 0;
		if (configuration.sourceResources.count > 4096)
		{
			throw std::invalid_argument("too many mobile source resources");
		}
		for (NSString* Path in configuration.sourceResources)
		{
			NSData* Source = configuration.sourceResources[Path];
			if (Source.length > Options.MaxSourceBytes - Total)
			{
				throw std::invalid_argument("mobile source resources exceed 16 MiB");
			}
			Total += Source.length;
			Options.Sources.push_back({Utf8(Path), Bytes(Source)});
		}
		_runner = std::make_unique<VeroScript::Mobile::FRunner>(std::move(Options));
		return self;
	}
	catch (const std::exception& Exception)
	{
		SetError(error, Exception.what());
	}
	catch (...)
	{
		SetError(error, "VeroScript mobile initialization failed");
	}
	return nil;
}

- (nullable VSMExecutionResult*)executeSource:(NSData*)source fileLabel:(NSString*)fileLabel error:(NSError**)error
{
	return [self executeData:source fileLabel:fileLabel package:NO error:error];
}

- (nullable VSMExecutionResult*)executePackage:(NSData*)package fileLabel:(NSString*)fileLabel error:(NSError**)error
{
	return [self executeData:package fileLabel:fileLabel package:YES error:error];
}

- (nullable VSMExecutionResult*)executeData:(NSData*)source fileLabel:(NSString*)fileLabel package:(BOOL)isPackage error:(NSError**)error
{
	// Objective-C message dispatch does not itself retain self for the whole call.
	__attribute__((objc_precise_lifetime)) VSMEngine* Lifetime = self;
	try
	{
		if (source.length > (isPackage ? 64u : 16u) * 1024 * 1024)
		{
			throw std::invalid_argument("mobile entry source exceeds 16 MiB");
		}
		const auto Input = Bytes(source);
		const auto Label = Utf8(fileLabel);
		const auto Result = isPackage ? Lifetime->_runner->ExecutePackage({reinterpret_cast<const std::uint8_t*>(Input.data()), Input.size()}, Label)
		                              : Lifetime->_runner->Execute(Input, Label);
		VSMExecutionResult* Output = [VSMExecutionResult new];
		Output.success = Result.bSuccess;
		Output.exitCode = Result.ExitCode;
		Output.outputTruncated = Result.bOutputTruncated;
		Output.outputUTF8 = [NSData dataWithBytes:Result.Output.data() length:Result.Output.size()];
		Output.diagnosticsUTF8 = [NSData dataWithBytes:Result.Diagnostics.data() length:Result.Diagnostics.size()];
		Output.errorMessage = [[NSString alloc] initWithBytes:Result.Error.data()
		                                               length:Result.Error.size()
		                                             encoding:NSUTF8StringEncoding]
		    ?: @"VeroScript execution failed";
		return Output;
	}
	catch (const std::exception& Exception)
	{
		SetError(error, Exception.what());
	}
	catch (...)
	{
		SetError(error, "VeroScript mobile execution failed");
	}
	return nil;
}

- (void)requestCancel
{
	__attribute__((objc_precise_lifetime)) VSMEngine* Lifetime = self;
	Lifetime->_runner->RequestCancel();
}
@end
