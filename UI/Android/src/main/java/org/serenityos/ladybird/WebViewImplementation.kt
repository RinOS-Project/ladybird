/**
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

package org.serenityos.ladybird

import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.ServiceConnection
import android.graphics.Bitmap
import android.location.Location
import android.location.LocationManager
import android.Manifest
import android.content.pm.PackageManager
import android.os.CancellationSignal
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import android.view.View
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import java.security.MessageDigest
import kotlin.math.ceil
import kotlin.math.roundToLong

/**
 * Wrapper around WebView::ViewImplementation for use by Kotlin
 */
class WebViewImplementation(private val view: WebView) {
    // Instance Pointer to native object, very unsafe :)
    private var nativeInstance: Long = 0
    private lateinit var resourceDir: String
    private lateinit var connection: ServiceConnection
    private val mainHandler = Handler(Looper.getMainLooper())
    private val geolocationRequests = mutableMapOf<Long, PendingGeolocationRequest>()
    private var geolocationNavigationGeneration: Int = 1

    private class PendingGeolocationRequest(
        val origin: String,
        val timeoutMs: Int,
        val navigationGeneration: Int,
        val requestId: Long,
    ) {
        var dialog: AlertDialog? = null
        val cancellationSignals = mutableListOf<CancellationSignal>()
        var timeout: Runnable? = null
        var completed = false
        var pendingProviders = 0
    }

    fun initialize(resourceDir: String) {
        this.resourceDir = resourceDir
        nativeInstance = nativeObjectInit()
    }

    fun dispose() {
        geolocationRequests.values.toList().forEach(::cancelGeolocationRequest)
        nativeObjectDispose(nativeInstance)
        nativeInstance = 0
    }

    fun loadURL(url: String) {
        nativeLoadURL(nativeInstance, url)
    }

    fun drawIntoBitmap(bitmap: Bitmap) {
        nativeDrawIntoBitmap(nativeInstance, bitmap)
    }

    fun setViewportGeometry(w: Int, h: Int) {
        nativeSetViewportGeometry(nativeInstance, w, h)
    }

    fun setDevicePixelRatio(ratio: Float) {
        nativeSetDevicePixelRatio(nativeInstance, ratio)
    }

    fun mouseEvent(eventType: Int, x: Float, y: Float, rawX: Float, rawY: Float) {
        nativeMouseEvent(nativeInstance, eventType, x, y, rawX, rawY)
    }

    // Functions called from native code
    fun bindWebContentService(ipcFd: Int) {
        val connector = LadybirdServiceConnection(ipcFd, resourceDir)
        connector.onDisconnect = {
            // FIXME: Notify impl that service is dead and might need restarted
            Log.e("WebContentView", "WebContent Died! :(")
        }
        // FIXME: Unbind this at some point maybe
        view.context.bindService(
            Intent(view.context, WebContentService::class.java),
            connector,
            Context.BIND_AUTO_CREATE
        )
        connection = connector
    }

    fun invalidateLayout() {
        view.requestLayout()
        view.invalidate()
    }

    fun onLoadStart(url: String, isRedirect: Boolean, navigationGeneration: Int) {
        geolocationNavigationGeneration = navigationGeneration
        geolocationRequests.values.toList().forEach { request ->
            if (request.navigationGeneration != navigationGeneration)
                cancelGeolocationRequest(request)
        }
        view.onLoadStart(url, isRedirect)
    }

    fun requestGeolocationPermission(origin: String, timeoutMs: Int, navigationGeneration: Int, requestId: Long) {
        view.post {
            if (navigationGeneration != geolocationNavigationGeneration)
                return@post

            val request = PendingGeolocationRequest(origin, timeoutMs, navigationGeneration, requestId)
            geolocationRequests[requestId] = request
            val unsignedTimeout = timeoutMs.toLong() and 0xffff_ffffL
            val boundedTimeout = if (unsignedTimeout == 0L) 1L else minOf(unsignedTimeout, 120_000L) + 1_000L
            request.timeout = Runnable {
                finishGeolocationRequest(request, allowed = true, location = null)
            }
            mainHandler.postDelayed(request.timeout!!, boundedTimeout)

            val preferences = view.context.getSharedPreferences("site-permissions", Context.MODE_PRIVATE)
            val key = "geolocation/" + sha256Hex(origin)
            if (preferences.contains(key)) {
                if (preferences.getBoolean(key, false))
                    requestPlatformGeolocation(request)
                else
                    finishGeolocationRequest(request, allowed = false, location = null)
                return@post
            }

            val dialog = AlertDialog.Builder(view.context)
                .setTitle("Location access")
                .setMessage("$origin wants to access your location.")
                .setPositiveButton("Allow") { _, _ -> decideGeolocationPermission(request, key, true) }
                .setNegativeButton("Block") { _, _ -> decideGeolocationPermission(request, key, false) }
                .setOnCancelListener { decideGeolocationPermission(request, key, false) }
                .create()
            request.dialog = dialog
            dialog.show()
        }
    }

    private fun decideGeolocationPermission(request: PendingGeolocationRequest, key: String, allowed: Boolean) {
        if (!isCurrentGeolocationRequest(request))
            return
        val preferences = view.context.getSharedPreferences("site-permissions", Context.MODE_PRIVATE)
        if (!preferences.edit().putBoolean(key, allowed).commit()) {
            finishGeolocationRequest(request, allowed = false, location = null)
            return
        }
        request.dialog = null
        if (allowed)
            requestPlatformGeolocation(request)
        else
            finishGeolocationRequest(request, allowed = false, location = null)
    }

    private fun requestPlatformGeolocation(request: PendingGeolocationRequest) {
        if (!isCurrentGeolocationRequest(request))
            return
        var context = view.context
        var activity: LadybirdActivity? = null
        while (context is ContextWrapper) {
            if (context is LadybirdActivity) {
                activity = context
                break
            }
            context = context.baseContext
        }
        if (activity == null) {
            finishGeolocationRequest(request, allowed = true, location = null)
            return
        }
        activity.requestLocationPermission { permissionGranted ->
            view.post {
                if (!isCurrentGeolocationRequest(request))
                    return@post
                if (!permissionGranted) {
                    finishGeolocationRequest(request, allowed = false, location = null)
                    return@post
                }
                startLocationRequest(request)
            }
        }
    }

    private fun startLocationRequest(request: PendingGeolocationRequest) {
        if (!isCurrentGeolocationRequest(request))
            return
        val context = view.context
        val manager = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        if (manager == null) {
            finishGeolocationRequest(request, allowed = true, location = null)
            return
        }
        val hasFinePermission = ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        val providers = try {
            manager.getProviders(true).filter { provider ->
                (provider == LocationManager.NETWORK_PROVIDER ||
                    (hasFinePermission && provider == LocationManager.GPS_PROVIDER))
            }
        } catch (_: SecurityException) {
            emptyList()
        }
        if (providers.isEmpty()) {
            finishGeolocationRequest(request, allowed = true, location = null)
            return
        }

        request.pendingProviders = providers.size
        providers.forEach { provider ->
            val cancellationSignal = CancellationSignal()
            request.cancellationSignals.add(cancellationSignal)
            try {
                manager.getCurrentLocation(provider, cancellationSignal, ContextCompat.getMainExecutor(context)) { location ->
                    if (!isCurrentGeolocationRequest(request))
                        return@getCurrentLocation
                    if (location != null && isValidHorizontalFix(location)) {
                        finishGeolocationRequest(request, allowed = true, location = location)
                        return@getCurrentLocation
                    }
                    request.pendingProviders -= 1
                    if (request.pendingProviders == 0)
                        finishGeolocationRequest(request, allowed = true, location = null)
                }
            } catch (_: SecurityException) {
                request.pendingProviders -= 1
                if (request.pendingProviders == 0)
                    finishGeolocationRequest(request, allowed = false, location = null)
            } catch (_: IllegalArgumentException) {
                request.pendingProviders -= 1
                if (request.pendingProviders == 0)
                    finishGeolocationRequest(request, allowed = true, location = null)
            }
        }
    }

    private fun isCurrentGeolocationRequest(request: PendingGeolocationRequest): Boolean =
        !request.completed && request.navigationGeneration == geolocationNavigationGeneration &&
            geolocationRequests[request.requestId] === request

    private fun isValidHorizontalFix(location: Location): Boolean =
        location.latitude.isFinite() && location.longitude.isFinite() &&
            location.latitude in -90.0..90.0 && location.longitude in -180.0..180.0 &&
            location.hasAccuracy() && location.accuracy.isFinite() && location.accuracy > 0f &&
            ceil(location.accuracy.toDouble() * 1000.0) <= Int.MAX_VALUE.toDouble() &&
            location.elapsedRealtimeNanos > 0L &&
            SystemClock.elapsedRealtimeNanos() - location.elapsedRealtimeNanos in 0L..30_000_000_000L

    private fun cancelGeolocationRequest(request: PendingGeolocationRequest) {
        if (request.completed)
            return
        request.completed = true
        request.dialog?.dismiss()
        request.dialog = null
        request.timeout?.let(mainHandler::removeCallbacks)
        request.timeout = null
        request.cancellationSignals.forEach { signal -> signal.cancel() }
        request.cancellationSignals.clear()
        geolocationRequests.remove(request.requestId)
    }

    private fun finishGeolocationRequest(request: PendingGeolocationRequest, allowed: Boolean, location: Location?) {
        if (!isCurrentGeolocationRequest(request))
            return
        cancelGeolocationRequest(request)
        val validLocation = location?.takeIf {
            isValidHorizontalFix(it)
        }
        if (!allowed || validLocation == null) {
            nativeCompleteGeolocationPermission(nativeInstance, request.navigationGeneration, request.requestId,
                allowed, false, 0L, 0L, 0L, 0, 0, 0, 0, 0)
            return
        }

        var altitudeMm = 0L
        var verticalAccuracyMm = 0
        var speedMmPerSecond = 0
        var headingMillidegrees = 0
        var flags = 0
        val hasVerticalAccuracy = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && validLocation.hasVerticalAccuracy() &&
            validLocation.verticalAccuracyMeters.isFinite() && validLocation.verticalAccuracyMeters > 0f &&
            ceil(validLocation.verticalAccuracyMeters.toDouble() * 1000.0) <= Int.MAX_VALUE.toDouble()
        if (validLocation.hasAltitude() && hasVerticalAccuracy && validLocation.altitude.isFinite() &&
            validLocation.altitude * 1000.0 in Long.MIN_VALUE.toDouble()..Long.MAX_VALUE.toDouble()) {
            altitudeMm = (validLocation.altitude * 1000.0).roundToLong()
            verticalAccuracyMm = ceil(validLocation.verticalAccuracyMeters.toDouble() * 1000.0).toInt()
            flags = flags or 1
        }
        if (validLocation.hasSpeed() && validLocation.speed.isFinite() && validLocation.speed >= 0f &&
            ceil(validLocation.speed.toDouble() * 1000.0) <= Int.MAX_VALUE.toDouble()) {
            speedMmPerSecond = ceil(validLocation.speed.toDouble() * 1000.0).toInt()
            flags = flags or 2
        }
        if (validLocation.hasBearing() && validLocation.bearing.isFinite() && validLocation.bearing >= 0f && validLocation.bearing < 360f) {
            headingMillidegrees = (validLocation.bearing.toDouble() * 1000.0).roundToLong().toInt()
            flags = flags or 4
        }
        val accuracyMm = ceil(validLocation.accuracy.toDouble() * 1000.0).toInt()
        nativeCompleteGeolocationPermission(nativeInstance, request.navigationGeneration, request.requestId,
            true, true, (validLocation.latitude * 10_000_000.0).roundToLong(),
            (validLocation.longitude * 10_000_000.0).roundToLong(), altitudeMm,
            accuracyMm, verticalAccuracyMm, speedMmPerSecond, headingMillidegrees, flags)
    }

    private fun sha256Hex(value: String): String = MessageDigest.getInstance("SHA-256")
        .digest(value.toByteArray(Charsets.UTF_8))
        .joinToString("") { byte -> (byte.toInt() and 0xff).toString(16).padStart(2, '0') }

    // Functions implemented in native code
    private external fun nativeObjectInit(): Long
    private external fun nativeObjectDispose(instance: Long)

    private external fun nativeDrawIntoBitmap(instance: Long, bitmap: Bitmap)
    private external fun nativeSetViewportGeometry(instance: Long, w: Int, h: Int)
    private external fun nativeSetDevicePixelRatio(instance: Long, ratio: Float)
    private external fun nativeLoadURL(instance: Long, url: String)
    private external fun nativeMouseEvent(instance: Long, eventType: Int, x: Float, y: Float, rawX: Float, rawY: Float)
    private external fun nativeCompleteGeolocationPermission(
        instance: Long, navigationGeneration: Int, requestId: Long, allowed: Boolean,
        fixAvailable: Boolean, latitudeE7: Long, longitudeE7: Long, altitudeMm: Long,
        horizontalAccuracyMm: Int, verticalAccuracyMm: Int, speedMmPerSecond: Int,
        headingMillidegrees: Int, flags: Int,
    )

    companion object {
        /*
         * We use a static class initializer to allow the native code to cache some
         * field offsets. This native function looks up and caches interesting
         * class/field/method IDs. Throws on failure.
         */
        private external fun nativeClassInit()

        init {
            nativeClassInit()
        }
    }
};
