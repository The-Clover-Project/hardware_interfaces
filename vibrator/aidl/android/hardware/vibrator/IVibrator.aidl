/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.hardware.vibrator;

import android.hardware.vibrator.Braking;
import android.hardware.vibrator.CompositeEffect;
import android.hardware.vibrator.CompositePrimitive;
import android.hardware.vibrator.CompositePwleV2;
import android.hardware.vibrator.Effect;
import android.hardware.vibrator.EffectStrength;
import android.hardware.vibrator.FrequencyAccelerationMapEntry;
import android.hardware.vibrator.IVibratorCallback;
import android.hardware.vibrator.PrimitivePwle;
import android.hardware.vibrator.VendorEffect;

@VintfStability
interface IVibrator {
    /**
     * Whether on w/ IVibratorCallback can be used w/ 'on' function
     */
    const int CAP_ON_CALLBACK = 1 << 0;
    /**
     * Whether on w/ IVibratorCallback can be used w/ 'perform' function
     */
    const int CAP_PERFORM_CALLBACK = 1 << 1;
    /**
     * Whether setAmplitude is supported (when external control is disabled)
     */
    const int CAP_AMPLITUDE_CONTROL = 1 << 2;
    /**
     * Whether setExternalControl is supported.
     */
    const int CAP_EXTERNAL_CONTROL = 1 << 3;
    /**
     * Whether setAmplitude is supported (when external control is enabled)
     */
    const int CAP_EXTERNAL_AMPLITUDE_CONTROL = 1 << 4;
    /**
     * Whether compose is supported.
     */
    const int CAP_COMPOSE_EFFECTS = 1 << 5;
    /**
     * Whether alwaysOnEnable/alwaysOnDisable is supported.
     */
    const int CAP_ALWAYS_ON_CONTROL = 1 << 6;
    /**
     * Whether getResonantFrequency is supported.
     */
    const int CAP_GET_RESONANT_FREQUENCY = 1 << 7;
    /**
     * Whether getQFactor is supported.
     */
    const int CAP_GET_Q_FACTOR = 1 << 8;
    /**
     * Whether frequency control is supported.
     */
    const int CAP_FREQUENCY_CONTROL = 1 << 9;
    /**
     * Whether composePwle is supported.
     */
    const int CAP_COMPOSE_PWLE_EFFECTS = 1 << 10;
    /**
     * Whether perform w/ vendor effect is supported.
     */
    const int CAP_PERFORM_VENDOR_EFFECTS = 1 << 11;
    /**
     * Whether composePwleV2 for PwlePrimitives is supported.
     */
    const int CAP_COMPOSE_PWLE_EFFECTS_V2 = 1 << 12;

    /**
     * Determine capabilities of the vibrator HAL (CAP_* mask)
     */
    int getCapabilities();

    /**
     * Turn off vibrator
     *
     * Cancel a previously-started vibration, if any. If a previously-started vibration is
     * associated with a callback, then onComplete should still be called on that callback.
     */
    void off();

    /**
     * Turn on vibrator
     *
     * This function must only be called after the previous timeout has expired or
     * was canceled (through off()). A callback is only expected to be supported when
     * getCapabilities CAP_ON_CALLBACK is specified.
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off.
     *
     * @param timeoutMs number of milliseconds to vibrate.
     * @param callback A callback used to inform Frameworks of state change, if supported.
     */
    void on(in int timeoutMs, in IVibratorCallback callback);

    /**
     * Fire off a predefined haptic event.
     *
     * A callback is only expected to be supported when getCapabilities CAP_PERFORM_CALLBACK
     * is specified.
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off.
     *
     * @param effect The type of haptic event to trigger.
     * @param strength The intensity of haptic event to trigger.
     * @param callback A callback used to inform Frameworks of state change, if supported.
     * @return The length of time the event is expected to take in
     *     milliseconds. This doesn't need to be perfectly accurate, but should be a reasonable
     *     approximation.
     */
    int perform(in Effect effect, in EffectStrength strength, in IVibratorCallback callback);

    /**
     * List supported effects.
     *
     * Return the effects which are supported (an effect is expected to be supported at every
     * strength level.
     */
    Effect[] getSupportedEffects();

    /**
     * Sets the motor's vibrational amplitude.
     *
     * Changes the force being produced by the underlying motor. This may not be supported and
     * this support is reflected in getCapabilities (CAP_AMPLITUDE_CONTROL). When this device
     * is under external control (via setExternalControl), amplitude control may not be supported
     * even though it is supported normally. This can be checked with
     * CAP_EXTERNAL_AMPLITUDE_CONTROL.
     *
     * @param amplitude The unitless force setting. Note that this number must
     *                  be between 0.0 (exclusive) and 1.0 (inclusive). It must
     *                  do it's best to map it onto the number of steps it does have.
     */
    void setAmplitude(in float amplitude);

    /**
     * Enables/disables control override of vibrator to audio.
     *
     * Support is reflected in getCapabilities (CAP_EXTERNAL_CONTROL).
     *
     * When this API is set, the vibrator control should be ceded to audio system
     * for haptic audio. While this is enabled, issuing of other commands to control
     * the vibrator is unsupported and the resulting behavior is undefined. Amplitude
     * control may or may not be supported and is reflected in the return value of
     * getCapabilities (CAP_EXTERNAL_AMPLITUDE_CONTROL) while this is enabled. When this is
     * disabled, the vibrator should resume to an off state.
     *
     * @param enabled Whether external control should be enabled or disabled.
     */
    void setExternalControl(in boolean enabled);

    /**
     * Retrieve composition delay limit.
     *
     * Support is reflected in getCapabilities (CAP_COMPOSE_EFFECTS).
     *
     * @return Maximum delay for a single CompositeEffect[] entry.
     */
    int getCompositionDelayMax();

    /**
     * Retrieve composition size limit.
     *
     * Support is reflected in getCapabilities (CAP_COMPOSE_EFFECTS).
     *
     * @return Maximum number of entries in CompositeEffect[].
     * @param maxDelayMs Maximum delay for a single CompositeEffect[] entry.
     */
    int getCompositionSizeMax();

    /**
     * List of supported effect primitive.
     *
     * Return the effect primitives which are supported by the compose API.
     * Implementations are expected to support all required primitives of the
     * interface version that they implement (see primitive definitions).
     */
    CompositePrimitive[] getSupportedPrimitives();

    /**
     * Retrieve effect primitive's duration in milliseconds.
     *
     * Support is reflected in getCapabilities (CAP_COMPOSE_EFFECTS).
     *
     * @return Best effort estimation of effect primitive's duration.
     * @param primitive Effect primitive being queried.
     */
    int getPrimitiveDuration(CompositePrimitive primitive);

    /**
     * Fire off a string of effect primitives, combined to perform richer effects.
     *
     * Support is reflected in getCapabilities (CAP_COMPOSE_EFFECTS).
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off. IVibratorCallback.onComplete() support is required for this API.
     *
     * @param composite Array of composition parameters.
     */
    void compose(in CompositeEffect[] composite, in IVibratorCallback callback);

    /**
     * List of supported always-on effects.
     *
     * Return the effects which are supported by the alwaysOnEnable (an effect
     * is expected to be supported at every strength level.
     */
    Effect[] getSupportedAlwaysOnEffects();

    /**
     * Enable an always-on haptic source, assigning a specific effect. An
     * always-on haptic source is a source that can be triggered externally
     * once enabled and assigned an effect to play. This may not be supported
     * and this support is reflected in getCapabilities (CAP_ALWAYS_ON_CONTROL).
     *
     * The always-on source ID is conveyed directly to clients through
     * device/board configuration files ensuring that no ID is assigned to
     * multiple clients. No client should use this API unless explicitly
     * assigned an always-on source ID. Clients must develop their own way to
     * get IDs from vendor in a stable way. For instance, a client may expose
     * a stable API (via HAL, sysprops, or xml overlays) to allow vendor to
     * associate a hardware ID with a specific usecase. When that usecase is
     * triggered, a client would use that hardware ID here.
     *
     * @param id The device-specific always-on source ID to enable.
     * @param effect The type of haptic event to trigger.
     * @param strength The intensity of haptic event to trigger.
     */
    void alwaysOnEnable(in int id, in Effect effect, in EffectStrength strength);

    /**
     * Disable an always-on haptic source. This may not be supported and this
     * support is reflected in getCapabilities (CAP_ALWAYS_ON_CONTROL).
     *
     * The always-on source ID is conveyed directly to clients through
     * device/board configuration files ensuring that no ID is assigned to
     * multiple clients. No client should use this API unless explicitly
     * assigned an always-on source ID. Clients must develop their own way to
     * get IDs from vendor in a stable way.
     *
     * @param id The device-specific always-on source ID to disable.
     */
    void alwaysOnDisable(in int id);

    /**
     * Retrieve the measured resonant frequency of the actuator.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_GET_RESONANT_FREQUENCY)
     *
     * @return Measured resonant frequency in Hz. Non-zero value if supported,
     *         or value should be ignored if not supported.
     */
    float getResonantFrequency();

    /**
     * Retrieve the measured Q factor.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_GET_Q_FACTOR)
     *
     * @return Measured Q factor. Non-zero value if supported, or value should be
     *         ignored if not supported.
     */
    float getQFactor();

    /**
     * Retrieve the frequency resolution used in getBandwidthAmplitudeMap() in units of hertz
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_FREQUENCY_CONTROL).
     *
     * @return The frequency resolution of the bandwidth amplitude map.
     *         Non-zero value if supported, or value should be ignored if not supported.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented even if CAP_FREQUENCY_CONTROL capability is reported.
     */
    float getFrequencyResolution();

    /**
     * Retrieve the minimum allowed frequency in units of hertz
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_FREQUENCY_CONTROL).
     *
     * @return The minimum frequency allowed. Non-zero value if supported,
     *         or value should be ignored if not supported.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented even if CAP_FREQUENCY_CONTROL capability is reported.
     */
    float getFrequencyMinimum();

    /**
     * Retrieve the output acceleration amplitude values per frequency supported
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_FREQUENCY_CONTROL).
     *
     * The mapping is represented as a list of amplitude values in the inclusive range [0.0, 1.0].
     * The first value represents the amplitude at the frequency returned by getFrequencyMinimum().
     * Each subsequent element is the amplitude at the next supported frequency, in increments
     * of getFrequencyResolution(). The value returned by getResonantFrequency() must be
     * represented in the returned list.
     *
     * The amplitude values represent the maximum output acceleration amplitude supported for each
     * given frequency. Equal amplitude values for different frequencies represent equal output
     * accelerations.
     *
     * @return The maximum output acceleration amplitude for each supported frequency,
     *         starting at getMinimumFrequency()
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented even if CAP_FREQUENCY_CONTROL capability is reported.
     */
    float[] getBandwidthAmplitudeMap();

    /**
     * Retrieve the maximum duration allowed for any primitive PWLE in units of milliseconds.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS).
     *
     * @return The maximum duration allowed for a single PrimitivePwle.
     *         Non-zero value if supported, or value should be ignored if not supported.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented. Use `IVibrator.getPwleV2PrimitiveDurationMaxMillis` instead.
     */
    int getPwlePrimitiveDurationMax();

    /**
     * Retrieve the maximum count for allowed PWLEs in one composition.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS).
     *
     * @return The maximum count allowed. Non-zero value if supported,
     *         or value should be ignored if not supported.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented. Use `IVibrator.getPwleV2CompositionSizeMax` instead.
     */
    int getPwleCompositionSizeMax();

    /**
     * List of supported braking mechanism.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS).
     * Implementations are optional but encouraged if available.
     *
     * @return The braking mechanisms which are supported by the composePwle API.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented.
     */
    Braking[] getSupportedBraking();

    /**
     * Fire off a string of PWLEs.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS).
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off. IVibratorCallback.onComplete() support is required for this API.
     *
     * @param composite Array of PWLEs.
     * @deprecated This method is deprecated from AIDL v3 and is no longer required to be
     * implemented. Use `IVibrator.composePwleV2` instead.
     */
    void composePwle(in PrimitivePwle[] composite, in IVibratorCallback callback);

    /**
     * Fire off a vendor-defined haptic event.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_PERFORM_VENDOR_EFFECTS).
     *
     * The duration of the effect is unknown and can be undefined for looping effects.
     * IVibratorCallback.onComplete() support is required for this API.
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off.
     *
     * @param effect The vendor data representing the effect to be performed.
     * @param callback A callback used to inform Frameworks of state change.
     * @throws :
     *         - EX_UNSUPPORTED_OPERATION if unsupported, as reflected by getCapabilities.
     *         - EX_ILLEGAL_ARGUMENT for bad framework parameters, e.g. scale or effect strength.
     *         - EX_SERVICE_SPECIFIC for bad vendor data, vibration is not triggered.
     */
    void performVendorEffect(in VendorEffect vendorEffect, in IVibratorCallback callback);

    /**
     * Retrieves a mapping of vibration frequency (Hz) to the maximum achievable output
     * acceleration (Gs) the device can reach at that frequency.
     *
     * The map, represented as a list of `FrequencyAccelerationMapEntry` (frequency, output
     * acceleration) pairs, defines the device's frequency response. The platform uses the minimum
     * and maximum frequency values to determine the supported input range for
     * `IVibrator.composePwleV2`. Output acceleration values are used to identify a frequency range
     * suitable to safely play perceivable vibrations with a simple API. The map is also exposed for
     * developers using an advanced API.
     *
     * The platform does not impose specific requirements on map resolution which can vary
     * depending on the shape of device output curve. The values will be linearly interpolated
     * during lookups. The platform will provide a simple API, defined by the first frequency range
     * where output acceleration consistently exceeds a minimum threshold of 10 db SL.
     *
     *
     * This may not be supported and this support is reflected in getCapabilities
     * (CAP_FREQUENCY_CONTROL). If this is supported, it's expected to be non-empty and
     * describe a valid non-empty frequency range where the simple API can be defined
     * (i.e. a range where the output acceleration is always above 10 db SL).
     *
     * @return A list of map entries representing the frequency to max acceleration
     *         mapping.
     * @throws EX_UNSUPPORTED_OPERATION if unsupported, as reflected by getCapabilities.
     */
    List<FrequencyAccelerationMapEntry> getFrequencyToOutputAccelerationMap();

    /**
     * Retrieve the maximum duration allowed for any primitive PWLE in units of
     * milliseconds.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS_V2).
     *
     * @return The maximum duration allowed for a single PrimitivePwle. Non-zero value if supported.
     * @throws EX_UNSUPPORTED_OPERATION if unsupported, as reflected by getCapabilities.
     */
    int getPwleV2PrimitiveDurationMaxMillis();

    /**
     * Retrieve the maximum number of PWLE primitives input supported by IVibrator.composePwleV2.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS_V2). Devices supporting
     * PWLE effects must support effects with at least 16 PwleV2Primitive.
     *
     * @return The maximum count allowed. Non-zero value if supported.
     * @throws EX_UNSUPPORTED_OPERATION if unsupported, as reflected by getCapabilities.
     */
    int getPwleV2CompositionSizeMax();

    /**
     * Retrieves the minimum duration (in milliseconds) of any segment within a
     * PWLE effect. Devices supporting PWLE effects must support a minimum ramp
     * time of 20 milliseconds.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS_V2).
     *
     * @return The minimum duration allowed for a single PrimitivePwle. Non-zero value if supported.
     * @throws EX_UNSUPPORTED_OPERATION if unsupported, as reflected by getCapabilities.
     */
    int getPwleV2PrimitiveDurationMinMillis();

    /**
     * Play composed sequence of PWLEs with optional callback upon completion.
     *
     * A PWLE (Piecewise-Linear Envelope) effect defines a vibration waveform using amplitude and
     * frequency points. The envelope linearly interpolates both amplitude and frequency between
     * consecutive points, creating smooth transitions in the vibration pattern.
     *
     * This may not be supported and this support is reflected in
     * getCapabilities (CAP_COMPOSE_PWLE_EFFECTS_V2).
     *
     * Note: Devices reporting CAP_COMPOSE_PWLE_EFFECTS_V2 support MUST also have the
     * CAP_FREQUENCY_CONTROL capability and provide a valid frequency to output acceleration map.
     *
     * Doing this operation while the vibrator is already on is undefined behavior. Clients should
     * explicitly call off. IVibratorCallback.onComplete() support is required for this API.
     *
     * @param composite A CompositePwleV2 representing a composite vibration effect, composed of an
     *                  array of primitives that define the PWLE (Piecewise-Linear Envelope).
     */
    void composePwleV2(in CompositePwleV2 composite, in IVibratorCallback callback);
}
