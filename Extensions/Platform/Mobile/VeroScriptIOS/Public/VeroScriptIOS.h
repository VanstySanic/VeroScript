// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#import <Foundation/Foundation.h>
#include "VeroScriptInterop.h"

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSErrorDomain const VSMErrorDomain;

typedef NS_ENUM(NSUInteger, VSMExecutionBackend) {
	VSMExecutionBackendVM = 0,
	VSMExecutionBackendMixed = 1,
};
typedef vs_interop_status (*VSMAotProgramFactory)(
	vs_interop_aot_program* _Nullable* _Nonnull, vs_interop_buffer* _Nullable* _Nonnull);

@interface VSMAotProgram : NSObject
+ (nullable instancetype)programWithFactory:(VSMAotProgramFactory)factory error:(NSError* _Nullable* _Nullable)error;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

@interface VSMConfiguration : NSObject
@property(nonatomic, copy) NSString* stdlibDirectory;
@property(nonatomic, copy, nullable) NSString* cacheDirectory;
// Empty/nil keeps the statically assembled VM; a nonempty path selects a compatible native backend.
@property(nonatomic, copy, nullable) NSString* vmBackendPath;
// Absolute virtual/file labels mapped to standard UTF-8 source bytes.
@property(nonatomic, copy) NSDictionary<NSString*, NSData*>* sourceResources;
@property(nonatomic) uint64_t maxInstructions;
@property(nonatomic) uint64_t timeoutMilliseconds;
@property(nonatomic) uint64_t maxCallDepth;
@property(nonatomic) uint64_t maxBufferBytes;
@property(nonatomic) uint64_t maxRetainedBytes;
@property(nonatomic) VSMExecutionBackend executionBackend;
@property(nonatomic, copy) NSDictionary<NSString*, VSMAotProgram*>* aotPrograms;
@property(nonatomic, copy, nullable) NSString* aotProgramId;
@end

@interface VSMExecutionResult : NSObject
@property(nonatomic, readonly) BOOL success;
@property(nonatomic, readonly) BOOL outputTruncated;
@property(nonatomic, readonly) int32_t exitCode;
// Raw bytes preserve UTF-8 and embedded NUL; a truncated tail may be incomplete.
@property(nonatomic, copy, readonly) NSData* outputUTF8;
@property(nonatomic, copy, readonly) NSData* diagnosticsUTF8;
@property(nonatomic, copy, readonly) NSString* errorMessage;
@end

/**
 * Configuration is copied on initialization. Execute on an application worker
 * queue. One execution at a time; each uses a fresh Core owner-thread session.
 * ARC keeps this object alive during execute/cancel. Calls are synchronous and
 * perform no UI dispatch. requestCancel is cooperative, including host callbacks.
 */
@interface VSMEngine : NSObject
- (nullable instancetype)initWithConfiguration:(VSMConfiguration*)configuration error:(NSError* _Nullable* _Nullable)error;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (nullable VSMExecutionResult*)executeSource:(NSData*)source fileLabel:(NSString*)fileLabel error:(NSError* _Nullable* _Nullable)error;
- (nullable VSMExecutionResult*)executePackage:(NSData*)package fileLabel:(NSString*)fileLabel error:(NSError* _Nullable* _Nullable)error;
- (void)requestCancel;
@end

NS_ASSUME_NONNULL_END
