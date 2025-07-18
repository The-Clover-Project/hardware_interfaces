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

#include <Constants.h>
#include <MockLocation.h>
#include <Utils.h>
#include <aidl/android/hardware/gnss/BnGnss.h>
#include <utils/SystemClock.h>

namespace android {
namespace hardware {
namespace gnss {
namespace common {

using aidl::android::hardware::gnss::ElapsedRealtime;
using aidl::android::hardware::gnss::GnssClock;
using aidl::android::hardware::gnss::GnssConstellationType;
using aidl::android::hardware::gnss::GnssData;
using aidl::android::hardware::gnss::GnssLocation;
using aidl::android::hardware::gnss::GnssMeasurement;
using aidl::android::hardware::gnss::IGnss;
using aidl::android::hardware::gnss::IGnssDebug;
using aidl::android::hardware::gnss::IGnssMeasurementCallback;
using aidl::android::hardware::gnss::SatellitePvt;
using GnssSvInfo = aidl::android::hardware::gnss::IGnssCallback::GnssSvInfo;
using GnssSvFlags = aidl::android::hardware::gnss::IGnssCallback::GnssSvFlags;

using GnssSvFlagsV1_0 = V1_0::IGnssCallback::GnssSvFlags;
using GnssAgc = aidl::android::hardware::gnss::GnssData::GnssAgc;
using GnssMeasurementFlagsV1_0 = V1_0::IGnssMeasurementCallback::GnssMeasurementFlags;
using GnssMeasurementFlagsV2_1 = V2_1::IGnssMeasurementCallback::GnssMeasurementFlags;
using GnssMeasurementStateV2_0 = V2_0::IGnssMeasurementCallback::GnssMeasurementState;
using ElapsedRealtimeFlags = V2_0::ElapsedRealtimeFlags;
using GnssConstellationTypeV2_0 = V2_0::GnssConstellationType;
using IGnssMeasurementCallbackV2_0 = V2_0::IGnssMeasurementCallback;
using GnssSignalType = V2_1::GnssSignalType;

using GnssDataV2_0 = V2_0::IGnssMeasurementCallback::GnssData;
using GnssDataV2_1 = V2_1::IGnssMeasurementCallback::GnssData;
using GnssSvInfoV1_0 = V1_0::IGnssCallback::GnssSvInfo;
using GnssSvInfoV2_0 = V2_0::IGnssCallback::GnssSvInfo;
using GnssSvInfoV2_1 = V2_1::IGnssCallback::GnssSvInfo;
using GnssAntennaInfo = ::android::hardware::gnss::V2_1::IGnssAntennaInfoCallback::GnssAntennaInfo;
using Row = V2_1::IGnssAntennaInfoCallback::Row;
using Coord = V2_1::IGnssAntennaInfoCallback::Coord;

GnssDataV2_1 Utils::getMockMeasurementV2_1() {
    GnssDataV2_0 gnssDataV2_0 = Utils::getMockMeasurementV2_0();
    V2_1::IGnssMeasurementCallback::GnssMeasurement gnssMeasurementV2_1 = {
            .v2_0 = gnssDataV2_0.measurements[0],
            .flags = (uint32_t)(GnssMeasurementFlagsV2_1::HAS_CARRIER_FREQUENCY |
                                GnssMeasurementFlagsV2_1::HAS_CARRIER_PHASE |
                                GnssMeasurementFlagsV2_1::HAS_FULL_ISB |
                                GnssMeasurementFlagsV2_1::HAS_FULL_ISB_UNCERTAINTY |
                                GnssMeasurementFlagsV2_1::HAS_SATELLITE_ISB |
                                GnssMeasurementFlagsV2_1::HAS_SATELLITE_ISB_UNCERTAINTY),
            .fullInterSignalBiasNs = 30.0,
            .fullInterSignalBiasUncertaintyNs = 250.0,
            .satelliteInterSignalBiasNs = 20.0,
            .satelliteInterSignalBiasUncertaintyNs = 150.0,
            .basebandCN0DbHz = 25.0,
    };
    GnssSignalType referenceSignalTypeForIsb = {
            .constellation = GnssConstellationTypeV2_0::GPS,
            .carrierFrequencyHz = 1.59975e+09,
            .codeType = "C",
    };
    V2_1::IGnssMeasurementCallback::GnssClock gnssClockV2_1 = {
            .v1_0 = gnssDataV2_0.clock,
            .referenceSignalTypeForIsb = referenceSignalTypeForIsb,
    };
    hidl_vec<V2_1::IGnssMeasurementCallback::GnssMeasurement> measurements(1);
    measurements[0] = gnssMeasurementV2_1;
    GnssDataV2_1 gnssDataV2_1 = {
            .measurements = measurements,
            .clock = gnssClockV2_1,
            .elapsedRealtime = gnssDataV2_0.elapsedRealtime,
    };
    return gnssDataV2_1;
}

GnssDataV2_0 Utils::getMockMeasurementV2_0() {
    V1_0::IGnssMeasurementCallback::GnssMeasurement measurement_1_0 = {
            .flags = (uint32_t)GnssMeasurementFlagsV1_0::HAS_CARRIER_FREQUENCY,
            .svid = (int16_t)6,
            .constellation = V1_0::GnssConstellationType::UNKNOWN,
            .timeOffsetNs = 0.0,
            .receivedSvTimeInNs = 8195997131077,
            .receivedSvTimeUncertaintyInNs = 15,
            .cN0DbHz = 30.0,
            .pseudorangeRateMps = -484.13739013671875,
            .pseudorangeRateUncertaintyMps = 0.1037999987602233,
            .accumulatedDeltaRangeState = (uint32_t)V1_0::IGnssMeasurementCallback::
                    GnssAccumulatedDeltaRangeState::ADR_STATE_UNKNOWN,
            .accumulatedDeltaRangeM = 0.0,
            .accumulatedDeltaRangeUncertaintyM = 0.0,
            .carrierFrequencyHz = 1.59975e+09,
            .multipathIndicator =
                    V1_0::IGnssMeasurementCallback::GnssMultipathIndicator::INDICATOR_UNKNOWN};
    V1_1::IGnssMeasurementCallback::GnssMeasurement measurement_1_1 = {.v1_0 = measurement_1_0};
    V2_0::IGnssMeasurementCallback::GnssMeasurement measurement_2_0 = {
            .v1_1 = measurement_1_1,
            .codeType = "C",
            .state = GnssMeasurementStateV2_0::STATE_CODE_LOCK |
                     GnssMeasurementStateV2_0::STATE_BIT_SYNC |
                     GnssMeasurementStateV2_0::STATE_SUBFRAME_SYNC |
                     GnssMeasurementStateV2_0::STATE_TOW_DECODED |
                     GnssMeasurementStateV2_0::STATE_GLO_STRING_SYNC |
                     GnssMeasurementStateV2_0::STATE_GLO_TOD_DECODED,
            .constellation = GnssConstellationTypeV2_0::GLONASS,
    };

    hidl_vec<IGnssMeasurementCallbackV2_0::GnssMeasurement> measurements(1);
    measurements[0] = measurement_2_0;
    V1_0::IGnssMeasurementCallback::GnssClock clock = {.timeNs = 2713545000000,
                                                       .fullBiasNs = -1226701900521857520,
                                                       .biasNs = 0.59689998626708984,
                                                       .biasUncertaintyNs = 47514.989972114563,
                                                       .driftNsps = -51.757811607455452,
                                                       .driftUncertaintyNsps = 310.64968328491528,
                                                       .hwClockDiscontinuityCount = 1};

    V2_0::ElapsedRealtime timestamp = {
            .flags = ElapsedRealtimeFlags::HAS_TIMESTAMP_NS |
                     ElapsedRealtimeFlags::HAS_TIME_UNCERTAINTY_NS,
            .timestampNs = static_cast<uint64_t>(::android::elapsedRealtimeNano()),
            // This is an hardcoded value indicating a 1ms of uncertainty between the two clocks.
            // In an actual implementation provide an estimate of the synchronization uncertainty
            // or don't set the field.
            .timeUncertaintyNs = 1000000};

    GnssDataV2_0 gnssData = {
            .measurements = measurements, .clock = clock, .elapsedRealtime = timestamp};
    return gnssData;
}

namespace {
GnssMeasurement getMockGnssMeasurement(int svid, GnssConstellationType constellationType,
                                       float cN0DbHz, float basebandCN0DbHz,
                                       double carrierFrequencyHz, bool enableCorrVecOutputs) {
    aidl::android::hardware::gnss::GnssSignalType signalType = {
            .constellation = constellationType,
            .carrierFrequencyHz = carrierFrequencyHz,
            .codeType = aidl::android::hardware::gnss::GnssSignalType::CODE_TYPE_C,
    };
    GnssMeasurement measurement = {
            .flags = GnssMeasurement::HAS_AUTOMATIC_GAIN_CONTROL |
                     GnssMeasurement::HAS_CARRIER_FREQUENCY | GnssMeasurement::HAS_CARRIER_PHASE |
                     GnssMeasurement::HAS_CARRIER_PHASE_UNCERTAINTY |
                     GnssMeasurement::HAS_FULL_ISB | GnssMeasurement::HAS_FULL_ISB_UNCERTAINTY |
                     GnssMeasurement::HAS_SATELLITE_ISB |
                     GnssMeasurement::HAS_SATELLITE_ISB_UNCERTAINTY |
                     GnssMeasurement::HAS_SATELLITE_PVT,
            .svid = svid,
            .signalType = signalType,
            .state = GnssMeasurement::STATE_CODE_LOCK | GnssMeasurement::STATE_BIT_SYNC |
                     GnssMeasurement::STATE_SUBFRAME_SYNC | GnssMeasurement::STATE_TOW_DECODED |
                     GnssMeasurement::STATE_GLO_STRING_SYNC |
                     GnssMeasurement::STATE_GLO_TOD_DECODED,
            .receivedSvTimeInNs = 8195997131077,
            .receivedSvTimeUncertaintyInNs = 15,
            .antennaCN0DbHz = cN0DbHz,
            .basebandCN0DbHz = basebandCN0DbHz,
            .pseudorangeRateMps = -484.13739013671875,
            .pseudorangeRateUncertaintyMps = 0.1037999987602233,
            .accumulatedDeltaRangeState = GnssMeasurement::ADR_STATE_VALID,
            .accumulatedDeltaRangeM = 1.52,
            .accumulatedDeltaRangeUncertaintyM = 2.43,
            .multipathIndicator = aidl::android::hardware::gnss::GnssMultipathIndicator::UNKNOWN,
            .agcLevelDb = 2.3,
            .fullInterSignalBiasNs = 21.5,
            .fullInterSignalBiasUncertaintyNs = 792.0,
            .satelliteInterSignalBiasNs = 233.9,
            .satelliteInterSignalBiasUncertaintyNs = 921.2,
            .satellitePvt =
                    {
                            .flags = SatellitePvt::HAS_POSITION_VELOCITY_CLOCK_INFO |
                                     SatellitePvt::HAS_IONO | SatellitePvt::HAS_TROPO,
                            .satPosEcef = {.posXMeters = 10442993.1153328,
                                           .posYMeters = -19926932.8051666,
                                           .posZMeters = -12034295.0216203,
                                           .ureMeters = 1000.2345678},
                            .satVelEcef = {.velXMps = -478.667183715732,
                                           .velYMps = 1580.68371984114,
                                           .velZMps = -3030.52994449997,
                                           .ureRateMps = 10.2345678},
                            .satClockInfo = {.satHardwareCodeBiasMeters = 1.396983861923e-09,
                                             .satTimeCorrectionMeters = -7113.08964331,
                                             .satClkDriftMps = 0},
                            .ionoDelayMeters = 3.069949602639317e-08,
                            .tropoDelayMeters = 3.882265204404031,
                            .timeOfClockSeconds = 12345,
                            .issueOfDataClock = 143,
                            .timeOfEphemerisSeconds = 9876,
                            .issueOfDataEphemeris = 48,
                            .ephemerisSource =
                                    SatellitePvt::SatelliteEphemerisSource::SERVER_LONG_TERM,
                    },
            .correlationVectors = {}};

    if (enableCorrVecOutputs) {
        aidl::android::hardware::gnss::CorrelationVector correlationVector1 = {
                .frequencyOffsetMps = 10,
                .samplingWidthM = 30,
                .samplingStartM = 0,
                .magnitude = {0, 5000, 10000, 5000, 0, 0, 3000, 0}};
        aidl::android::hardware::gnss::CorrelationVector correlationVector2 = {
                .frequencyOffsetMps = 20,
                .samplingWidthM = 30,
                .samplingStartM = -10,
                .magnitude = {0, 3000, 5000, 3000, 0, 0, 1000, 0}};
        measurement.correlationVectors = {correlationVector1, correlationVector2};
        measurement.flags |= GnssMeasurement::HAS_CORRELATION_VECTOR;
    }
    return measurement;
}
}  // namespace

GnssData Utils::getMockMeasurement(const bool enableCorrVecOutputs, const bool enableFullTracking) {
    std::vector<GnssMeasurement> measurements = {
            // GPS
            getMockGnssMeasurement(3, GnssConstellationType::GPS, 32.5, 27.5, kGpsL1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(5, GnssConstellationType::GPS, 27.0, 22.0, kGpsL1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(17, GnssConstellationType::GPS, 30.5, 25.5, kGpsL5FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(26, GnssConstellationType::GPS, 24.1, 19.1, kGpsL5FreqHz,
                                   enableCorrVecOutputs),
            // GAL
            getMockGnssMeasurement(2, GnssConstellationType::GALILEO, 33.5, 27.5, kGalE1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(4, GnssConstellationType::GALILEO, 28.0, 22.0, kGalE1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(10, GnssConstellationType::GALILEO, 35.5, 25.5, kGalE1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(29, GnssConstellationType::GALILEO, 34.1, 19.1, kGalE1FreqHz,
                                   enableCorrVecOutputs),
            // GLO
            getMockGnssMeasurement(5, GnssConstellationType::GLONASS, 20.5, 15.5, kGloG1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(17, GnssConstellationType::GLONASS, 21.5, 16.5, kGloG1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(18, GnssConstellationType::GLONASS, 28.3, 25.3, kGloG1FreqHz,
                                   enableCorrVecOutputs),
            getMockGnssMeasurement(10, GnssConstellationType::GLONASS, 25.0, 20.0, kGloG1FreqHz,
                                   enableCorrVecOutputs),
            // IRNSS
            getMockGnssMeasurement(3, GnssConstellationType::IRNSS, 22.0, 19.7, kIrnssL5FreqHz,
                                   enableCorrVecOutputs),
    };

    GnssClock clock = {
            .gnssClockFlags = GnssClock::HAS_FULL_BIAS | GnssClock::HAS_BIAS |
                              GnssClock::HAS_BIAS_UNCERTAINTY | GnssClock::HAS_DRIFT |
                              GnssClock::HAS_DRIFT_UNCERTAINTY,
            .timeNs = 2713545000000,
            .fullBiasNs = -1226701900521857520,
            .biasNs = 0.59689998626708984,
            .biasUncertaintyNs = 47514.989972114563,
            .driftNsps = -51.757811607455452,
            .driftUncertaintyNsps = 310.64968328491528,
            .hwClockDiscontinuityCount = 1,
            .referenceSignalTypeForIsb = {
                    .constellation = GnssConstellationType::GLONASS,
                    .carrierFrequencyHz = 1.59975e+09,
                    .codeType = aidl::android::hardware::gnss::GnssSignalType::CODE_TYPE_C,
            }};

    ElapsedRealtime timestamp = {
            .flags = ElapsedRealtime::HAS_TIMESTAMP_NS | ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS,
            .timestampNs = ::android::elapsedRealtimeNano(),
            // This is an hardcoded value indicating a 1ms of uncertainty between the two clocks.
            // In an actual implementation provide an estimate of the synchronization uncertainty
            // or don't set the field.
            .timeUncertaintyNs = 1020400};

    GnssAgc gnssAgc1 = {
            .agcLevelDb = 3.5,
            .constellation = GnssConstellationType::GLONASS,
            .carrierFrequencyHz = (int64_t)kGloG1FreqHz,
    };

    GnssAgc gnssAgc2 = {
            .agcLevelDb = -5.1,
            .constellation = GnssConstellationType::GPS,
            .carrierFrequencyHz = (int64_t)kGpsL1FreqHz,
    };

    GnssData gnssData = {.measurements = measurements,
                         .clock = clock,
                         .elapsedRealtime = timestamp,
                         .gnssAgcs = std::vector({gnssAgc1, gnssAgc2}),
                         .isFullTracking = enableFullTracking};
    return gnssData;
}

GnssLocation Utils::getMockLocation() {
    ElapsedRealtime elapsedRealtime = {
            .flags = ElapsedRealtime::HAS_TIMESTAMP_NS | ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS,
            .timestampNs = ::android::elapsedRealtimeNano(),
            // This is an hardcoded value indicating a 1ms of uncertainty between the two clocks.
            // In an actual implementation provide an estimate of the synchronization uncertainty
            // or don't set the field.
            .timeUncertaintyNs = 1020400};
    GnssLocation location = {.gnssLocationFlags = 0xFF,
                             .latitudeDegrees = gMockLatitudeDegrees,
                             .longitudeDegrees = gMockLongitudeDegrees,
                             .altitudeMeters = gMockAltitudeMeters,
                             .speedMetersPerSec = gMockSpeedMetersPerSec,
                             .bearingDegrees = gMockBearingDegrees,
                             .horizontalAccuracyMeters = kMockHorizontalAccuracyMeters,
                             .verticalAccuracyMeters = kMockVerticalAccuracyMeters,
                             .speedAccuracyMetersPerSecond = kMockSpeedAccuracyMetersPerSecond,
                             .bearingAccuracyDegrees = kMockBearingAccuracyDegrees,
                             .timestampMillis = static_cast<int64_t>(
                                     kMockTimestamp + ::android::elapsedRealtimeNano() / 1e6),
                             .elapsedRealtime = elapsedRealtime};
    return location;
}

V2_0::GnssLocation Utils::getMockLocationV2_0() {
    const V2_0::ElapsedRealtime timestamp = {
            .flags = V2_0::ElapsedRealtimeFlags::HAS_TIMESTAMP_NS |
                     V2_0::ElapsedRealtimeFlags::HAS_TIME_UNCERTAINTY_NS,
            .timestampNs = static_cast<uint64_t>(::android::elapsedRealtimeNano()),
            // This is an hardcoded value indicating a 1ms of uncertainty between the two clocks.
            // In an actual implementation provide an estimate of the synchronization uncertainty
            // or don't set the field.
            .timeUncertaintyNs = 1000000};

    V2_0::GnssLocation location = {.v1_0 = Utils::getMockLocationV1_0(),
                                   .elapsedRealtime = timestamp};
    return location;
}

V1_0::GnssLocation Utils::getMockLocationV1_0() {
    V1_0::GnssLocation location = {
            .gnssLocationFlags = 0xFF,
            .latitudeDegrees = gMockLatitudeDegrees,
            .longitudeDegrees = gMockLongitudeDegrees,
            .altitudeMeters = gMockAltitudeMeters,
            .speedMetersPerSec = gMockSpeedMetersPerSec,
            .bearingDegrees = gMockBearingDegrees,
            .horizontalAccuracyMeters = kMockHorizontalAccuracyMeters,
            .verticalAccuracyMeters = kMockVerticalAccuracyMeters,
            .speedAccuracyMetersPerSecond = kMockSpeedAccuracyMetersPerSecond,
            .bearingAccuracyDegrees = kMockBearingAccuracyDegrees,
            .timestamp =
                    static_cast<int64_t>(kMockTimestamp + ::android::elapsedRealtimeNano() / 1e6)};
    return location;
}

namespace {
GnssSvInfo getMockSvInfo(int svid, GnssConstellationType type, float cN0DbHz, float basebandCN0DbHz,
                         float elevationDegrees, float azimuthDegrees, long carrierFrequencyHz) {
    GnssSvInfo svInfo = {
            .svid = svid,
            .constellation = type,
            .cN0Dbhz = cN0DbHz,
            .basebandCN0DbHz = basebandCN0DbHz,
            .elevationDegrees = elevationDegrees,
            .azimuthDegrees = azimuthDegrees,
            .carrierFrequencyHz = carrierFrequencyHz,
            .svFlag = (int)GnssSvFlags::USED_IN_FIX | (int)GnssSvFlags::HAS_EPHEMERIS_DATA |
                      (int)GnssSvFlags::HAS_ALMANAC_DATA | (int)GnssSvFlags::HAS_CARRIER_FREQUENCY};
    return svInfo;
}
}  // anonymous namespace

std::vector<GnssSvInfo> Utils::getMockSvInfoList() {
    std::vector<GnssSvInfo> gnssSvInfoList = {
            // svid in [1, 32] for GPS
            getMockSvInfo(3, GnssConstellationType::GPS, 32.5, 27.5, 59.1, 166.5, kGpsL1FreqHz),
            getMockSvInfo(5, GnssConstellationType::GPS, 27.0, 22.0, 29.0, 56.5, kGpsL1FreqHz),
            getMockSvInfo(17, GnssConstellationType::GPS, 30.5, 25.5, 71.0, 77.0, kGpsL5FreqHz),
            getMockSvInfo(26, GnssConstellationType::GPS, 24.1, 19.1, 28.0, 253.0, kGpsL5FreqHz),
            // svid in [1, 36] for GAL
            getMockSvInfo(2, GnssConstellationType::GALILEO, 33.5, 27.5, 59.1, 166.5, kGalE1FreqHz),
            getMockSvInfo(4, GnssConstellationType::GALILEO, 28.0, 22.0, 29.0, 56.5, kGalE1FreqHz),
            getMockSvInfo(10, GnssConstellationType::GALILEO, 35.5, 25.5, 71.0, 77.0, kGalE1FreqHz),
            getMockSvInfo(29, GnssConstellationType::GALILEO, 34.1, 19.1, 28.0, 253.0,
                          kGalE1FreqHz),
            // "1 <= svid <= 25 || 93 <= svid <= 106" for GLO
            getMockSvInfo(5, GnssConstellationType::GLONASS, 20.5, 15.5, 11.5, 116.0, kGloG1FreqHz),
            getMockSvInfo(17, GnssConstellationType::GLONASS, 21.5, 16.5, 28.5, 186.0,
                          kGloG1FreqHz),
            getMockSvInfo(18, GnssConstellationType::GLONASS, 28.3, 25.3, 38.8, 69.0, kGloG1FreqHz),
            getMockSvInfo(10, GnssConstellationType::GLONASS, 25.0, 20.0, 66.0, 247.0,
                          kGloG1FreqHz),
            // "1 <= X <= 14" for IRNSS
            getMockSvInfo(3, GnssConstellationType::IRNSS, 22.0, 19.7, 35.0, 112.0, kIrnssL5FreqHz),
    };
    return gnssSvInfoList;
}

hidl_vec<GnssSvInfoV2_1> Utils::getMockSvInfoListV2_1() {
    GnssSvInfoV1_0 gnssSvInfoV1_0 = Utils::getMockSvInfoV1_0(3, V1_0::GnssConstellationType::GPS,
                                                             32.5, 59.1, 166.5, kGpsL1FreqHz);
    GnssSvInfoV2_0 gnssSvInfoV2_0 =
            Utils::getMockSvInfoV2_0(gnssSvInfoV1_0, V2_0::GnssConstellationType::GPS);
    hidl_vec<GnssSvInfoV2_1> gnssSvInfoList = {
            Utils::getMockSvInfoV2_1(gnssSvInfoV2_0, 27.5),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(5, V1_0::GnssConstellationType::GPS, 27.0,
                                                        29.0, 56.5, kGpsL1FreqHz),
                                      V2_0::GnssConstellationType::GPS),
                    22.0),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(17, V1_0::GnssConstellationType::GPS, 30.5,
                                                        71.0, 77.0, kGpsL5FreqHz),
                                      V2_0::GnssConstellationType::GPS),
                    25.5),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(26, V1_0::GnssConstellationType::GPS, 24.1,
                                                        28.0, 253.0, kGpsL5FreqHz),
                                      V2_0::GnssConstellationType::GPS),
                    19.1),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(5, V1_0::GnssConstellationType::GLONASS,
                                                        20.5, 11.5, 116.0, kGloG1FreqHz),
                                      V2_0::GnssConstellationType::GLONASS),
                    15.5),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(17, V1_0::GnssConstellationType::GLONASS,
                                                        21.5, 28.5, 186.0, kGloG1FreqHz),
                                      V2_0::GnssConstellationType::GLONASS),
                    16.5),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(18, V1_0::GnssConstellationType::GLONASS,
                                                        28.3, 38.8, 69.0, kGloG1FreqHz),
                                      V2_0::GnssConstellationType::GLONASS),
                    25.3),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(10, V1_0::GnssConstellationType::GLONASS,
                                                        25.0, 66.0, 247.0, kGloG1FreqHz),
                                      V2_0::GnssConstellationType::GLONASS),
                    20.0),
            getMockSvInfoV2_1(
                    getMockSvInfoV2_0(getMockSvInfoV1_0(3, V1_0::GnssConstellationType::UNKNOWN,
                                                        22.0, 35.0, 112.0, kIrnssL5FreqHz),
                                      V2_0::GnssConstellationType::IRNSS),
                    19.7),
    };
    return gnssSvInfoList;
}

GnssSvInfoV2_1 Utils::getMockSvInfoV2_1(GnssSvInfoV2_0 gnssSvInfoV2_0, float basebandCN0DbHz) {
    GnssSvInfoV2_1 gnssSvInfoV2_1 = {
            .v2_0 = gnssSvInfoV2_0,
            .basebandCN0DbHz = basebandCN0DbHz,
    };
    return gnssSvInfoV2_1;
}

GnssSvInfoV2_0 Utils::getMockSvInfoV2_0(GnssSvInfoV1_0 gnssSvInfoV1_0,
                                        V2_0::GnssConstellationType type) {
    GnssSvInfoV2_0 gnssSvInfoV2_0 = {
            .v1_0 = gnssSvInfoV1_0,
            .constellation = type,
    };
    return gnssSvInfoV2_0;
}

GnssSvInfoV1_0 Utils::getMockSvInfoV1_0(int16_t svid, V1_0::GnssConstellationType type,
                                        float cN0DbHz, float elevationDegrees, float azimuthDegrees,
                                        float carrierFrequencyHz) {
    GnssSvInfoV1_0 svInfo = {
            .svid = svid,
            .constellation = type,
            .cN0Dbhz = cN0DbHz,
            .elevationDegrees = elevationDegrees,
            .azimuthDegrees = azimuthDegrees,
            .carrierFrequencyHz = carrierFrequencyHz,
            .svFlag = GnssSvFlagsV1_0::USED_IN_FIX | GnssSvFlagsV1_0::HAS_EPHEMERIS_DATA |
                      GnssSvFlagsV1_0::HAS_ALMANAC_DATA | GnssSvFlagsV1_0::HAS_CARRIER_FREQUENCY};
    return svInfo;
}

hidl_vec<GnssAntennaInfo> Utils::getMockAntennaInfos() {
    GnssAntennaInfo mockAntennaInfo_1 = {
            .carrierFrequencyMHz = kGpsL1FreqHz * 1e-6,
            .phaseCenterOffsetCoordinateMillimeters = Coord{.x = 1,
                                                            .xUncertainty = 0.1,
                                                            .y = 2,
                                                            .yUncertainty = 0.1,
                                                            .z = 3,
                                                            .zUncertainty = 0.1},
            .phaseCenterVariationCorrectionMillimeters =
                    {
                            Row{hidl_vec<double>{1, -1, 5, -2, 3, -1}},
                            Row{hidl_vec<double>{-2, 3, 2, 0, 1, 2}},
                            Row{hidl_vec<double>{1, 3, 2, -1, -3, 5}},
                    },
            .phaseCenterVariationCorrectionUncertaintyMillimeters =
                    {
                            Row{hidl_vec<double>{0.1, 0.2, 0.4, 0.1, 0.2, 0.3}},
                            Row{hidl_vec<double>{0.3, 0.2, 0.3, 0.6, 0.1, 0.1}},
                            Row{hidl_vec<double>{0.1, 0.1, 0.4, 0.2, 0.5, 0.3}},
                    },
            .signalGainCorrectionDbi =
                    {
                            Row{hidl_vec<double>{2, -3, 1, -3, 0, -4}},
                            Row{hidl_vec<double>{1, 0, -4, 1, 3, -2}},
                            Row{hidl_vec<double>{3, -2, 0, -2, 3, 0}},
                    },
            .signalGainCorrectionUncertaintyDbi =
                    {
                            Row{hidl_vec<double>{0.3, 0.1, 0.2, 0.6, 0.1, 0.3}},
                            Row{hidl_vec<double>{0.1, 0.1, 0.5, 0.2, 0.3, 0.1}},
                            Row{hidl_vec<double>{0.2, 0.4, 0.2, 0.1, 0.1, 0.2}},
                    },
    };

    GnssAntennaInfo mockAntennaInfo_2 = {
            .carrierFrequencyMHz = kGpsL5FreqHz * 1e-6,
            .phaseCenterOffsetCoordinateMillimeters = Coord{.x = 5,
                                                            .xUncertainty = 0.1,
                                                            .y = 6,
                                                            .yUncertainty = 0.1,
                                                            .z = 7,
                                                            .zUncertainty = 0.1},
    };

    hidl_vec<GnssAntennaInfo> mockAntennaInfos = {
            mockAntennaInfo_1,
            mockAntennaInfo_2,
    };
    return mockAntennaInfos;
}

}  // namespace common
}  // namespace gnss
}  // namespace hardware
}  // namespace android
