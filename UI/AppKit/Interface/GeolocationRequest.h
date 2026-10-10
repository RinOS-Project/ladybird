/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#import <Cocoa/Cocoa.h>

typedef void (^LadybirdGeolocationCompletion)(BOOL allowed, BOOL fix_available,
    int64_t latitude_e7, int64_t longitude_e7, int64_t altitude_mm,
    uint32_t horizontal_accuracy_mm, uint32_t vertical_accuracy_mm,
    uint32_t speed_mm_per_second, uint32_t heading_millidegrees, uint32_t flags);

@interface GeolocationRequest : NSObject

- (instancetype)initWithOrigin:(NSString*)origin
                navigationGeneration:(uint32_t)navigation_generation
                          requestId:(uint64_t)request_id
                          timeoutMs:(uint32_t)timeout_ms
                         completion:(LadybirdGeolocationCompletion)completion;

- (void)startForWindow:(NSWindow*)window;
- (void)cancel;

@end
