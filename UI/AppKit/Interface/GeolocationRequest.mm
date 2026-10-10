/*
 * Copyright (c) 2026, Ladybird contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#import <Interface/GeolocationRequest.h>

#import <CoreLocation/CoreLocation.h>
#import <CommonCrypto/CommonDigest.h>

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

@interface GeolocationRequest () <CLLocationManagerDelegate>

@property (nonatomic, copy) NSString* origin;
@property (nonatomic) uint32_t navigationGeneration;
@property (nonatomic) uint64_t requestId;
@property (nonatomic) uint32_t timeoutMs;
@property (nonatomic, copy) LadybirdGeolocationCompletion completion;
@property (nonatomic, strong) NSWindow* parentWindow;
@property (nonatomic, strong) NSAlert* alert;
@property (nonatomic, strong) CLLocationManager* locationManager;
@property (nonatomic) BOOL finished;

@end

@implementation GeolocationRequest

- (instancetype)initWithOrigin:(NSString*)origin
          navigationGeneration:(uint32_t)navigation_generation
                    requestId:(uint64_t)request_id
                    timeoutMs:(uint32_t)timeout_ms
                   completion:(LadybirdGeolocationCompletion)completion
{
    if (self = [super init]) {
        self.origin = origin;
        self.navigationGeneration = navigation_generation;
        self.requestId = request_id;
        self.timeoutMs = timeout_ms;
        self.completion = completion;
    }
    return self;
}

- (NSString*)permissionKey
{
    auto* bytes = [self.origin dataUsingEncoding:NSUTF8StringEncoding];
    if (bytes == nil || [bytes length] > UINT32_MAX)
        return nil;

    unsigned char digest[CC_SHA256_DIGEST_LENGTH] {};
    CC_SHA256([bytes bytes], static_cast<CC_LONG>([bytes length]), digest);
    NSMutableString* key = [NSMutableString stringWithString:@"site_permissions/geolocation/"];
    for (NSUInteger index = 0; index < CC_SHA256_DIGEST_LENGTH; ++index)
        [key appendFormat:@"%02x", digest[index]];
    return key;
}

- (void)startForWindow:(NSWindow*)window
{
    if (self.finished)
        return;
    self.parentWindow = window;
    if (window == nil) {
        [self finishAllowed:NO location:nil];
        return;
    }

    uint64_t bounded_timeout_ms = self.timeoutMs == 0 ? 1 : self.timeoutMs;
    bounded_timeout_ms = std::min<uint64_t>(bounded_timeout_ms, 120'000) + 1'000;
    __weak GeolocationRequest* weak_self = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(bounded_timeout_ms * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        [weak_self finishAllowed:YES location:nil];
    });

    NSString* key = [self permissionKey];
    if (key == nil) {
        [self finishAllowed:NO location:nil];
        return;
    }
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    id saved_decision = [defaults objectForKey:key];
    if ([saved_decision isKindOfClass:[NSNumber class]]) {
        if ([saved_decision boolValue])
            [self startNativeLocationRequest];
        else
            [self finishAllowed:NO location:nil];
        return;
    }

    self.alert = [[NSAlert alloc] init];
    [self.alert setMessageText:@"Location access"];
    [self.alert setInformativeText:[NSString stringWithFormat:@"%@ wants to access your location.", self.origin]];
    [[self.alert addButtonWithTitle:@"Block"] setTag:NSAlertFirstButtonReturn];
    [[self.alert addButtonWithTitle:@"Allow"] setTag:NSAlertSecondButtonReturn];
    [self.alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
        if (self.finished)
            return;
        bool allowed = response == NSAlertSecondButtonReturn;
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        [defaults setObject:@(allowed) forKey:key];
        if (![defaults synchronize]) {
            [self finishAllowed:NO location:nil];
            return;
        }
        self.alert = nil;
        if (allowed)
            [self startNativeLocationRequest];
        else
            [self finishAllowed:NO location:nil];
    }];
}

- (void)startNativeLocationRequest
{
    if (self.finished)
        return;
    self.locationManager = [[CLLocationManager alloc] init];
    self.locationManager.delegate = self;
    self.locationManager.desiredAccuracy = kCLLocationAccuracyBest;

    auto status = self.locationManager.authorizationStatus;
    if (status == kCLAuthorizationStatusNotDetermined) {
        [self.locationManager requestWhenInUseAuthorization];
        return;
    }
    if (status != kCLAuthorizationStatusAuthorizedAlways && status != kCLAuthorizationStatusAuthorizedWhenInUse) {
        [self finishAllowed:NO location:nil];
        return;
    }
    [self.locationManager requestLocation];
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager
{
    if (self.finished || manager != self.locationManager)
        return;
    auto status = manager.authorizationStatus;
    if (status == kCLAuthorizationStatusAuthorizedAlways || status == kCLAuthorizationStatusAuthorizedWhenInUse) {
        [manager requestLocation];
    } else if (status != kCLAuthorizationStatusNotDetermined) {
        [self finishAllowed:NO location:nil];
    }
}

- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations
{
    if (self.finished || manager != self.locationManager || locations.count == 0)
        return;

    CLLocation* location = locations.lastObject;
    auto const age = -[location.timestamp timeIntervalSinceNow];
    auto const horizontal_accuracy = location.horizontalAccuracy;
    if (!std::isfinite(location.coordinate.latitude) || !std::isfinite(location.coordinate.longitude) ||
        location.coordinate.latitude < -90.0 || location.coordinate.latitude > 90.0 ||
        location.coordinate.longitude < -180.0 || location.coordinate.longitude > 180.0 ||
        !std::isfinite(horizontal_accuracy) || horizontal_accuracy <= 0.0 ||
        horizontal_accuracy * 1000.0 > std::numeric_limits<uint32_t>::max() ||
        age < 0.0 || age > 30.0) {
        return;
    }
    [self finishAllowed:YES location:location];
}

- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error
{
    if (self.finished || manager != self.locationManager)
        return;
    [self finishAllowed:error.code != kCLErrorDenied location:nil];
}

- (void)finishAllowed:(BOOL)allowed location:(CLLocation*)location
{
    if (self.finished)
        return;
    self.finished = YES;
    [self.locationManager stopUpdatingLocation];
    self.locationManager.delegate = nil;
    self.locationManager = nil;
    if (self.alert != nil) {
        [self.alert.window close];
        self.alert = nil;
    }

    if (location == nil || !allowed) {
        self.completion(allowed, NO, 0, 0, 0, 0, 0, 0, 0, 0);
        return;
    }

    auto latitude_e7 = static_cast<int64_t>(std::llround(location.coordinate.latitude * 10'000'000.0));
    auto longitude_e7 = static_cast<int64_t>(std::llround(location.coordinate.longitude * 10'000'000.0));
    auto horizontal_accuracy_mm = static_cast<uint32_t>(std::ceil(location.horizontalAccuracy * 1000.0));
    int64_t altitude_mm = 0;
    uint32_t vertical_accuracy_mm = 0;
    uint32_t speed_mm_per_second = 0;
    uint32_t heading_millidegrees = 0;
    uint32_t flags = 0;

    if (location.verticalAccuracy > 0.0 && std::isfinite(location.verticalAccuracy) &&
        std::isfinite(location.altitude) && location.verticalAccuracy * 1000.0 <= std::numeric_limits<uint32_t>::max() &&
        location.altitude * 1000.0 > static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        location.altitude * 1000.0 < static_cast<double>(std::numeric_limits<int64_t>::max())) {
        altitude_mm = static_cast<int64_t>(std::llround(location.altitude * 1000.0));
        vertical_accuracy_mm = static_cast<uint32_t>(std::ceil(location.verticalAccuracy * 1000.0));
        flags |= 1;
    }
    if (location.speed >= 0.0 && std::isfinite(location.speed) &&
        location.speed * 1000.0 <= std::numeric_limits<uint32_t>::max()) {
        speed_mm_per_second = static_cast<uint32_t>(std::ceil(location.speed * 1000.0));
        flags |= 2;
    }
    if (location.course >= 0.0 && location.course < 360.0 && std::isfinite(location.course)) {
        heading_millidegrees = static_cast<uint32_t>(std::llround(location.course * 1000.0));
        flags |= 4;
    }
    self.completion(YES, YES, latitude_e7, longitude_e7, altitude_mm,
        horizontal_accuracy_mm, vertical_accuracy_mm, speed_mm_per_second,
        heading_millidegrees, flags);
}

- (void)cancel
{
    if (self.finished)
        return;
    self.finished = YES;
    [self.locationManager stopUpdatingLocation];
    self.locationManager.delegate = nil;
    self.locationManager = nil;
    if (self.alert != nil) {
        [self.alert.window close];
        self.alert = nil;
    }
    self.completion = nil;
}

@end
