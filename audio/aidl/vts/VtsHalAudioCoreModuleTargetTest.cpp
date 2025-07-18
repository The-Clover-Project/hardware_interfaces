/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <forward_list>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#define LOG_TAG "VtsHalAudioCore.Module"
#include <android-base/logging.h>

#include <StreamWorker.h>
#include <Utils.h>
#include <aidl/Gtest.h>
#include <aidl/Vintf.h>
#include <aidl/android/hardware/audio/core/BnStreamCallback.h>
#include <aidl/android/hardware/audio/core/IModule.h>
#include <aidl/android/hardware/audio/core/ITelephony.h>
#include <aidl/android/hardware/audio/core/sounddose/ISoundDose.h>
#include <aidl/android/media/audio/common/AudioIoFlags.h>
#include <aidl/android/media/audio/common/AudioMMapPolicyInfo.h>
#include <aidl/android/media/audio/common/AudioMMapPolicyType.h>
#include <aidl/android/media/audio/common/AudioOutputFlags.h>
#include <android-base/chrono_utils.h>
#include <android/binder_enums.h>
#include <error/expected_utils.h>
#include <fmq/AidlMessageQueue.h>

#include "AudioHalBinderServiceUtil.h"
#include "ModuleConfig.h"
#include "TestUtils.h"

using namespace android;
using aidl::android::hardware::audio::common::AudioOffloadMetadata;
using aidl::android::hardware::audio::common::getChannelCount;
using aidl::android::hardware::audio::common::hasMmapFlag;
using aidl::android::hardware::audio::common::isAnyBitPositionFlagSet;
using aidl::android::hardware::audio::common::isBitPositionFlagSet;
using aidl::android::hardware::audio::common::isTelephonyDeviceType;
using aidl::android::hardware::audio::common::isValidAudioMode;
using aidl::android::hardware::audio::common::PlaybackTrackMetadata;
using aidl::android::hardware::audio::common::RecordTrackMetadata;
using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::hardware::audio::core::AudioPatch;
using aidl::android::hardware::audio::core::AudioRoute;
using aidl::android::hardware::audio::core::IBluetooth;
using aidl::android::hardware::audio::core::IBluetoothA2dp;
using aidl::android::hardware::audio::core::IBluetoothLe;
using aidl::android::hardware::audio::core::IModule;
using aidl::android::hardware::audio::core::IStreamCommon;
using aidl::android::hardware::audio::core::IStreamIn;
using aidl::android::hardware::audio::core::IStreamOut;
using aidl::android::hardware::audio::core::ITelephony;
using aidl::android::hardware::audio::core::MmapBufferDescriptor;
using aidl::android::hardware::audio::core::ModuleDebug;
using aidl::android::hardware::audio::core::StreamDescriptor;
using aidl::android::hardware::audio::core::VendorParameter;
using aidl::android::hardware::audio::core::sounddose::ISoundDose;
using aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using aidl::android::media::audio::common::AudioChannelLayout;
using aidl::android::media::audio::common::AudioConfigBase;
using aidl::android::media::audio::common::AudioContentType;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceAddress;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioDualMonoMode;
using aidl::android::media::audio::common::AudioEncapsulationMode;
using aidl::android::media::audio::common::AudioFormatDescription;
using aidl::android::media::audio::common::AudioFormatType;
using aidl::android::media::audio::common::AudioGainConfig;
using aidl::android::media::audio::common::AudioInputFlags;
using aidl::android::media::audio::common::AudioIoFlags;
using aidl::android::media::audio::common::AudioLatencyMode;
using aidl::android::media::audio::common::AudioMMapPolicy;
using aidl::android::media::audio::common::AudioMMapPolicyInfo;
using aidl::android::media::audio::common::AudioMMapPolicyType;
using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioOutputFlags;
using aidl::android::media::audio::common::AudioPlaybackRate;
using aidl::android::media::audio::common::AudioPort;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::AudioPortDeviceExt;
using aidl::android::media::audio::common::AudioPortExt;
using aidl::android::media::audio::common::AudioPortMixExt;
using aidl::android::media::audio::common::AudioSource;
using aidl::android::media::audio::common::AudioUsage;
using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Float;
using aidl::android::media::audio::common::Int;
using aidl::android::media::audio::common::MicrophoneDynamicInfo;
using aidl::android::media::audio::common::MicrophoneInfo;
using aidl::android::media::audio::common::Void;
using android::hardware::audio::common::StreamLogic;
using android::hardware::audio::common::StreamWorker;
using android::hardware::audio::common::testing::detail::TestExecutionTracer;
using ndk::enum_range;
using ndk::ScopedAStatus;

static constexpr int32_t kAidlVersion1 = 1;
static constexpr int32_t kAidlVersion2 = 2;
static constexpr int32_t kAidlVersion3 = 3;

template <typename T>
std::set<int32_t> extractIds(const std::vector<T>& v) {
    std::set<int32_t> ids;
    std::transform(v.begin(), v.end(), std::inserter(ids, ids.begin()),
                   [](const auto& entity) { return entity.id; });
    return ids;
}

template <typename T>
auto findById(const std::vector<T>& v, int32_t id) {
    return std::find_if(v.begin(), v.end(), [&](const auto& e) { return e.id == id; });
}

template <typename T>
auto findAny(const std::vector<T>& v, const std::set<int32_t>& ids) {
    return std::find_if(v.begin(), v.end(), [&](const auto& e) { return ids.count(e.id) > 0; });
}

template <typename C>
std::vector<int32_t> GetNonExistentIds(const C& allIds, bool includeZero = true) {
    if (allIds.empty()) {
        return includeZero ? std::vector<int32_t>{-1, 0, 1} : std::vector<int32_t>{-1, 1};
    }
    std::vector<int32_t> nonExistentIds;
    if (auto value = *std::min_element(allIds.begin(), allIds.end()) - 1;
        includeZero || value != 0) {
        nonExistentIds.push_back(value);
    } else {
        nonExistentIds.push_back(value - 1);
    }
    if (auto value = *std::max_element(allIds.begin(), allIds.end()) + 1;
        includeZero || value != 0) {
        nonExistentIds.push_back(value);
    } else {
        nonExistentIds.push_back(value + 1);
    }
    return nonExistentIds;
}

AudioDeviceAddress::Tag suggestDeviceAddressTag(const AudioDeviceDescription& description) {
    using Tag = AudioDeviceAddress::Tag;
    if (std::string_view connection = description.connection;
        connection == AudioDeviceDescription::CONNECTION_BT_A2DP ||
        // Note: BT LE Broadcast uses a "group id".
        (description.type != AudioDeviceType::OUT_BROADCAST &&
         connection == AudioDeviceDescription::CONNECTION_BT_LE) ||
        connection == AudioDeviceDescription::CONNECTION_BT_SCO ||
        connection == AudioDeviceDescription::CONNECTION_WIRELESS) {
        return Tag::mac;
    } else if (connection == AudioDeviceDescription::CONNECTION_IP_V4) {
        return Tag::ipv4;
    } else if (connection == AudioDeviceDescription::CONNECTION_USB) {
        return Tag::alsa;
    }
    return Tag::id;
}

AudioPort GenerateUniqueDeviceAddress(const AudioPort& port) {
    // Point-to-point connections do not use addresses.
    static const std::set<std::string> kPointToPointConnections = {
            AudioDeviceDescription::CONNECTION_ANALOG, AudioDeviceDescription::CONNECTION_HDMI,
            AudioDeviceDescription::CONNECTION_HDMI_ARC,
            AudioDeviceDescription::CONNECTION_HDMI_EARC, AudioDeviceDescription::CONNECTION_SPDIF};
    static int nextId = 0;
    using Tag = AudioDeviceAddress::Tag;
    const auto& deviceDescription = port.ext.get<AudioPortExt::Tag::device>().device.type;
    AudioDeviceAddress address = port.ext.get<AudioPortExt::Tag::device>().device.address;
    // If the address is already set, do not re-generate.
    if (address == AudioDeviceAddress() &&
        kPointToPointConnections.count(deviceDescription.connection) == 0) {
        switch (suggestDeviceAddressTag(deviceDescription)) {
            case Tag::id:
                address = AudioDeviceAddress::make<Tag::id>(std::to_string(++nextId));
                break;
            case Tag::mac:
                address = AudioDeviceAddress::make<Tag::mac>(
                        std::vector<uint8_t>{1, 2, 3, 4, 5, static_cast<uint8_t>(++nextId & 0xff)});
                break;
            case Tag::ipv4:
                address = AudioDeviceAddress::make<Tag::ipv4>(
                        std::vector<uint8_t>{192, 168, 0, static_cast<uint8_t>(++nextId & 0xff)});
                break;
            case Tag::ipv6:
                address = AudioDeviceAddress::make<Tag::ipv6>(std::vector<int32_t>{
                        0xfc00, 0x0123, 0x4567, 0x89ab, 0xcdef, 0, 0, ++nextId & 0xffff});
                break;
            case Tag::alsa:
                address = AudioDeviceAddress::make<Tag::alsa>(std::vector<int32_t>{1, ++nextId});
                break;
        }
    }
    AudioPort result = port;
    result.ext.get<AudioPortExt::Tag::device>().device.address = std::move(address);
    return result;
}

static const AudioFormatDescription kApeFileAudioFormat = {.encoding = "audio/x-ape"};
static const AudioChannelLayout kApeFileChannelMask =
        AudioChannelLayout::make<AudioChannelLayout::layoutMask>(AudioChannelLayout::LAYOUT_MONO);
struct MediaFileInfo {
    std::string path;
    int32_t bps;
    int32_t durationMs;
};
static const std::map<AudioConfigBase, MediaFileInfo> kMediaFileDataInfos = {
        {{44100, kApeFileChannelMask, kApeFileAudioFormat},
         {"/data/local/tmp/sine882hz_44100_3s.ape", 217704, 3000}},
        {{48000, kApeFileChannelMask, kApeFileAudioFormat},
         {"/data/local/tmp/sine960hz_48000_3s.ape", 236256, 3000}},
};

std::optional<MediaFileInfo> getMediaFileInfoForConfig(const AudioConfigBase& config) {
    const auto it = kMediaFileDataInfos.find(config);
    if (it != kMediaFileDataInfos.end()) return it->second;
    return std::nullopt;
}

std::optional<MediaFileInfo> getMediaFileInfoForConfig(const AudioPortConfig& config) {
    if (!config.sampleRate.has_value() || !config.format.has_value() ||
        !config.channelMask.has_value()) {
        return std::nullopt;
    }
    return getMediaFileInfoForConfig(AudioConfigBase{
            config.sampleRate->value, config.channelMask.value(), config.format.value()});
}

std::optional<AudioOffloadInfo> generateOffloadInfoIfNeeded(const AudioPortConfig& portConfig) {
    if (portConfig.flags.has_value() &&
        portConfig.flags.value().getTag() == AudioIoFlags::Tag::output &&
        isBitPositionFlagSet(portConfig.flags.value().get<AudioIoFlags::Tag::output>(),
                             AudioOutputFlags::COMPRESS_OFFLOAD)) {
        AudioOffloadInfo offloadInfo;
        offloadInfo.base.sampleRate = portConfig.sampleRate.value().value;
        offloadInfo.base.channelMask = portConfig.channelMask.value();
        offloadInfo.base.format = portConfig.format.value();
        if (auto info = getMediaFileInfoForConfig(portConfig); info.has_value()) {
            offloadInfo.bitRatePerSecond = info->bps;
            offloadInfo.durationUs = info->durationMs * 1000LL;
        } else {
            offloadInfo.bitRatePerSecond = 256000;                             // Arbitrary value.
            offloadInfo.durationUs = std::chrono::microseconds(1min).count();  // Arbitrary value.
        }
        offloadInfo.usage = AudioUsage::MEDIA;
        offloadInfo.encapsulationMode = AudioEncapsulationMode::NONE;
        return offloadInfo;
    }
    return {};
}

// All 'With*' classes are move-only because they are associated with some
// resource or state of a HAL module.
class WithDebugFlags {
  public:
    static WithDebugFlags createNested(const WithDebugFlags& parent) {
        return WithDebugFlags(parent.mFlags);
    }

    WithDebugFlags() = default;
    explicit WithDebugFlags(const ModuleDebug& initial) : mInitial(initial), mFlags(initial) {}
    WithDebugFlags(const WithDebugFlags&) = delete;
    WithDebugFlags& operator=(const WithDebugFlags&) = delete;
    ~WithDebugFlags() {
        if (mModule != nullptr) {
            EXPECT_IS_OK(mModule->setModuleDebug(mInitial));
        }
    }
    void SetUp(IModule* module) {
        ASSERT_IS_OK(module->setModuleDebug(mFlags));
        mModule = module;
    }
    ModuleDebug& flags() { return mFlags; }

  private:
    ModuleDebug mInitial;
    ModuleDebug mFlags;
    IModule* mModule = nullptr;
};

template <typename T>
class WithModuleParameter {
  public:
    WithModuleParameter(const std::string parameterId, const T& value)
        : mParameterId(parameterId), mValue(value) {}
    WithModuleParameter(const WithModuleParameter&) = delete;
    WithModuleParameter& operator=(const WithModuleParameter&) = delete;
    ~WithModuleParameter() {
        if (mModule != nullptr) {
            VendorParameter parameter{.id = mParameterId};
            parameter.ext.setParcelable(mInitial);
            EXPECT_IS_OK(mModule->setVendorParameters({parameter}, false));
        }
    }
    ScopedAStatus SetUpNoChecks(IModule* module, bool failureExpected) {
        std::vector<VendorParameter> parameters;
        ScopedAStatus result = module->getVendorParameters({mParameterId}, &parameters);
        if (result.isOk() && parameters.size() == 1) {
            std::optional<T> maybeInitial;
            binder_status_t status = parameters[0].ext.getParcelable(&maybeInitial);
            if (status == STATUS_OK && maybeInitial.has_value()) {
                mInitial = maybeInitial.value();
                VendorParameter parameter{.id = mParameterId};
                parameter.ext.setParcelable(mValue);
                result = module->setVendorParameters({parameter}, false);
                if (result.isOk()) {
                    LOG(INFO) << __func__ << ": overriding parameter \"" << mParameterId
                              << "\" with " << mValue.toString()
                              << ", old value: " << mInitial.toString();
                    mModule = module;
                }
            } else {
                LOG(ERROR) << __func__ << ": error while retrieving the value of \"" << mParameterId
                           << "\"";
                return ScopedAStatus::fromStatus(status);
            }
        }
        if (!result.isOk()) {
            LOG(failureExpected ? INFO : ERROR)
                    << __func__ << ": can not override vendor parameter \"" << mParameterId << "\""
                    << result;
        }
        return result;
    }

  private:
    const std::string mParameterId;
    const T mValue;
    IModule* mModule = nullptr;
    T mInitial;
};

// For consistency, WithAudioPortConfig can start both with a non-existent
// port config, and with an existing one. Existence is determined by the
// id of the provided config. If it's not 0, then WithAudioPortConfig is
// essentially a no-op wrapper.
class WithAudioPortConfig {
  public:
    WithAudioPortConfig() = default;
    explicit WithAudioPortConfig(const AudioPortConfig& config) : mInitialConfig(config) {}
    WithAudioPortConfig(const WithAudioPortConfig&) = delete;
    WithAudioPortConfig& operator=(const WithAudioPortConfig&) = delete;
    ~WithAudioPortConfig() {
        if (mModule != nullptr) {
            EXPECT_IS_OK(mModule->resetAudioPortConfig(getId())) << "port config id " << getId();
        }
    }
    void SetUp(IModule* module) {
        ASSERT_NE(AudioPortExt::Tag::unspecified, mInitialConfig.ext.getTag())
                << "config: " << mInitialConfig.toString();
        // Negotiation is allowed for device ports because the HAL module is
        // allowed to provide an empty profiles list for attached devices.
        ASSERT_NO_FATAL_FAILURE(
                SetUpImpl(module, mInitialConfig.ext.getTag() == AudioPortExt::Tag::device));
    }
    int32_t getId() const { return mConfig.id; }
    const AudioPortConfig& get() const { return mConfig; }

  private:
    void SetUpImpl(IModule* module, bool negotiate) {
        if (mInitialConfig.id == 0) {
            AudioPortConfig suggested;
            bool applied = false;
            ASSERT_IS_OK(module->setAudioPortConfig(mInitialConfig, &suggested, &applied))
                    << "Config: " << mInitialConfig.toString();
            if (!applied && negotiate) {
                mInitialConfig = suggested;
                ASSERT_NO_FATAL_FAILURE(SetUpImpl(module, false))
                        << " while applying suggested config: " << suggested.toString();
            } else {
                ASSERT_TRUE(applied) << "Suggested: " << suggested.toString();
                mConfig = suggested;
                mModule = module;
            }
        } else {
            mConfig = mInitialConfig;
        }
    }

    AudioPortConfig mInitialConfig;
    IModule* mModule = nullptr;
    AudioPortConfig mConfig;
};

template <typename T>
void GenerateTestArrays(size_t validElementCount, T validMin, T validMax,
                        std::vector<std::vector<T>>* validValues,
                        std::vector<std::vector<T>>* invalidValues) {
    validValues->emplace_back(validElementCount, validMin);
    validValues->emplace_back(validElementCount, validMax);
    validValues->emplace_back(validElementCount, (validMin + validMax) / 2.f);
    if (validElementCount > 0) {
        invalidValues->emplace_back(validElementCount - 1, validMin);
    }
    invalidValues->emplace_back(validElementCount + 1, validMin);
    for (auto m : {-2, -1, 2}) {
        const auto invalidMin = m * validMin;
        if (invalidMin < validMin || invalidMin > validMax) {
            invalidValues->emplace_back(validElementCount, invalidMin);
        }
        const auto invalidMax = m * validMax;
        if (invalidMax < validMin || invalidMax > validMax) {
            invalidValues->emplace_back(validElementCount, invalidMax);
        }
    }
}

template <typename PropType, class Instance, typename Getter, typename Setter>
void TestAccessors(Instance* inst, Getter getter, Setter setter,
                   const std::vector<PropType>& validValues,
                   const std::vector<PropType>& invalidValues, bool* isSupported,
                   const std::vector<PropType>* ambivalentValues = nullptr) {
    PropType initialValue{};
    ScopedAStatus status = (inst->*getter)(&initialValue);
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        *isSupported = false;
        return;
    }
    ASSERT_TRUE(status.isOk()) << "Unexpected status from a getter: " << status;
    *isSupported = true;
    for (const auto v : validValues) {
        EXPECT_IS_OK((inst->*setter)(v)) << "for a valid value: " << ::testing::PrintToString(v);
        PropType currentValue{};
        EXPECT_IS_OK((inst->*getter)(&currentValue));
        EXPECT_EQ(v, currentValue);
    }
    for (const auto v : invalidValues) {
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, (inst->*setter)(v))
                << "for an invalid value: " << ::testing::PrintToString(v);
    }
    if (ambivalentValues != nullptr) {
        for (const auto v : *ambivalentValues) {
            const auto status = (inst->*setter)(v);
            if (!status.isOk()) {
                EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, status)
                        << "for an ambivalent value: " << ::testing::PrintToString(v);
            }
        }
    }
    EXPECT_IS_OK((inst->*setter)(initialValue)) << "Failed to restore the initial value";
}

template <class Instance>
void TestGetVendorParameters(Instance* inst, bool* isSupported) {
    static const std::vector<std::vector<std::string>> kIdsLists = {{}, {"zero"}, {"one", "two"}};
    static const auto kStatuses = {EX_ILLEGAL_ARGUMENT, EX_ILLEGAL_STATE, EX_UNSUPPORTED_OPERATION};
    for (const auto& ids : kIdsLists) {
        std::vector<VendorParameter> params;
        if (ndk::ScopedAStatus status = inst->getVendorParameters(ids, &params); status.isOk()) {
            EXPECT_EQ(ids.size(), params.size()) << "Size of the returned parameters list must "
                                                 << "match the size of the provided ids list";
            for (const auto& param : params) {
                EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), param.id))
                        << "Returned parameter id \"" << param.id << "\" is unexpected";
            }
            for (const auto& id : ids) {
                EXPECT_NE(params.end(),
                          std::find_if(params.begin(), params.end(),
                                       [&](const auto& param) { return param.id == id; }))
                        << "Requested parameter with id \"" << id << "\" was not returned";
            }
        } else {
            EXPECT_STATUS(kStatuses, status);
            if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
                *isSupported = false;
                return;
            }
        }
    }
    *isSupported = true;
}

template <class Instance>
void TestSetVendorParameters(Instance* inst, bool* isSupported) {
    static const auto kStatuses = {EX_NONE, EX_ILLEGAL_ARGUMENT, EX_ILLEGAL_STATE,
                                   EX_UNSUPPORTED_OPERATION};
    static const std::vector<std::vector<VendorParameter>> kParamsLists = {
            {}, {VendorParameter{"zero"}}, {VendorParameter{"one"}, VendorParameter{"two"}}};
    for (const auto& params : kParamsLists) {
        ndk::ScopedAStatus status = inst->setVendorParameters(params, false);
        if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
            *isSupported = false;
            return;
        }
        EXPECT_STATUS(kStatuses, status)
                << ::android::internal::ToString(params) << ", async: false";
        EXPECT_STATUS(kStatuses, inst->setVendorParameters(params, true))
                << ::android::internal::ToString(params) << ", async: true";
    }
    *isSupported = true;
}

// Can be used as a base for any test here, does not depend on the fixture GTest parameters.
class AudioCoreModuleBase {
  public:
    // Fixed buffer size are used for negative tests only. For any tests involving stream
    // opening that must success, the minimum buffer size must be obtained from a patch.
    // This is implemented by the 'StreamFixture' utility class.
    static constexpr int kNegativeTestBufferSizeFrames = 256;
    static constexpr int kDefaultLargeBufferSizeFrames = 48000;

    void SetUpImpl(const std::string& moduleName, bool setUpDebug = true) {
        ASSERT_NO_FATAL_FAILURE(ConnectToService(moduleName, setUpDebug));
        ASSERT_IS_OK(module->getAudioPorts(&initialPorts));
        ASSERT_IS_OK(module->getAudioRoutes(&initialRoutes));
    }

    void TearDownImpl() {
        debug.reset();
        ASSERT_NE(module, nullptr);
        std::vector<AudioPort> finalPorts;
        ASSERT_IS_OK(module->getAudioPorts(&finalPorts));
        EXPECT_NO_FATAL_FAILURE(VerifyVectorsAreEqual<AudioPort>(initialPorts, finalPorts))
                << "The list of audio ports was not restored to the initial state";
        std::vector<AudioRoute> finalRoutes;
        ASSERT_IS_OK(module->getAudioRoutes(&finalRoutes));
        EXPECT_NO_FATAL_FAILURE(VerifyVectorsAreEqual<AudioRoute>(initialRoutes, finalRoutes))
                << "The list of audio routes was not restored to the initial state";
    }

    void ConnectToService(const std::string& moduleName, bool setUpDebug) {
        ASSERT_EQ(module, nullptr);
        ASSERT_EQ(debug, nullptr);
        module = IModule::fromBinder(binderUtil.connectToService(moduleName));
        ASSERT_NE(module, nullptr);
        if (setUpDebug) {
            ASSERT_NO_FATAL_FAILURE(SetUpDebug());
        }
        ASSERT_TRUE(module->getInterfaceVersion(&aidlVersion).isOk());
    }

    void RestartService() {
        ASSERT_NE(module, nullptr);
        moduleConfig.reset();
        const bool setUpDebug = !!debug;
        debug.reset();
        module = IModule::fromBinder(binderUtil.restartService());
        ASSERT_NE(module, nullptr);
        if (setUpDebug) {
            ASSERT_NO_FATAL_FAILURE(SetUpDebug());
        }
        ASSERT_TRUE(module->getInterfaceVersion(&aidlVersion).isOk());
    }

    void SetUpDebug() {
        debug.reset(new WithDebugFlags());
        debug->flags().simulateDeviceConnections = true;
        ASSERT_NO_FATAL_FAILURE(debug->SetUp(module.get()));
    }

    void ApplyEveryConfig(const std::vector<AudioPortConfig>& configs) {
        for (const auto& config : configs) {
            ASSERT_NE(0, config.portId);
            WithAudioPortConfig portConfig(config);
            ASSERT_NO_FATAL_FAILURE(portConfig.SetUp(module.get()));  // calls setAudioPortConfig
            EXPECT_EQ(config.portId, portConfig.get().portId);
            std::vector<AudioPortConfig> retrievedPortConfigs;
            ASSERT_IS_OK(module->getAudioPortConfigs(&retrievedPortConfigs));
            const int32_t portConfigId = portConfig.getId();
            auto configIt = std::find_if(
                    retrievedPortConfigs.begin(), retrievedPortConfigs.end(),
                    [&portConfigId](const auto& retrConf) { return retrConf.id == portConfigId; });
            EXPECT_NE(configIt, retrievedPortConfigs.end())
                    << "Port config id returned by setAudioPortConfig: " << portConfigId
                    << " is not found in the list returned by getAudioPortConfigs";
            if (configIt != retrievedPortConfigs.end()) {
                EXPECT_EQ(portConfig.get(), *configIt)
                        << "Applied port config returned by setAudioPortConfig: "
                        << portConfig.get().toString()
                        << " is not the same as retrieved via getAudioPortConfigs: "
                        << configIt->toString();
            }
        }
    }

    template <typename Entity>
    void GetAllEntityIds(std::set<int32_t>* entityIds,
                         ScopedAStatus (IModule::*getter)(std::vector<Entity>*),
                         const std::string& errorMessage) {
        std::vector<Entity> entities;
        { ASSERT_IS_OK((module.get()->*getter)(&entities)); }
        *entityIds = extractIds<Entity>(entities);
        EXPECT_EQ(entities.size(), entityIds->size()) << errorMessage;
    }

    void GetAllPatchIds(std::set<int32_t>* patchIds) {
        return GetAllEntityIds<AudioPatch>(
                patchIds, &IModule::getAudioPatches,
                "IDs of audio patches returned by IModule.getAudioPatches are not unique");
    }

    void GetAllPortIds(std::set<int32_t>* portIds) {
        return GetAllEntityIds<AudioPort>(
                portIds, &IModule::getAudioPorts,
                "IDs of audio ports returned by IModule.getAudioPorts are not unique");
    }

    void GetAllPortConfigIds(std::set<int32_t>* portConfigIds) {
        return GetAllEntityIds<AudioPortConfig>(
                portConfigIds, &IModule::getAudioPortConfigs,
                "IDs of audio port configs returned by IModule.getAudioPortConfigs are not unique");
    }

    void SetUpModuleConfig() {
        if (moduleConfig == nullptr) {
            moduleConfig = std::make_unique<ModuleConfig>(module.get());
            ASSERT_EQ(EX_NONE, moduleConfig->getStatus().getExceptionCode())
                    << "ModuleConfig init error: " << moduleConfig->getError();
        }
    }

    // Warning: modifies the vectors!
    template <typename T>
    void VerifyVectorsAreEqual(std::vector<T>& v1, std::vector<T>& v2) {
        ASSERT_EQ(v1.size(), v2.size());
        std::sort(v1.begin(), v1.end());
        std::sort(v2.begin(), v2.end());
        if (v1 != v2) {
            FAIL() << "Vectors are not equal: v1 = " << ::android::internal::ToString(v1)
                   << ", v2 = " << ::android::internal::ToString(v2);
        }
    }

    std::shared_ptr<IModule> module;
    std::unique_ptr<ModuleConfig> moduleConfig;
    AudioHalBinderServiceUtil binderUtil;
    std::unique_ptr<WithDebugFlags> debug;
    std::vector<AudioPort> initialPorts;
    std::vector<AudioRoute> initialRoutes;
    int32_t aidlVersion = -1;
};

class WithDevicePortConnectedState {
  public:
    explicit WithDevicePortConnectedState(const AudioPort& idAndData) : mIdAndData(idAndData) {}
    WithDevicePortConnectedState(const WithDevicePortConnectedState&) = delete;
    WithDevicePortConnectedState& operator=(const WithDevicePortConnectedState&) = delete;
    ~WithDevicePortConnectedState() {
        if (mModule != nullptr) {
            EXPECT_IS_OK_OR_UNKNOWN_TRANSACTION(mModule->prepareToDisconnectExternalDevice(getId()))
                    << "when preparing to disconnect device port ID " << getId();
            EXPECT_IS_OK(mModule->disconnectExternalDevice(getId()))
                    << "when disconnecting device port ID " << getId();
        }
        if (mModuleConfig != nullptr) {
            EXPECT_IS_OK(mModuleConfig->onExternalDeviceDisconnected(mModule, mConnectedPort))
                    << "when external device disconnected";
        }
    }
    ScopedAStatus SetUpNoChecks(IModule* module, ModuleConfig* moduleConfig) {
        RETURN_STATUS_IF_ERROR(module->connectExternalDevice(mIdAndData, &mConnectedPort));
        RETURN_STATUS_IF_ERROR(moduleConfig->onExternalDeviceConnected(module, mConnectedPort));
        mModule = module;
        mModuleConfig = moduleConfig;
        return ScopedAStatus::ok();
    }
    void SetUp(IModule* module, ModuleConfig* moduleConfig) {
        ASSERT_NE(moduleConfig, nullptr);
        ASSERT_IS_OK(SetUpNoChecks(module, moduleConfig))
                << "when connecting device port ID & data " << mIdAndData.toString();
        ASSERT_NE(mIdAndData.id, getId())
                << "ID of the connected port must not be the same as the ID of the template port";
    }
    int32_t getId() const { return mConnectedPort.id; }
    const AudioPort& get() { return mConnectedPort; }

  private:
    const AudioPort mIdAndData;
    IModule* mModule = nullptr;
    ModuleConfig* mModuleConfig = nullptr;
    AudioPort mConnectedPort;
};

class AudioCoreModule : public AudioCoreModuleBase, public testing::TestWithParam<std::string> {
  public:
    void SetUp() override { ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam())); }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }
};

class StreamContext {
  public:
    typedef AidlMessageQueue<StreamDescriptor::Command, SynchronizedReadWrite> CommandMQ;
    typedef AidlMessageQueue<StreamDescriptor::Reply, SynchronizedReadWrite> ReplyMQ;
    typedef AidlMessageQueue<int8_t, SynchronizedReadWrite> DataMQ;

    explicit StreamContext(const StreamDescriptor& descriptor, const AudioConfigBase& config,
                           AudioIoFlags flags)
        : mFrameSizeBytes(descriptor.frameSizeBytes),
          mConfig(config),
          mCommandMQ(new CommandMQ(descriptor.command)),
          mReplyMQ(new ReplyMQ(descriptor.reply)),
          mBufferSizeFrames(descriptor.bufferSizeFrames),
          mFlags(flags),
          mDataMQ(maybeCreateDataMQ(descriptor)),
          mIsMmapped(isMmapped(descriptor)),
          mMmapBurstSizeFrames(getMmapBurstSizeFrames(descriptor)),
          mSharedMemoryFd(maybeGetMmapFd(descriptor)) {}
    void checkIsValid() const {
        EXPECT_NE(0UL, mFrameSizeBytes);
        ASSERT_NE(nullptr, mCommandMQ);
        EXPECT_TRUE(mCommandMQ->isValid());
        ASSERT_NE(nullptr, mReplyMQ);
        EXPECT_TRUE(mReplyMQ->isValid());
        if (isMmapped()) {
            EXPECT_NE(0, mMmapBurstSizeFrames) << "MMAP burst size must not be zero";
        } else {
            ASSERT_NE(nullptr, mDataMQ);
            EXPECT_TRUE(mDataMQ->isValid());
            EXPECT_GE(mDataMQ->getQuantumCount() * mDataMQ->getQuantumSize(),
                      mFrameSizeBytes * mBufferSizeFrames)
                    << "Data MQ actual buffer size is "
                       "less than the buffer size as specified by the descriptor";
        }
    }
    size_t getBufferSizeBytes() const { return mFrameSizeBytes * mBufferSizeFrames; }
    size_t getBufferSizeFrames() const { return mBufferSizeFrames; }
    CommandMQ* getCommandMQ() const { return mCommandMQ.get(); }
    const AudioConfigBase& getConfig() const { return mConfig; }
    DataMQ* getDataMQ() const { return mDataMQ.get(); }
    AudioIoFlags getFlags() const { return mFlags; }
    size_t getFrameSizeBytes() const { return mFrameSizeBytes; }
    ReplyMQ* getReplyMQ() const { return mReplyMQ.get(); }
    int getSampleRate() const { return mConfig.sampleRate; }
    bool isMmapped() const { return mIsMmapped; }
    int32_t getMmapBurstSizeFrames() const { return mMmapBurstSizeFrames; }
    int getMmapFd() const { return mSharedMemoryFd; }

  private:
    static std::unique_ptr<DataMQ> maybeCreateDataMQ(const StreamDescriptor& descriptor) {
        using Tag = StreamDescriptor::AudioBuffer::Tag;
        if (descriptor.audio.getTag() == Tag::fmq) {
            return std::make_unique<DataMQ>(descriptor.audio.get<Tag::fmq>());
        }
        return nullptr;
    }
    static int32_t getMmapBurstSizeFrames(const StreamDescriptor& descriptor) {
        using Tag = StreamDescriptor::AudioBuffer::Tag;
        if (descriptor.audio.getTag() == Tag::mmap) {
            return descriptor.audio.get<Tag::mmap>().burstSizeFrames;
        }
        return -1;
    }
    static bool isMmapped(const StreamDescriptor& descriptor) {
        using Tag = StreamDescriptor::AudioBuffer::Tag;
        return descriptor.audio.getTag() == Tag::mmap;
    }
    static int32_t maybeGetMmapFd(const StreamDescriptor& descriptor) {
        using Tag = StreamDescriptor::AudioBuffer::Tag;
        if (descriptor.audio.getTag() == Tag::mmap) {
            return descriptor.audio.get<Tag::mmap>().sharedMemory.fd.get();
        }
        return -1;
    }

    const size_t mFrameSizeBytes;
    const AudioConfigBase mConfig;
    std::unique_ptr<CommandMQ> mCommandMQ;
    std::unique_ptr<ReplyMQ> mReplyMQ;
    const size_t mBufferSizeFrames;
    const AudioIoFlags mFlags;
    std::unique_ptr<DataMQ> mDataMQ;
    const bool mIsMmapped;
    const int32_t mMmapBurstSizeFrames;
    const int32_t mSharedMemoryFd;  // owned by StreamDescriptor
};

struct StreamWorkerMethods {
    virtual ~StreamWorkerMethods() = default;
    virtual bool createMmapBuffer(MmapBufferDescriptor* desc) = 0;
    virtual bool supportsCreateMmapBuffer() = 0;
};

class MmapSharedMemory {
  public:
    explicit MmapSharedMemory(const StreamContext& context, StreamWorkerMethods* stream)
        : mStream(stream),
          mBufferSizeBytes(context.getBufferSizeBytes()),
          mSharedMemoryFd(::dup(context.getMmapFd())) {}
    ~MmapSharedMemory() { releaseSharedMemory(); }

    int8_t* getMmapMemory() {
        if (mSharedMemory != nullptr) return mSharedMemory;
        if (mSharedMemoryFd.get() != -1) {
            int8_t* sharedMemory = (int8_t*)mmap(nullptr, mBufferSizeBytes, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, mSharedMemoryFd.get(), 0);
            if (sharedMemory != MAP_FAILED && sharedMemory != nullptr) {
                mSharedMemory = sharedMemory;
            } else {
                PLOG(ERROR) << __func__ << ": mmap() failed, fd " << mSharedMemoryFd.get()
                            << ", size " << mBufferSizeBytes;
            }
        } else {
            LOG(WARNING) << __func__ << ": shared memory FD has not been set yet";
        }
        return mSharedMemory;
    }
    bool updateMmapSharedMemoryIfNeeded(StreamDescriptor::State state) {
        if (mPreviousState == StreamDescriptor::State::STANDBY &&
            state != StreamDescriptor::State::STANDBY && state != StreamDescriptor::State::ERROR) {
            LOG(INFO) << "Mmap stream exited standby, update Mmap buffer";
            MmapBufferDescriptor desc;
            if (!mStream->createMmapBuffer(&desc)) return false;
            updateMmapSharedMemoryFd(desc);
        }
        mPreviousState = state;
        return true;
    }

  private:
    static ndk::ScopedFileDescriptor getMmapFd(const MmapBufferDescriptor& desc) {
        return desc.sharedMemory.fd.get() != -1 ? desc.sharedMemory.fd.dup()
                                                : ndk::ScopedFileDescriptor{};
    }
    void releaseSharedMemory() {
        if (mSharedMemory != nullptr) {
            munmap(mSharedMemory, mBufferSizeBytes);
        }
        mSharedMemory = nullptr;
    }
    void updateMmapSharedMemoryFd(const MmapBufferDescriptor& desc) {
        mSharedMemoryFd = getMmapFd(desc);
        releaseSharedMemory();
    }

    StreamWorkerMethods* const mStream;
    const size_t mBufferSizeBytes;
    ndk::ScopedFileDescriptor mSharedMemoryFd;
    // Maps on the worker thread, may unmap in the destructor on the main thread.
    std::atomic<int8_t*> mSharedMemory = nullptr;
    // 'STANDBY' is always the starting state for a stream.
    StreamDescriptor::State mPreviousState = StreamDescriptor::State::STANDBY;
};

struct StreamEventReceiver {
    virtual ~StreamEventReceiver() = default;
    enum class Event { None, DrainReady, Error, TransferReady };
    virtual std::tuple<int, Event> getLastEvent() const = 0;
    virtual std::tuple<int, Event> waitForEvent(int clientEventSeq) = 0;
    static constexpr int kEventSeqInit = -1;
};
std::string toString(StreamEventReceiver::Event event) {
    switch (event) {
        case StreamEventReceiver::Event::None:
            return "None";
        case StreamEventReceiver::Event::DrainReady:
            return "DrainReady";
        case StreamEventReceiver::Event::Error:
            return "Error";
        case StreamEventReceiver::Event::TransferReady:
            return "TransferReady";
    }
    return std::to_string(static_cast<int32_t>(event));
}

// Note: we use a reference wrapper, not a pointer, because methods of std::*list
// return references to inserted elements. This way, we can put a returned reference
// into the children vector without any type conversions, and this makes DAG creation
// code more clear.
template <typename T>
struct DagNode : public std::pair<T, std::vector<std::reference_wrapper<DagNode<T>>>> {
    using Children = std::vector<std::reference_wrapper<DagNode>>;
    DagNode(const T& t, const Children& c) : std::pair<T, Children>(t, c) {}
    DagNode(T&& t, Children&& c) : std::pair<T, Children>(std::move(t), std::move(c)) {}
    const T& datum() const { return this->first; }
    Children& children() { return this->second; }
    const Children& children() const { return this->second; }
};
// Since DagNodes do contain references to next nodes, node links provided
// by the list are not used. Thus, the order of the nodes in the list is not
// important, except that the starting node must be at the front of the list,
// which means, it must always be added last.
template <typename T>
struct Dag : public std::forward_list<DagNode<T>> {
    Dag() = default;
    // We prohibit copying and moving Dag instances because implementing that
    // is not trivial due to references between nodes.
    Dag(const Dag&) = delete;
    Dag(Dag&&) = delete;
    Dag& operator=(const Dag&) = delete;
    Dag& operator=(Dag&&) = delete;
};

// Transition to the next state happens either due to a command from the client,
// or after an event received from the server.
using TransitionTrigger = std::variant<StreamDescriptor::Command, StreamEventReceiver::Event>;
std::string toString(const TransitionTrigger& trigger) {
    if (std::holds_alternative<StreamDescriptor::Command>(trigger)) {
        return std::string("'")
                .append(toString(std::get<StreamDescriptor::Command>(trigger).getTag()))
                .append("' command");
    }
    return std::string("'")
            .append(toString(std::get<StreamEventReceiver::Event>(trigger)))
            .append("' event");
}

struct StateSequence {
    virtual ~StateSequence() = default;
    virtual void rewind() = 0;
    virtual bool done() const = 0;
    virtual TransitionTrigger getTrigger() = 0;
    virtual std::set<StreamDescriptor::State> getExpectedStates() = 0;
    virtual void advance(StreamDescriptor::State state) = 0;
};

// Defines the current state and the trigger to transfer to the next one,
// thus "state" is the "from" state.
using StateTransitionFrom = std::pair<StreamDescriptor::State, TransitionTrigger>;

static const StreamDescriptor::Command kGetStatusCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::getStatus>(Void{});
static const StreamDescriptor::Command kStartCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::start>(Void{});
static const StreamDescriptor::Command kBurstCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::burst>(0);
static const StreamDescriptor::Command kDrainInCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::drain>(
                StreamDescriptor::DrainMode::DRAIN_UNSPECIFIED);
static const StreamDescriptor::Command kDrainOutAllCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::drain>(
                StreamDescriptor::DrainMode::DRAIN_ALL);
static const StreamDescriptor::Command kDrainOutEarlyCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::drain>(
                StreamDescriptor::DrainMode::DRAIN_EARLY_NOTIFY);
static const StreamDescriptor::Command kStandbyCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::standby>(Void{});
static const StreamDescriptor::Command kPauseCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::pause>(Void{});
static const StreamDescriptor::Command kFlushCommand =
        StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::flush>(Void{});
static const StreamEventReceiver::Event kTransferReadyEvent =
        StreamEventReceiver::Event::TransferReady;
static const StreamEventReceiver::Event kDrainReadyEvent = StreamEventReceiver::Event::DrainReady;

struct StateDag : public Dag<StateTransitionFrom> {
    using Node = StateDag::reference;
    using NextStates = StateDag::value_type::Children;

    template <typename... Next>
    Node makeNode(StreamDescriptor::State s, TransitionTrigger t, Next&&... next) {
        return emplace_front(std::make_pair(s, t), NextStates{std::forward<Next>(next)...});
    }
    Node makeNodes(const std::vector<StateTransitionFrom>& v, Node last) {
        auto helper = [&](auto i, auto&& h) -> Node {
            if (i == v.end()) return last;
            return makeNode(i->first, i->second, h(++i, h));
        };
        return helper(v.begin(), helper);
    }
    Node makeNodes(StreamDescriptor::State s, TransitionTrigger t, size_t count, Node last) {
        auto helper = [&](size_t c, auto&& h) -> Node {
            if (c == 0) return last;
            return makeNode(s, t, h(--c, h));
        };
        return helper(count, helper);
    }
    Node makeNodes(const std::vector<StateTransitionFrom>& v, StreamDescriptor::State f) {
        return makeNodes(v, makeFinalNode(f));
    }
    Node makeFinalNode(StreamDescriptor::State s) {
        // The actual command used here is irrelevant. Since it's the final node
        // in the test sequence, no commands sent after reaching it.
        return emplace_front(std::make_pair(s, kGetStatusCommand), NextStates{});
    }
};

class StateSequenceFollower : public StateSequence {
  public:
    explicit StateSequenceFollower(std::unique_ptr<StateDag> steps)
        : mSteps(std::move(steps)), mCurrent(mSteps->front()) {}
    void rewind() override { mCurrent = mSteps->front(); }
    bool done() const override { return current().children().empty(); }
    TransitionTrigger getTrigger() override { return current().datum().second; }
    std::set<StreamDescriptor::State> getExpectedStates() override {
        std::set<StreamDescriptor::State> result;
        std::transform(current().children().cbegin(), current().children().cend(),
                       std::inserter(result, result.begin()),
                       [](const auto& node) { return node.get().datum().first; });
        LOG(DEBUG) << __func__ << ": " << ::android::internal::ToString(result);
        return result;
    }
    void advance(StreamDescriptor::State state) override {
        if (auto it = std::find_if(
                    current().children().cbegin(), current().children().cend(),
                    [&](const auto& node) { return node.get().datum().first == state; });
            it != current().children().cend()) {
            LOG(DEBUG) << __func__ << ": " << toString(mCurrent.get().datum().first) << " -> "
                       << toString(it->get().datum().first);
            mCurrent = *it;
        } else {
            LOG(FATAL) << __func__ << ": state " << toString(state) << " is unexpected";
        }
    }

  private:
    StateDag::const_reference current() const { return mCurrent.get(); }
    std::unique_ptr<StateDag> mSteps;
    std::reference_wrapper<StateDag::value_type> mCurrent;
};

struct StreamLogicDriver {
    virtual ~StreamLogicDriver() = default;
    // Return 'true' to stop the worker.
    virtual bool done() = 0;
    // For 'Writer' logic, if the 'actualSize' is 0, write is skipped.
    // The 'fmqByteCount' from the returned command is passed as is to the HAL.
    virtual TransitionTrigger getNextTrigger(int maxDataSize, int* actualSize = nullptr) = 0;
    // Return 'true' to indicate that no further processing is needed,
    // for example, the driver is expecting a bad status to be returned.
    // The logic cycle will return with 'CONTINUE' status. Otherwise,
    // the reply will be validated and then passed to 'processValidReply'.
    virtual bool interceptRawReply(const StreamDescriptor::Reply& reply) = 0;
    // Return 'false' to indicate that the contents of the reply are unexpected.
    // Will abort the logic cycle.
    virtual bool processValidReply(const StreamDescriptor::Reply& reply) = 0;
};

class StreamCommonLogic : public StreamLogic {
  protected:
    StreamCommonLogic(const StreamContext& context, StreamLogicDriver* driver,
                      StreamWorkerMethods* stream, StreamEventReceiver* eventReceiver)
        : mCommandMQ(context.getCommandMQ()),
          mReplyMQ(context.getReplyMQ()),
          mDataMQ(context.getDataMQ()),
          mMmap(context, stream),
          mData(context.getBufferSizeBytes()),
          mDriver(driver),
          mEventReceiver(eventReceiver),
          mIsMmapped(context.isMmapped()),
          mMmapBurstSleep(mIsMmapped ? static_cast<double>(context.getMmapBurstSizeFrames()) /
                                               context.getSampleRate()
                                     : 0.0),
          mIsCompressOffload(context.getFlags().getTag() == AudioIoFlags::output &&
                             isBitPositionFlagSet(context.getFlags().get<AudioIoFlags::output>(),
                                                  AudioOutputFlags::COMPRESS_OFFLOAD)),
          mConfig(context.getConfig()) {}
    StreamContext::CommandMQ* getCommandMQ() const { return mCommandMQ; }
    const AudioConfigBase& getConfig() const { return mConfig; }
    StreamContext::ReplyMQ* getReplyMQ() const { return mReplyMQ; }
    StreamContext::DataMQ* getDataMQ() const { return mDataMQ; }
    StreamLogicDriver* getDriver() const { return mDriver; }
    StreamEventReceiver* getEventReceiver() const { return mEventReceiver; }
    int getSampleRate() const { return mConfig.sampleRate; }
    bool isCompressOffload() const { return mIsCompressOffload; }
    bool isMmapped() const { return mIsMmapped; }

    std::string init() override {
        LOG(DEBUG) << __func__ << ": isMmapped? " << mIsMmapped << ", MmapBurstSleep "
                   << mMmapBurstSleep << ", isCompressOffload? " << mIsCompressOffload << ", "
                   << mConfig.toString();
        return "";
    }
    const std::vector<int8_t>& getData() const { return mData; }
    void fillData(int8_t filler) { std::fill(mData.begin(), mData.end(), filler); }
    void loadData(std::ifstream& is, size_t* size) {
        *size = std::min(*size, mData.size());
        is.read(reinterpret_cast<char*>(mData.data()), *size);
    }
    std::optional<StreamDescriptor::Command> maybeGetNextCommand(int* actualSize = nullptr) {
        TransitionTrigger trigger = mDriver->getNextTrigger(mData.size(), actualSize);
        if (StreamEventReceiver::Event* expEvent =
                    std::get_if<StreamEventReceiver::Event>(&trigger);
            expEvent != nullptr) {
            auto [eventSeq, event] = mEventReceiver->waitForEvent(mLastEventSeq);
            mLastEventSeq = eventSeq;
            if (event != *expEvent) {
                // TODO: Make available as an error so it can be displayed by GTest
                LOG(ERROR) << __func__ << ": expected event " << toString(*expEvent) << ", got "
                           << toString(event);
                return {};
            }
            // If we were waiting for an event, the new stream state must be retrieved
            // via 'getStatus'.
            return StreamDescriptor::Command::make<StreamDescriptor::Command::Tag::getStatus>(
                    Void{});
        }
        return std::get<StreamDescriptor::Command>(trigger);
    }
    bool readDataFromMQ(size_t readCount) {
        std::vector<int8_t> data(readCount);
        if (mDataMQ->read(data.data(), readCount)) {
            memcpy(mData.data(), data.data(), std::min(mData.size(), data.size()));
            return true;
        }
        LOG(ERROR) << __func__ << ": reading of " << readCount << " bytes from MQ failed";
        return false;
    }
    bool writeDataToMQ() {
        if (mDataMQ->write(mData.data(), mData.size())) {
            return true;
        }
        LOG(ERROR) << __func__ << ": writing of " << mData.size() << " bytes to MQ failed";
        return false;
    }
    bool readDataFromMmap(size_t readCount) {
        if (auto memory = mMmap.getMmapMemory(); memory != nullptr) {
            std::memcpy(mData.data(), memory, readCount);
            // Since MMap `burst` does not block, need to sleep here to get an updated position.
            std::this_thread::sleep_for(mMmapBurstSleep);
            return true;
        }
        LOG(ERROR) << __func__ << ": reading of " << readCount << " bytes from MMap failed";
        return false;
    }
    bool writeDataToMmap() {
        if (auto memory = mMmap.getMmapMemory(); memory != nullptr) {
            std::memcpy(memory, mData.data(), mData.size());
            // Since MMap `burst` does not block, need to sleep here to get an updated position.
            std::this_thread::sleep_for(mMmapBurstSleep);
            return true;
        }
        LOG(ERROR) << __func__ << ": writing of " << mData.size() << " bytes to MMap failed";
        return false;
    }
    bool updateMmapSharedMemoryIfNeeded(StreamDescriptor::State state) {
        return isMmapped() ? mMmap.updateMmapSharedMemoryIfNeeded(state) : true;
    }

  private:
    StreamContext::CommandMQ* mCommandMQ;
    StreamContext::ReplyMQ* mReplyMQ;
    StreamContext::DataMQ* mDataMQ;
    MmapSharedMemory mMmap;
    std::vector<int8_t> mData;
    StreamLogicDriver* const mDriver;
    StreamEventReceiver* const mEventReceiver;
    int mLastEventSeq = StreamEventReceiver::kEventSeqInit;
    const bool mIsMmapped;
    const std::chrono::duration<double> mMmapBurstSleep;
    const bool mIsCompressOffload;
    const AudioConfigBase mConfig;
};

class StreamReaderLogic : public StreamCommonLogic {
  public:
    StreamReaderLogic(const StreamContext& context, StreamLogicDriver* driver,
                      StreamWorkerMethods* stream, StreamEventReceiver* eventReceiver)
        : StreamCommonLogic(context, driver, stream, eventReceiver),
          mMmapBurstSizeFrames(context.getMmapBurstSizeFrames()) {}
    // Should only be called after the worker has joined.
    const std::vector<int8_t>& getData() const { return StreamCommonLogic::getData(); }

  protected:
    Status cycle() override {
        if (getDriver()->done()) {
            LOG(DEBUG) << __func__ << ": clean exit";
            return Status::EXIT;
        }
        StreamDescriptor::Command command;
        if (auto maybeCommand = maybeGetNextCommand(); maybeCommand.has_value()) {
            command = std::move(maybeCommand.value());
        } else {
            LOG(ERROR) << __func__ << ": no next command";
            return Status::ABORT;
        }
        LOG(DEBUG) << "Writing command: " << command.toString();
        if (!getCommandMQ()->writeBlocking(&command, 1)) {
            LOG(ERROR) << __func__ << ": writing of command into MQ failed";
            return Status::ABORT;
        }
        StreamDescriptor::Reply reply{};
        LOG(DEBUG) << "Reading reply...";
        if (!getReplyMQ()->readBlocking(&reply, 1)) {
            return Status::ABORT;
        }
        LOG(DEBUG) << "Reply received: " << reply.toString();
        if (getDriver()->interceptRawReply(reply)) {
            LOG(DEBUG) << __func__ << ": reply has been intercepted by the driver";
            return Status::CONTINUE;
        }
        if (reply.status != STATUS_OK) {
            LOG(ERROR) << __func__ << ": received error status: " << statusToString(reply.status);
            return Status::ABORT;
        }
        if (reply.fmqByteCount < 0 ||
            (command.getTag() == StreamDescriptor::Command::Tag::burst &&
             reply.fmqByteCount > command.get<StreamDescriptor::Command::Tag::burst>())) {
            LOG(ERROR) << __func__
                       << ": received invalid byte count in the reply: " << reply.fmqByteCount;
            return Status::ABORT;
        }
        if (!isMmapped() &&
            static_cast<size_t>(reply.fmqByteCount) != getDataMQ()->availableToRead()) {
            LOG(ERROR) << __func__
                       << ": the byte count in the reply is not the same as the amount of "
                       << "data available in the MQ: " << reply.fmqByteCount
                       << " != " << getDataMQ()->availableToRead();
        }
        if (reply.latencyMs < 0 && reply.latencyMs != StreamDescriptor::LATENCY_UNKNOWN) {
            LOG(ERROR) << __func__ << ": received invalid latency value: " << reply.latencyMs;
            return Status::ABORT;
        }
        if (reply.xrunFrames < 0) {
            LOG(ERROR) << __func__ << ": received invalid xrunFrames value: " << reply.xrunFrames;
            return Status::ABORT;
        }
        if (std::find(enum_range<StreamDescriptor::State>().begin(),
                      enum_range<StreamDescriptor::State>().end(),
                      reply.state) == enum_range<StreamDescriptor::State>().end()) {
            LOG(ERROR) << __func__ << ": received invalid stream state: " << toString(reply.state);
            return Status::ABORT;
        }
        const bool acceptedReply = getDriver()->processValidReply(reply);
        if (const size_t readCount =
                    !isMmapped() ? getDataMQ()->availableToRead()
                                 : (command.getTag() == StreamDescriptor::Command::Tag::burst
                                            ? mMmapBurstSizeFrames
                                            : 0);
            readCount > 0) {
            fillData(-1);
            if (isMmapped() ? readDataFromMmap(readCount) : readDataFromMQ(readCount)) {
                goto checkAcceptedReply;
            }
            LOG(ERROR) << __func__ << ": reading of " << readCount << " data bytes failed";
            return Status::ABORT;
        }  // readCount == 0
    checkAcceptedReply:
        if (acceptedReply) {
            return updateMmapSharedMemoryIfNeeded(reply.state) ? Status::CONTINUE : Status::ABORT;
        }
        LOG(ERROR) << __func__ << ": unacceptable reply: " << reply.toString();
        return Status::ABORT;
    }

    const int32_t mMmapBurstSizeFrames;
};
using StreamReader = StreamWorker<StreamReaderLogic>;

class StreamWriterLogic : public StreamCommonLogic {
  public:
    StreamWriterLogic(const StreamContext& context, StreamLogicDriver* driver,
                      StreamWorkerMethods* stream, StreamEventReceiver* eventReceiver)
        : StreamCommonLogic(context, driver, stream, eventReceiver) {}
    // Should only be called after the worker has joined.
    const std::vector<int8_t>& getData() const { return StreamCommonLogic::getData(); }

  protected:
    std::string init() override {
        if (auto status = StreamCommonLogic::init(); !status.empty()) return status;
        if (isCompressOffload()) {
            const auto info = getMediaFileInfoForConfig(getConfig());
            if (info) {
                mCompressedMedia.open(info->path, std::ios::in | std::ios::binary);
                if (!mCompressedMedia.is_open()) {
                    return std::string("failed to open media file \"") + info->path + "\"";
                }
                mCompressedMedia.seekg(0, mCompressedMedia.end);
                mCompressedMediaSize = mCompressedMedia.tellg();
                mCompressedMedia.seekg(0, mCompressedMedia.beg);
                LOG(DEBUG) << __func__ << ": using media file \"" << info->path << "\", size "
                           << mCompressedMediaSize << " bytes";
            }
        }
        return "";
    }
    Status cycle() override {
        if (getDriver()->done()) {
            LOG(DEBUG) << __func__ << ": clean exit";
            return Status::EXIT;
        }
        int actualSize = 0;
        StreamDescriptor::Command command;
        if (auto maybeCommand = maybeGetNextCommand(&actualSize); maybeCommand.has_value()) {
            command = std::move(maybeCommand.value());
        } else {
            LOG(ERROR) << __func__ << ": no next command";
            return Status::ABORT;
        }
        if (actualSize > 0) {
            if (command.getTag() == StreamDescriptor::Command::burst) {
                if (!isCompressOffload()) {
                    fillData(mBurstIteration);
                    if (mBurstIteration < std::numeric_limits<int8_t>::max()) {
                        mBurstIteration++;
                    } else {
                        mBurstIteration = 0;
                    }
                } else {
                    fillData(0);
                    size_t size = std::min(static_cast<size_t>(actualSize),
                                           mCompressedMediaSize - mCompressedMediaPos);
                    loadData(mCompressedMedia, &size);
                    if (!mCompressedMedia.good()) {
                        LOG(ERROR) << __func__ << ": read failed";
                        return Status::ABORT;
                    }
                    LOG(DEBUG) << __func__ << ": read from file " << size << " bytes";
                    mCompressedMediaPos += size;
                    if (mCompressedMediaPos >= mCompressedMediaSize) {
                        mCompressedMedia.seekg(0, mCompressedMedia.beg);
                        mCompressedMediaPos = 0;
                        LOG(DEBUG) << __func__ << ": rewound to the beginning of the file";
                    }
                }
            }
            if (isMmapped() ? !writeDataToMmap() : !writeDataToMQ()) {
                return Status::ABORT;
            }
        }
        LOG(DEBUG) << "Writing command: " << command.toString();
        if (!getCommandMQ()->writeBlocking(&command, 1)) {
            LOG(ERROR) << __func__ << ": writing of command into MQ failed";
            return Status::ABORT;
        }
        StreamDescriptor::Reply reply{};
        LOG(DEBUG) << "Reading reply...";
        if (!getReplyMQ()->readBlocking(&reply, 1)) {
            LOG(ERROR) << __func__ << ": reading of reply from MQ failed";
            return Status::ABORT;
        }
        LOG(DEBUG) << "Reply received: " << reply.toString();
        if (getDriver()->interceptRawReply(reply)) {
            return Status::CONTINUE;
        }
        if (reply.status != STATUS_OK) {
            LOG(ERROR) << __func__ << ": received error status: " << statusToString(reply.status);
            return Status::ABORT;
        }
        if (reply.fmqByteCount < 0 ||
            (command.getTag() == StreamDescriptor::Command::Tag::burst &&
             reply.fmqByteCount > command.get<StreamDescriptor::Command::Tag::burst>())) {
            LOG(ERROR) << __func__
                       << ": received invalid byte count in the reply: " << reply.fmqByteCount;
            return Status::ABORT;
        }
        // It is OK for the implementation to leave data in the MQ when the stream is paused.
        if (!isMmapped() && reply.state != StreamDescriptor::State::PAUSED &&
            getDataMQ()->availableToWrite() != getDataMQ()->getQuantumCount()) {
            LOG(ERROR) << __func__ << ": the HAL module did not consume all data from the data MQ: "
                       << "available to write " << getDataMQ()->availableToWrite()
                       << ", total size: " << getDataMQ()->getQuantumCount();
            return Status::ABORT;
        }
        if (reply.latencyMs < 0 && reply.latencyMs != StreamDescriptor::LATENCY_UNKNOWN) {
            LOG(ERROR) << __func__ << ": received invalid latency value: " << reply.latencyMs;
            return Status::ABORT;
        }
        if (reply.xrunFrames < 0) {
            LOG(ERROR) << __func__ << ": received invalid xrunFrames value: " << reply.xrunFrames;
            return Status::ABORT;
        }
        if (std::find(enum_range<StreamDescriptor::State>().begin(),
                      enum_range<StreamDescriptor::State>().end(),
                      reply.state) == enum_range<StreamDescriptor::State>().end()) {
            LOG(ERROR) << __func__ << ": received invalid stream state: " << toString(reply.state);
            return Status::ABORT;
        }
        if (getDriver()->processValidReply(reply)) {
            return updateMmapSharedMemoryIfNeeded(reply.state) ? Status::CONTINUE : Status::ABORT;
        }
        LOG(ERROR) << __func__ << ": unacceptable reply: " << reply.toString();
        return Status::ABORT;
    }

  private:
    int8_t mBurstIteration = 1;
    std::ifstream mCompressedMedia;
    size_t mCompressedMediaSize = 0;
    size_t mCompressedMediaPos = 0;
};
using StreamWriter = StreamWorker<StreamWriterLogic>;

class DefaultStreamCallback : public ::aidl::android::hardware::audio::core::BnStreamCallback,
                              public StreamEventReceiver {
    ndk::ScopedAStatus onTransferReady() override {
        LOG(DEBUG) << __func__;
        putLastEvent(Event::TransferReady);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus onError() override {
        LOG(DEBUG) << __func__;
        putLastEvent(Event::Error);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus onDrainReady() override {
        LOG(DEBUG) << __func__;
        putLastEvent(Event::DrainReady);
        return ndk::ScopedAStatus::ok();
    }

  public:
    // To avoid timing out the whole test suite in case no event is received
    // from the HAL, use a local timeout for event waiting.
    // TODO: The timeout for 'onTransferReady' should depend on the buffer size.
    static constexpr auto kEventTimeoutMs = std::chrono::milliseconds(3000);

    StreamEventReceiver* getEventReceiver() { return this; }
    std::tuple<int, Event> getLastEvent() const override {
        std::lock_guard l(mLock);
        return getLastEvent_l();
    }
    std::tuple<int, Event> waitForEvent(int clientEventSeq) override {
        std::unique_lock l(mLock);
        android::base::ScopedLockAssertion lock_assertion(mLock);
        LOG(DEBUG) << __func__ << ": client " << clientEventSeq << ", last " << mLastEventSeq;
        if (mCv.wait_for(l, kEventTimeoutMs, [&]() {
                android::base::ScopedLockAssertion lock_assertion(mLock);
                return clientEventSeq < mLastEventSeq;
            })) {
        } else {
            LOG(WARNING) << __func__ << ": timed out waiting for an event";
            putLastEvent_l(Event::None);
        }
        return getLastEvent_l();
    }

  private:
    std::tuple<int, Event> getLastEvent_l() const REQUIRES(mLock) {
        return std::make_tuple(mLastEventSeq, mLastEvent);
    }
    void putLastEvent(Event event) {
        {
            std::lock_guard l(mLock);
            putLastEvent_l(event);
        }
        mCv.notify_one();
    }
    void putLastEvent_l(Event event) REQUIRES(mLock) {
        mLastEventSeq++;
        mLastEvent = event;
    }

    mutable std::mutex mLock;
    std::condition_variable mCv;
    int mLastEventSeq GUARDED_BY(mLock) = kEventSeqInit;
    Event mLastEvent GUARDED_BY(mLock) = Event::None;
};

template <typename T>
struct IOTraits {
    static constexpr bool is_input = std::is_same_v<T, IStreamIn>;
    static constexpr const char* directionStr = is_input ? "input" : "output";
    using Worker = std::conditional_t<is_input, StreamReader, StreamWriter>;
};

template <typename Stream>
class WithStream : public StreamWorkerMethods {
  public:
    static ndk::ScopedAStatus callClose(std::shared_ptr<Stream> stream) {
        std::shared_ptr<IStreamCommon> common;
        ndk::ScopedAStatus status = stream->getStreamCommon(&common);
        if (!status.isOk()) return status;
        status = common->prepareToClose();
        if (!status.isOk()) return status;
        return common->close();
    }

    WithStream() = default;
    explicit WithStream(const AudioPortConfig& portConfig) : mPortConfig(portConfig) {}
    WithStream(const WithStream&) = delete;
    WithStream& operator=(const WithStream&) = delete;
    ~WithStream() {
        if (mStream != nullptr) {
            mContext.reset();
            EXPECT_IS_OK(callClose(mStream)) << "port config id " << getPortId();
        }
    }
    void SetUpPortConfig(IModule* module) { ASSERT_NO_FATAL_FAILURE(mPortConfig.SetUp(module)); }
    ScopedAStatus SetUpNoChecks(IModule* module, long bufferSizeFrames) {
        return SetUpNoChecks(module, mPortConfig.get(), bufferSizeFrames);
    }
    ScopedAStatus SetUpNoChecks(IModule* module, const AudioPortConfig& portConfig,
                                long bufferSizeFrames);
    void SetUpStream(IModule* module, long bufferSizeFrames) {
        ASSERT_IS_OK(SetUpNoChecks(module, bufferSizeFrames)) << "port config id " << getPortId();
        ASSERT_NE(nullptr, mStream) << "port config id " << getPortId();
        EXPECT_GE(mDescriptor.bufferSizeFrames, bufferSizeFrames)
                << "actual buffer size must be no less than requested";
        const auto& config = mPortConfig.get();
        ASSERT_TRUE(config.channelMask.has_value());
        ASSERT_TRUE(config.format.has_value());
        ASSERT_TRUE(config.sampleRate.has_value());
        ASSERT_TRUE(config.flags.has_value());
        const AudioConfigBase cfg{config.sampleRate->value, *config.channelMask, *config.format};
        mContext.emplace(mDescriptor, cfg, config.flags.value());
        ASSERT_NO_FATAL_FAILURE(mContext.value().checkIsValid());
        ASSERT_IS_OK(mStream->getInterfaceVersion(&mInterfaceVersion));
    }
    void SetUp(IModule* module, long bufferSizeFrames) {
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfig(module));
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module, bufferSizeFrames));
    }
    Stream* get() const { return mStream.get(); }
    const StreamContext* getContext() const { return mContext ? &(mContext.value()) : nullptr; }
    StreamEventReceiver* getEventReceiver() { return mStreamCallback->getEventReceiver(); }
    std::shared_ptr<Stream> getSharedPointer() const { return mStream; }
    const AudioPortConfig& getPortConfig() const { return mPortConfig.get(); }
    int32_t getPortId() const { return mPortConfig.getId(); }
    // StreamWorkerMethods
    bool createMmapBuffer(MmapBufferDescriptor* desc) override {
        std::shared_ptr<IStreamCommon> common;
        ndk::ScopedAStatus status = mStream->getStreamCommon(&common);
        if (!status.isOk()) {
            LOG(ERROR) << __func__ << ": getStreamCommon failed: " << status.getMessage();
            return false;
        }
        if (mInterfaceVersion <= kAidlVersion3) {
            std::vector<VendorParameter> parameters;
            ScopedAStatus result = common->getVendorParameters({kCreateMmapBuffer}, &parameters);
            if (result.isOk() && parameters.size() == 1) {
                std::optional<MmapBufferDescriptor> result;
                binder_status_t status = parameters[0].ext.getParcelable(&result);
                if (status == ::android::OK) {
                    *desc = std::move(*result);
                    return true;
                } else {
                    LOG(ERROR) << __func__ << ": failed to extract parcelable: " << status;
                }
            } else {
                LOG(ERROR) << __func__
                           << ": failed to call 'createMmapBuffer' via 'getVendorParameter': "
                           << result.getMessage();
            }
        } else {
            // TODO: Use common->createMmapBuffer after interface update.
        }
        return false;
    }
    bool supportsCreateMmapBuffer() override {
        if (!mHasCreateMmapBuffer.has_value()) {
            if (mInterfaceVersion > kAidlVersion3) {
                mHasCreateMmapBuffer = true;
            } else {
                std::shared_ptr<IStreamCommon> common;
                ndk::ScopedAStatus status = mStream->getStreamCommon(&common);
                if (status.isOk()) {
                    VendorParameter createMmapBuffer{.id = kCreateMmapBuffer};
                    mHasCreateMmapBuffer =
                            common->setVendorParameters({createMmapBuffer}, false).isOk();
                } else {
                    LOG(ERROR) << __func__ << ": getStreamCommon failed: " << status.getMessage();
                    return false;
                }
            }
        }
        return mHasCreateMmapBuffer.value();
    }

  private:
    static constexpr const char* kCreateMmapBuffer = "aosp.createMmapBuffer";

    WithAudioPortConfig mPortConfig;
    std::shared_ptr<Stream> mStream;
    StreamDescriptor mDescriptor;
    std::optional<StreamContext> mContext;
    std::shared_ptr<DefaultStreamCallback> mStreamCallback;
    int32_t mInterfaceVersion = -1;
    std::optional<bool> mHasCreateMmapBuffer;
};

SinkMetadata GenerateSinkMetadata(const AudioPortConfig& portConfig) {
    RecordTrackMetadata trackMeta;
    trackMeta.source = AudioSource::MIC;
    trackMeta.gain = 1.0;
    trackMeta.channelMask = portConfig.channelMask.value();
    SinkMetadata metadata;
    metadata.tracks.push_back(trackMeta);
    return metadata;
}

template <>
ScopedAStatus WithStream<IStreamIn>::SetUpNoChecks(IModule* module,
                                                   const AudioPortConfig& portConfig,
                                                   long bufferSizeFrames) {
    aidl::android::hardware::audio::core::IModule::OpenInputStreamArguments args;
    args.portConfigId = portConfig.id;
    args.sinkMetadata = GenerateSinkMetadata(portConfig);
    args.bufferSizeFrames = bufferSizeFrames;
    auto callback = ndk::SharedRefBase::make<DefaultStreamCallback>();
    // TODO: Uncomment when support for asynchronous input is implemented.
    // args.callback = callback;
    aidl::android::hardware::audio::core::IModule::OpenInputStreamReturn ret;
    ScopedAStatus status = module->openInputStream(args, &ret);
    if (status.isOk()) {
        mStream = std::move(ret.stream);
        mDescriptor = std::move(ret.desc);
        mStreamCallback = std::move(callback);
    }
    return status;
}

SourceMetadata GenerateSourceMetadata(const AudioPortConfig& portConfig) {
    PlaybackTrackMetadata trackMeta;
    trackMeta.usage = AudioUsage::MEDIA;
    trackMeta.contentType = AudioContentType::MUSIC;
    trackMeta.gain = 1.0;
    trackMeta.channelMask = portConfig.channelMask.value();
    SourceMetadata metadata;
    metadata.tracks.push_back(trackMeta);
    return metadata;
}

template <>
ScopedAStatus WithStream<IStreamOut>::SetUpNoChecks(IModule* module,
                                                    const AudioPortConfig& portConfig,
                                                    long bufferSizeFrames) {
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamArguments args;
    args.portConfigId = portConfig.id;
    args.sourceMetadata = GenerateSourceMetadata(portConfig);
    args.offloadInfo = generateOffloadInfoIfNeeded(portConfig);
    args.bufferSizeFrames = bufferSizeFrames;
    auto callback = ndk::SharedRefBase::make<DefaultStreamCallback>();
    args.callback = callback;
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamReturn ret;
    ScopedAStatus status = module->openOutputStream(args, &ret);
    if (status.isOk()) {
        mStream = std::move(ret.stream);
        mDescriptor = std::move(ret.desc);
        mStreamCallback = std::move(callback);
    }
    return status;
}

class WithAudioPatch {
  public:
    WithAudioPatch() = default;
    WithAudioPatch(const AudioPortConfig& srcPortConfig, const AudioPortConfig& sinkPortConfig)
        : mSrcPortConfig(srcPortConfig), mSinkPortConfig(sinkPortConfig) {}
    WithAudioPatch(bool sinkIsCfg1, const AudioPortConfig& portConfig1,
                   const AudioPortConfig& portConfig2)
        : mSrcPortConfig(sinkIsCfg1 ? portConfig2 : portConfig1),
          mSinkPortConfig(sinkIsCfg1 ? portConfig1 : portConfig2) {}
    WithAudioPatch(const WithAudioPatch& patch, const AudioPortConfig& srcPortConfig,
                   const AudioPortConfig& sinkPortConfig)
        : mInitialPatch(patch.mPatch),
          mSrcPortConfig(srcPortConfig),
          mSinkPortConfig(sinkPortConfig),
          mModule(patch.mModule),
          mPatch(patch.mPatch) {}
    WithAudioPatch(const WithAudioPatch&) = delete;
    WithAudioPatch& operator=(const WithAudioPatch&) = delete;
    ~WithAudioPatch() {
        if (mModule != nullptr && mPatch.id != 0) {
            if (mInitialPatch.has_value()) {
                AudioPatch ignored;
                // This releases our port configs so that they can be reset.
                EXPECT_IS_OK(mModule->setAudioPatch(*mInitialPatch, &ignored))
                        << "patch id " << mInitialPatch->id;
            } else {
                EXPECT_IS_OK(mModule->resetAudioPatch(mPatch.id)) << "patch id " << getId();
            }
        }
    }
    void SetUpPortConfigs(IModule* module) {
        ASSERT_NO_FATAL_FAILURE(mSrcPortConfig.SetUp(module));
        ASSERT_NO_FATAL_FAILURE(mSinkPortConfig.SetUp(module));
    }
    ScopedAStatus SetUpNoChecks(IModule* module) {
        mModule = module;
        mPatch.sourcePortConfigIds = std::vector<int32_t>{mSrcPortConfig.getId()};
        mPatch.sinkPortConfigIds = std::vector<int32_t>{mSinkPortConfig.getId()};
        return mModule->setAudioPatch(mPatch, &mPatch);
    }
    void SetUp(IModule* module) {
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfigs(module));
        ASSERT_IS_OK(SetUpNoChecks(module)) << "source port config id " << mSrcPortConfig.getId()
                                            << "; sink port config id " << mSinkPortConfig.getId();
        EXPECT_GT(mPatch.minimumStreamBufferSizeFrames, 0) << "patch id " << getId();
        for (auto latencyMs : mPatch.latenciesMs) {
            EXPECT_GT(latencyMs, 0) << "patch id " << getId();
        }
    }
    void VerifyAgainstAllPatches(IModule* module) {
        std::vector<AudioPatch> allPatches;
        ASSERT_IS_OK(module->getAudioPatches(&allPatches));
        const auto& patchIt = findById(allPatches, getId());
        ASSERT_NE(patchIt, allPatches.end()) << "patch id " << getId();
        if (get() != *patchIt) {
            FAIL() << "Stored patch: " << get().toString() << " is not the same as returned "
                   << "by the HAL module: " << patchIt->toString();
        }
    }
    int32_t getId() const { return mPatch.id; }
    const AudioPatch& get() const { return mPatch; }
    int32_t getMinimumStreamBufferSizeFrames() const {
        return mPatch.minimumStreamBufferSizeFrames;
    }
    const AudioPortConfig& getSinkPortConfig() const { return mSinkPortConfig.get(); }
    const AudioPortConfig& getSrcPortConfig() const { return mSrcPortConfig.get(); }
    const AudioPortConfig& getPortConfig(bool getSink) const {
        return getSink ? getSinkPortConfig() : getSrcPortConfig();
    }

  private:
    std::optional<AudioPatch> mInitialPatch;
    WithAudioPortConfig mSrcPortConfig;
    WithAudioPortConfig mSinkPortConfig;
    IModule* mModule = nullptr;
    AudioPatch mPatch;
};

TEST_P(AudioCoreModule, Published) {
    // SetUp must complete with no failures.
}

TEST_P(AudioCoreModule, CanBeRestarted) {
    ASSERT_NO_FATAL_FAILURE(RestartService());
}

TEST_P(AudioCoreModule, PortIdsAreUnique) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
}

TEST_P(AudioCoreModule, GetAudioPortsIsStable) {
    std::vector<AudioPort> ports1;
    ASSERT_IS_OK(module->getAudioPorts(&ports1));
    std::vector<AudioPort> ports2;
    ASSERT_IS_OK(module->getAudioPorts(&ports2));
    EXPECT_NO_FATAL_FAILURE(VerifyVectorsAreEqual<AudioPort>(ports1, ports2))
            << "Audio port arrays do not match across consequent calls to getAudioPorts";
}

TEST_P(AudioCoreModule, GetAudioRoutesIsStable) {
    std::vector<AudioRoute> routes1;
    ASSERT_IS_OK(module->getAudioRoutes(&routes1));
    std::vector<AudioRoute> routes2;
    ASSERT_IS_OK(module->getAudioRoutes(&routes2));
    EXPECT_NO_FATAL_FAILURE(VerifyVectorsAreEqual<AudioRoute>(routes1, routes2))
            << " Audio route arrays do not match across consequent calls to getAudioRoutes";
}

TEST_P(AudioCoreModule, GetAudioRoutesAreValid) {
    std::vector<AudioRoute> routes;
    ASSERT_IS_OK(module->getAudioRoutes(&routes));
    for (const auto& route : routes) {
        std::set<int32_t> sources(route.sourcePortIds.begin(), route.sourcePortIds.end());
        EXPECT_NE(0UL, sources.size())
                << "empty audio port sinks in the audio route: " << route.toString();
        EXPECT_EQ(sources.size(), route.sourcePortIds.size())
                << "IDs of audio port sinks are not unique in the audio route: "
                << route.toString();
    }
}

TEST_P(AudioCoreModule, GetAudioRoutesPortIdsAreValid) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    std::vector<AudioRoute> routes;
    ASSERT_IS_OK(module->getAudioRoutes(&routes));
    for (const auto& route : routes) {
        EXPECT_EQ(1UL, portIds.count(route.sinkPortId))
                << route.sinkPortId << " sink port id is unknown";
        for (const auto& source : route.sourcePortIds) {
            EXPECT_EQ(1UL, portIds.count(source)) << source << " source port id is unknown";
        }
    }
}

TEST_P(AudioCoreModule, GetAudioRoutesForAudioPort) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    if (portIds.empty()) {
        GTEST_SKIP() << "No ports in the module.";
    }
    for (const auto portId : portIds) {
        std::vector<AudioRoute> routes;
        EXPECT_IS_OK(module->getAudioRoutesForAudioPort(portId, &routes));
        for (const auto& r : routes) {
            if (r.sinkPortId != portId) {
                const auto& srcs = r.sourcePortIds;
                EXPECT_TRUE(std::find(srcs.begin(), srcs.end(), portId) != srcs.end())
                        << " port ID " << portId << " does not used by the route " << r.toString();
            }
        }
    }
    for (const auto portId : GetNonExistentIds(portIds)) {
        std::vector<AudioRoute> routes;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->getAudioRoutesForAudioPort(portId, &routes))
                << "port ID " << portId;
    }
}

TEST_P(AudioCoreModule, CheckDevicePorts) {
    std::vector<AudioPort> ports;
    ASSERT_IS_OK(module->getAudioPorts(&ports));
    std::optional<int32_t> defaultOutput, defaultInput;
    std::set<AudioDevice> inputs, outputs;
    const int defaultDeviceFlag = 1 << AudioPortDeviceExt::FLAG_INDEX_DEFAULT_DEVICE;
    for (const auto& port : ports) {
        if (port.ext.getTag() != AudioPortExt::Tag::device) continue;
        const AudioPortDeviceExt& devicePort = port.ext.get<AudioPortExt::Tag::device>();
        EXPECT_NE(AudioDeviceType::NONE, devicePort.device.type.type);
        EXPECT_NE(AudioDeviceType::IN_DEFAULT, devicePort.device.type.type);
        EXPECT_NE(AudioDeviceType::OUT_DEFAULT, devicePort.device.type.type);
        if (devicePort.device.type.type > AudioDeviceType::IN_DEFAULT &&
            devicePort.device.type.type < AudioDeviceType::OUT_DEFAULT) {
            EXPECT_EQ(AudioIoFlags::Tag::input, port.flags.getTag());
        } else if (devicePort.device.type.type > AudioDeviceType::OUT_DEFAULT) {
            EXPECT_EQ(AudioIoFlags::Tag::output, port.flags.getTag());
        }
        EXPECT_FALSE((devicePort.flags & defaultDeviceFlag) != 0 &&
                     !devicePort.device.type.connection.empty())
                << "Device port " << port.id
                << " must be permanently attached to be set as default";
        if ((devicePort.flags & defaultDeviceFlag) != 0) {
            if (port.flags.getTag() == AudioIoFlags::Tag::output) {
                EXPECT_FALSE(defaultOutput.has_value())
                        << "At least two output device ports are declared as default: "
                        << defaultOutput.value() << " and " << port.id;
                defaultOutput = port.id;
                EXPECT_EQ(0UL, outputs.count(devicePort.device))
                        << "Non-unique output device: " << devicePort.device.toString();
                outputs.insert(devicePort.device);
            } else if (port.flags.getTag() == AudioIoFlags::Tag::input) {
                EXPECT_FALSE(defaultInput.has_value())
                        << "At least two input device ports are declared as default: "
                        << defaultInput.value() << " and " << port.id;
                defaultInput = port.id;
                EXPECT_EQ(0UL, inputs.count(devicePort.device))
                        << "Non-unique input device: " << devicePort.device.toString();
                inputs.insert(devicePort.device);
            } else {
                FAIL() << "Invalid AudioIoFlags Tag: " << toString(port.flags.getTag());
            }
        }
        // Speaker layout can be null or layoutMask variant.
        if (devicePort.speakerLayout.has_value()) {
            // Should only be set for output ports.
            EXPECT_EQ(AudioIoFlags::Tag::output, port.flags.getTag());
            const auto speakerLayoutTag = devicePort.speakerLayout.value().getTag();
            EXPECT_EQ(AudioChannelLayout::Tag::layoutMask, speakerLayoutTag)
                    << "If set, speaker layout must be layoutMask.  Received: "
                    << toString(speakerLayoutTag);
        }
    }
}

TEST_P(AudioCoreModule, CheckMixPorts) {
    std::vector<AudioPort> ports;
    ASSERT_IS_OK(module->getAudioPorts(&ports));
    std::optional<int32_t> primaryMixPort;
    for (const auto& port : ports) {
        if (port.ext.getTag() != AudioPortExt::Tag::mix) continue;
        const auto& mixPort = port.ext.get<AudioPortExt::Tag::mix>();
        if (port.flags.getTag() == AudioIoFlags::Tag::output &&
            isBitPositionFlagSet(port.flags.get<AudioIoFlags::Tag::output>(),
                                 AudioOutputFlags::PRIMARY)) {
            EXPECT_FALSE(primaryMixPort.has_value())
                    << "At least two mix ports have PRIMARY flag set: " << primaryMixPort.value()
                    << " and " << port.id;
            primaryMixPort = port.id;
            EXPECT_GE(mixPort.maxOpenStreamCount, 0)
                    << "Primary mix port " << port.id << " can not have maxOpenStreamCount "
                    << mixPort.maxOpenStreamCount;
        }
    }
}

TEST_P(AudioCoreModule, GetAudioPort) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    if (portIds.empty()) {
        GTEST_SKIP() << "No ports in the module.";
    }
    for (const auto portId : portIds) {
        AudioPort port;
        EXPECT_IS_OK(module->getAudioPort(portId, &port));
        EXPECT_EQ(portId, port.id);
    }
    for (const auto portId : GetNonExistentIds(portIds)) {
        AudioPort port;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->getAudioPort(portId, &port))
                << "port ID " << portId;
    }
}

TEST_P(AudioCoreModule, SetUpModuleConfig) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    // Send the module config to logcat to facilitate failures investigation.
    LOG(INFO) << "SetUpModuleConfig: " << moduleConfig->toString();
}

// Verify that HAL module reports for a connected device port at least one non-dynamic profile,
// that is, a profile with actual supported configuration.
// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, GetAudioPortWithExternalDevices) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : ports) {
        AudioPort portWithData = GenerateUniqueDeviceAddress(port);
        WithDevicePortConnectedState portConnected(portWithData);
        ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
        const int32_t connectedPortId = portConnected.getId();
        ASSERT_NE(portWithData.id, connectedPortId);
        ASSERT_EQ(portWithData.ext.getTag(), portConnected.get().ext.getTag());
        EXPECT_EQ(portWithData.ext.get<AudioPortExt::Tag::device>().device,
                  portConnected.get().ext.get<AudioPortExt::Tag::device>().device);
        // Verify that 'getAudioPort' and 'getAudioPorts' return the same connected port.
        AudioPort connectedPort;
        EXPECT_IS_OK(module->getAudioPort(connectedPortId, &connectedPort))
                << "port ID " << connectedPortId;
        EXPECT_EQ(portConnected.get(), connectedPort);
        const auto& portProfiles = connectedPort.profiles;
        if (portProfiles.empty()) {
            const auto routableMixPorts = moduleConfig->getRoutableMixPortsForDevicePort(
                    connectedPort, true /*connectedOnly*/);
            bool hasMixPortWithStaticProfile = false;
            for (const auto& mixPort : routableMixPorts) {
                const auto& mixPortProfiles = mixPort.profiles;
                if (!mixPortProfiles.empty() &&
                    !std::all_of(mixPortProfiles.begin(), mixPortProfiles.end(),
                                 [](const auto& profile) {
                                     return profile.format.type == AudioFormatType::DEFAULT;
                                 })) {
                    hasMixPortWithStaticProfile = true;
                    break;
                }
            }
            EXPECT_TRUE(hasMixPortWithStaticProfile)
                    << "Connected port has no profiles and no routable mix ports with profiles: "
                    << connectedPort.toString();
        }
        const auto dynamicProfileIt =
                std::find_if(portProfiles.begin(), portProfiles.end(), [](const auto& profile) {
                    return profile.format.type == AudioFormatType::DEFAULT;
                });
        EXPECT_EQ(portProfiles.end(), dynamicProfileIt) << "Connected port contains dynamic "
                                                        << "profiles: " << connectedPort.toString();

        std::vector<AudioPort> allPorts;
        ASSERT_IS_OK(module->getAudioPorts(&allPorts));
        const auto allPortsIt = findById(allPorts, connectedPortId);
        EXPECT_NE(allPorts.end(), allPortsIt);
        if (allPortsIt != allPorts.end()) {
            EXPECT_EQ(portConnected.get(), *allPortsIt);
        }
    }
}

TEST_P(AudioCoreModule, OpenStreamInvalidPortConfigId) {
    std::set<int32_t> portConfigIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortConfigIds(&portConfigIds));
    for (const auto portConfigId : GetNonExistentIds(portConfigIds)) {
        {
            aidl::android::hardware::audio::core::IModule::OpenInputStreamArguments args;
            args.portConfigId = portConfigId;
            args.bufferSizeFrames = kNegativeTestBufferSizeFrames;
            aidl::android::hardware::audio::core::IModule::OpenInputStreamReturn ret;
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->openInputStream(args, &ret))
                    << "port config ID " << portConfigId;
            EXPECT_EQ(nullptr, ret.stream);
        }
        {
            aidl::android::hardware::audio::core::IModule::OpenOutputStreamArguments args;
            args.portConfigId = portConfigId;
            args.bufferSizeFrames = kNegativeTestBufferSizeFrames;
            aidl::android::hardware::audio::core::IModule::OpenOutputStreamReturn ret;
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->openOutputStream(args, &ret))
                    << "port config ID " << portConfigId;
            EXPECT_EQ(nullptr, ret.stream);
        }
    }
}

TEST_P(AudioCoreModule, PortConfigIdsAreUnique) {
    std::set<int32_t> portConfigIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortConfigIds(&portConfigIds));
}

TEST_P(AudioCoreModule, PortConfigPortIdsAreValid) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    std::vector<AudioPortConfig> portConfigs;
    ASSERT_IS_OK(module->getAudioPortConfigs(&portConfigs));
    for (const auto& config : portConfigs) {
        EXPECT_EQ(1UL, portIds.count(config.portId))
                << config.portId << " port id is unknown, config id " << config.id;
    }
}

TEST_P(AudioCoreModule, ResetAudioPortConfigInvalidId) {
    std::set<int32_t> portConfigIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortConfigIds(&portConfigIds));
    for (const auto portConfigId : GetNonExistentIds(portConfigIds)) {
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->resetAudioPortConfig(portConfigId))
                << "port config ID " << portConfigId;
    }
}

// Verify that for the audio port configs provided by the HAL after init, resetting
// the config does not delete it, but brings it back to the initial config.
TEST_P(AudioCoreModule, ResetAudioPortConfigToInitialValue) {
    std::vector<AudioPortConfig> portConfigsBefore;
    ASSERT_IS_OK(module->getAudioPortConfigs(&portConfigsBefore));
    // TODO: Change port configs according to port profiles.
    for (const auto& c : portConfigsBefore) {
        EXPECT_IS_OK(module->resetAudioPortConfig(c.id)) << "port config ID " << c.id;
    }
    std::vector<AudioPortConfig> portConfigsAfter;
    ASSERT_IS_OK(module->getAudioPortConfigs(&portConfigsAfter));
    for (const auto& c : portConfigsBefore) {
        auto afterIt = findById<AudioPortConfig>(portConfigsAfter, c.id);
        EXPECT_NE(portConfigsAfter.end(), afterIt)
                << " port config ID " << c.id << " was removed by reset";
        if (afterIt != portConfigsAfter.end()) {
            EXPECT_TRUE(c == *afterIt)
                    << "Expected: " << c.toString() << "; Actual: " << afterIt->toString();
        }
    }
}

TEST_P(AudioCoreModule, SetAudioPortConfigSuggestedConfig) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    auto srcMixPort = moduleConfig->getSourceMixPortForConnectedDevice();
    if (!srcMixPort.has_value()) {
        GTEST_SKIP() << "No mix port for attached output devices";
    }
    AudioPortConfig portConfig;
    AudioPortConfig suggestedConfig;
    portConfig.portId = srcMixPort.value().id;
    const int32_t kIoHandle = 42;
    portConfig.ext = AudioPortMixExt{.handle = kIoHandle};
    {
        bool applied = true;
        ASSERT_IS_OK(module->setAudioPortConfig(portConfig, &suggestedConfig, &applied))
                << "Config: " << portConfig.toString();
        EXPECT_FALSE(applied);
    }
    EXPECT_EQ(0, suggestedConfig.id);
    EXPECT_TRUE(suggestedConfig.sampleRate.has_value());
    EXPECT_TRUE(suggestedConfig.channelMask.has_value());
    EXPECT_TRUE(suggestedConfig.format.has_value());
    EXPECT_TRUE(suggestedConfig.flags.has_value());
    ASSERT_EQ(AudioPortExt::Tag::mix, suggestedConfig.ext.getTag());
    EXPECT_EQ(kIoHandle, suggestedConfig.ext.get<AudioPortExt::Tag::mix>().handle);
    WithAudioPortConfig applied(suggestedConfig);
    ASSERT_NO_FATAL_FAILURE(applied.SetUp(module.get()));
    const AudioPortConfig& appliedConfig = applied.get();
    EXPECT_NE(0, appliedConfig.id);
    ASSERT_TRUE(appliedConfig.sampleRate.has_value());
    EXPECT_EQ(suggestedConfig.sampleRate.value(), appliedConfig.sampleRate.value());
    ASSERT_TRUE(appliedConfig.channelMask.has_value());
    EXPECT_EQ(suggestedConfig.channelMask.value(), appliedConfig.channelMask.value());
    ASSERT_TRUE(appliedConfig.format.has_value());
    EXPECT_EQ(suggestedConfig.format.value(), appliedConfig.format.value());
    ASSERT_TRUE(appliedConfig.flags.has_value());
    EXPECT_EQ(suggestedConfig.flags.value(), appliedConfig.flags.value());
    ASSERT_EQ(AudioPortExt::Tag::mix, appliedConfig.ext.getTag());
    EXPECT_EQ(kIoHandle, appliedConfig.ext.get<AudioPortExt::Tag::mix>().handle);
}

TEST_P(AudioCoreModule, SetAllAttachedDevicePortConfigs) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    ASSERT_NO_FATAL_FAILURE(ApplyEveryConfig(moduleConfig->getPortConfigsForAttachedDevicePorts()));
}

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, SetAllExternalDevicePortConfigs) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : ports) {
        WithDevicePortConnectedState portConnected(GenerateUniqueDeviceAddress(port));
        ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
        ASSERT_NO_FATAL_FAILURE(
                ApplyEveryConfig(moduleConfig->getPortConfigsForDevicePort(portConnected.get())));
    }
}

TEST_P(AudioCoreModule, SetAllStaticAudioPortConfigs) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    ASSERT_NO_FATAL_FAILURE(ApplyEveryConfig(moduleConfig->getPortConfigsForMixPorts()));
}

TEST_P(AudioCoreModule, SetAudioPortConfigInvalidPortId) {
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    for (const auto portId : GetNonExistentIds(portIds)) {
        AudioPortConfig portConfig, suggestedConfig;
        bool applied;
        portConfig.portId = portId;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->setAudioPortConfig(portConfig, &suggestedConfig, &applied))
                << "port ID " << portId;
        EXPECT_FALSE(suggestedConfig.format.has_value());
        EXPECT_FALSE(suggestedConfig.channelMask.has_value());
        EXPECT_FALSE(suggestedConfig.sampleRate.has_value());
    }
}

TEST_P(AudioCoreModule, SetAudioPortConfigInvalidPortConfigId) {
    std::set<int32_t> portConfigIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortConfigIds(&portConfigIds));
    for (const auto portConfigId : GetNonExistentIds(portConfigIds)) {
        AudioPortConfig portConfig, suggestedConfig;
        bool applied;
        portConfig.id = portConfigId;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->setAudioPortConfig(portConfig, &suggestedConfig, &applied))
                << "port config ID " << portConfigId;
        EXPECT_FALSE(suggestedConfig.format.has_value());
        EXPECT_FALSE(suggestedConfig.channelMask.has_value());
        EXPECT_FALSE(suggestedConfig.sampleRate.has_value());
    }
}

TEST_P(AudioCoreModule, SetAudioPortConfigInvalidPortAudioGain) {
    ASSERT_GE(aidlVersion, kAidlVersion1);
    if (aidlVersion < kAidlVersion3) {
        GTEST_SKIP() << "Skip for audio HAL version lower than " << kAidlVersion3;
    }
    std::vector<AudioPort> ports;
    ASSERT_IS_OK(module->getAudioPorts(&ports));
    bool atLeastOnePortWithNonemptyGain = false;
    for (const auto port : ports) {
        AudioPortConfig portConfig;
        portConfig.portId = port.id;
        if (port.gains.empty()) {
            continue;
        }
        atLeastOnePortWithNonemptyGain = true;
        int index = 0;
        ASSERT_NE(0, port.gains[index].stepValue) << "Invalid audio port config gain step 0";
        portConfig.gain->index = index;
        AudioGainConfig invalidGainConfig;

        int invalidGain = port.gains[index].maxValue + port.gains[index].stepValue;
        invalidGainConfig.values.push_back(invalidGain);
        portConfig.gain.emplace(invalidGainConfig);
        bool applied = true;
        AudioPortConfig suggestedConfig;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->setAudioPortConfig(portConfig, &suggestedConfig, &applied))
                << "invalid port gain " << invalidGain << " lower than min gain";

        invalidGain = port.gains[index].minValue - port.gains[index].stepValue;
        invalidGainConfig.values[0] = invalidGain;
        portConfig.gain.emplace(invalidGainConfig);
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->setAudioPortConfig(portConfig, &suggestedConfig, &applied))
                << "invalid port gain " << invalidGain << "higher than max gain";
    }
    if (!atLeastOnePortWithNonemptyGain) {
        GTEST_SKIP() << "No audio port contains non-empty gain configuration";
    }
}

TEST_P(AudioCoreModule, TryConnectMissingDevice) {
    // Limit checks to connection types that are known to be detectable by HAL implementations.
    static const std::set<std::string> kCheckedConnectionTypes{
            AudioDeviceDescription::CONNECTION_HDMI, AudioDeviceDescription::CONNECTION_HDMI_ARC,
            AudioDeviceDescription::CONNECTION_HDMI_EARC, AudioDeviceDescription::CONNECTION_IP_V4,
            AudioDeviceDescription::CONNECTION_USB};
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    WithDebugFlags doNotSimulateConnections = WithDebugFlags::createNested(*debug);
    doNotSimulateConnections.flags().simulateDeviceConnections = false;
    ASSERT_NO_FATAL_FAILURE(doNotSimulateConnections.SetUp(module.get()));
    bool hasAtLeastOneCheckedConnection = false;
    for (const auto& port : ports) {
        if (kCheckedConnectionTypes.count(
                    port.ext.get<AudioPortExt::device>().device.type.connection) == 0) {
            continue;
        }
        AudioPort portWithData = GenerateUniqueDeviceAddress(port), connectedPort;
        ScopedAStatus status = module->connectExternalDevice(portWithData, &connectedPort);
        EXPECT_STATUS(EX_ILLEGAL_STATE, status) << "static port " << portWithData.toString();
        if (status.isOk()) {
            EXPECT_IS_OK_OR_UNKNOWN_TRANSACTION(
                    module->prepareToDisconnectExternalDevice(connectedPort.id))
                    << "when preparing to disconnect device port ID " << connectedPort.id;
            EXPECT_IS_OK(module->disconnectExternalDevice(connectedPort.id))
                    << "when disconnecting device port ID " << connectedPort.id;
        }
        hasAtLeastOneCheckedConnection = true;
    }
    if (!hasAtLeastOneCheckedConnection) {
        GTEST_SKIP() << "No external devices with connection types that can be checked.";
    }
}

TEST_P(AudioCoreModule, TryChangingConnectionSimulationMidway) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    WithDevicePortConnectedState portConnected(GenerateUniqueDeviceAddress(*ports.begin()));
    ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
    ModuleDebug midwayDebugChange = debug->flags();
    midwayDebugChange.simulateDeviceConnections = false;
    EXPECT_STATUS(EX_ILLEGAL_STATE, module->setModuleDebug(midwayDebugChange))
            << "when trying to disable connections simulation while having a connected device";
}

TEST_P(AudioCoreModule, ConnectDisconnectExternalDeviceInvalidPorts) {
    AudioPort ignored;
    std::set<int32_t> portIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPortIds(&portIds));
    for (const auto portId : GetNonExistentIds(portIds)) {
        AudioPort invalidPort;
        invalidPort.id = portId;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->connectExternalDevice(invalidPort, &ignored))
                << "port ID " << portId << ", when setting CONNECTED state";
        EXPECT_STATUS_OR_UNKNOWN_TRANSACTION(EX_ILLEGAL_ARGUMENT,
                                             module->prepareToDisconnectExternalDevice(portId))
                << "port ID " << portId << ", when preparing to disconnect";
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->disconnectExternalDevice(portId))
                << "port ID " << portId << ", when setting DISCONNECTED state";
    }

    std::vector<AudioPort> ports;
    ASSERT_IS_OK(module->getAudioPorts(&ports));
    for (const auto& port : ports) {
        if (port.ext.getTag() != AudioPortExt::Tag::device) {
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->connectExternalDevice(port, &ignored))
                    << "non-device port ID " << port.id << " when setting CONNECTED state";
            EXPECT_STATUS_OR_UNKNOWN_TRANSACTION(EX_ILLEGAL_ARGUMENT,
                                                 module->prepareToDisconnectExternalDevice(port.id))
                    << "non-device port ID " << port.id << " when preparing to disconnect";
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->disconnectExternalDevice(port.id))
                    << "non-device port ID " << port.id << " when setting DISCONNECTED state";
        } else {
            const auto& devicePort = port.ext.get<AudioPortExt::Tag::device>();
            if (devicePort.device.type.connection.empty()) {
                EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->connectExternalDevice(port, &ignored))
                        << "for a permanently attached device port ID " << port.id
                        << " when setting CONNECTED state";
                EXPECT_STATUS_OR_UNKNOWN_TRANSACTION(
                        EX_ILLEGAL_ARGUMENT, module->prepareToDisconnectExternalDevice(port.id))
                        << "for a permanently attached device port ID " << port.id
                        << " when preparing to disconnect";
                EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->disconnectExternalDevice(port.id))
                        << "for a permanently attached device port ID " << port.id
                        << " when setting DISCONNECTED state";
            }
        }
    }
}

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, ConnectDisconnectExternalDeviceTwice) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    AudioPort ignored;
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : ports) {
        EXPECT_STATUS_OR_UNKNOWN_TRANSACTION(EX_ILLEGAL_ARGUMENT,
                                             module->prepareToDisconnectExternalDevice(port.id))
                << "when preparing to disconnect already disconnected device port ID " << port.id;
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->disconnectExternalDevice(port.id))
                << "when disconnecting already disconnected device port ID " << port.id;
        AudioPort portWithData = GenerateUniqueDeviceAddress(port);
        WithDevicePortConnectedState portConnected(portWithData);
        ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->connectExternalDevice(portConnected.get(), &ignored))
                << "when trying to connect a connected device port "
                << portConnected.get().toString();
        EXPECT_STATUS(EX_ILLEGAL_STATE, module->connectExternalDevice(portWithData, &ignored))
                << "when connecting again the external device "
                << portWithData.ext.get<AudioPortExt::Tag::device>().device.toString()
                << "; Returned connected port " << ignored.toString() << " for template "
                << portWithData.toString();
    }
}

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, DisconnectExternalDeviceNonResetPortConfig) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : ports) {
        WithDevicePortConnectedState portConnected(GenerateUniqueDeviceAddress(port));
        ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
        const auto portConfig = moduleConfig->getSingleConfigForDevicePort(portConnected.get());
        {
            WithAudioPortConfig config(portConfig);
            // Note: if SetUp fails, check the status of 'GetAudioPortWithExternalDevices' test.
            // Our test assumes that 'getAudioPort' returns at least one profile, and it
            // is not a dynamic profile.
            ASSERT_NO_FATAL_FAILURE(config.SetUp(module.get()));
            EXPECT_IS_OK_OR_UNKNOWN_TRANSACTION(
                    module->prepareToDisconnectExternalDevice(portConnected.getId()))
                    << "when preparing to disconnect device port ID " << port.id
                    << " with active configuration " << config.getId();
            EXPECT_STATUS(EX_ILLEGAL_STATE, module->disconnectExternalDevice(portConnected.getId()))
                    << "when trying to disconnect device port ID " << port.id
                    << " with active configuration " << config.getId();
        }
    }
}

TEST_P(AudioCoreModule, ExternalDevicePortRoutes) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> ports = moduleConfig->getExternalDevicePorts();
    if (ports.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : ports) {
        std::vector<AudioRoute> routesBefore;
        ASSERT_IS_OK(module->getAudioRoutes(&routesBefore));

        int32_t connectedPortId;
        {
            WithDevicePortConnectedState portConnected(GenerateUniqueDeviceAddress(port));
            ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
            connectedPortId = portConnected.getId();
            std::vector<AudioRoute> connectedPortRoutes;
            ASSERT_IS_OK(module->getAudioRoutesForAudioPort(connectedPortId, &connectedPortRoutes))
                    << "when retrieving routes for connected port id " << connectedPortId;
            // There must be routes for the port to be useful.
            if (connectedPortRoutes.empty()) {
                std::vector<AudioRoute> allRoutes;
                ASSERT_IS_OK(module->getAudioRoutes(&allRoutes));
                ADD_FAILURE() << " no routes returned for the connected port "
                              << portConnected.get().toString()
                              << "; all routes: " << android::internal::ToString(allRoutes);
            }
        }
        std::vector<AudioRoute> ignored;
        ASSERT_STATUS(EX_ILLEGAL_ARGUMENT,
                      module->getAudioRoutesForAudioPort(connectedPortId, &ignored))
                << "when retrieving routes for released connected port id " << connectedPortId;

        std::vector<AudioRoute> routesAfter;
        ASSERT_IS_OK(module->getAudioRoutes(&routesAfter));
        ASSERT_EQ(routesBefore.size(), routesAfter.size())
                << "Sizes of audio route arrays do not match after creating and "
                << "releasing a connected port";
        std::sort(routesBefore.begin(), routesBefore.end());
        std::sort(routesAfter.begin(), routesAfter.end());
        EXPECT_EQ(routesBefore, routesAfter);
    }
}

class RoutedPortsProfilesSnapshot {
  public:
    explicit RoutedPortsProfilesSnapshot(int32_t portId) : mPortId(portId) {}
    void Capture(IModule* module) {
        std::vector<AudioRoute> routes;
        ASSERT_IS_OK(module->getAudioRoutesForAudioPort(mPortId, &routes));
        std::vector<AudioPort> allPorts;
        ASSERT_IS_OK(module->getAudioPorts(&allPorts));
        ASSERT_NO_FATAL_FAILURE(GetAllRoutedPorts(routes, allPorts));
        ASSERT_NO_FATAL_FAILURE(GetProfileSizes());
    }
    void VerifyNoProfilesChanges(const RoutedPortsProfilesSnapshot& before) {
        for (const auto& p : before.mRoutedPorts) {
            auto beforeIt = before.mPortProfileSizes.find(p.id);
            ASSERT_NE(beforeIt, before.mPortProfileSizes.end())
                    << "port ID " << p.id << " not found in the initial profile sizes";
            EXPECT_EQ(beforeIt->second, mPortProfileSizes[p.id])
                    << " port " << p.toString() << " has an unexpected profile size change"
                    << " following an external device connection and disconnection";
        }
    }
    void VerifyProfilesNonEmpty() {
        for (const auto& p : mRoutedPorts) {
            EXPECT_NE(0UL, mPortProfileSizes[p.id])
                    << " port " << p.toString() << " must have had its profiles"
                    << " populated while having a connected external device";
        }
    }

    const std::vector<AudioPort>& getRoutedPorts() const { return mRoutedPorts; }

  private:
    void GetAllRoutedPorts(const std::vector<AudioRoute>& routes,
                           std::vector<AudioPort>& allPorts) {
        for (const auto& r : routes) {
            if (r.sinkPortId == mPortId) {
                for (const auto& srcPortId : r.sourcePortIds) {
                    const auto srcPortIt = findById(allPorts, srcPortId);
                    ASSERT_NE(allPorts.end(), srcPortIt) << "port ID " << srcPortId;
                    mRoutedPorts.push_back(*srcPortIt);
                }
            } else {
                const auto sinkPortIt = findById(allPorts, r.sinkPortId);
                ASSERT_NE(allPorts.end(), sinkPortIt) << "port ID " << r.sinkPortId;
                mRoutedPorts.push_back(*sinkPortIt);
            }
        }
    }
    void GetProfileSizes() {
        std::transform(
                mRoutedPorts.begin(), mRoutedPorts.end(),
                std::inserter(mPortProfileSizes, mPortProfileSizes.end()),
                [](const auto& port) { return std::make_pair(port.id, port.profiles.size()); });
    }

    const int32_t mPortId;
    std::vector<AudioPort> mRoutedPorts;
    std::map<int32_t, size_t> mPortProfileSizes;
};

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, ExternalDeviceMixPortConfigs) {
    // After an external device has been connected, all mix ports that can be routed
    // to the device port for the connected device must have non-empty profiles.
    // Since the test connects and disconnects a single device each time, the size
    // of profiles for all mix ports routed to the device port under test must get back
    // to the original count once the external device is disconnected.
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> externalDevicePorts = moduleConfig->getExternalDevicePorts();
    if (externalDevicePorts.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : externalDevicePorts) {
        SCOPED_TRACE(port.toString());
        RoutedPortsProfilesSnapshot before(port.id);
        ASSERT_NO_FATAL_FAILURE(before.Capture(module.get()));
        if (before.getRoutedPorts().empty()) continue;
        {
            WithDevicePortConnectedState portConnected(GenerateUniqueDeviceAddress(port));
            ASSERT_NO_FATAL_FAILURE(portConnected.SetUp(module.get(), moduleConfig.get()));
            RoutedPortsProfilesSnapshot connected(portConnected.getId());
            ASSERT_NO_FATAL_FAILURE(connected.Capture(module.get()));
            EXPECT_NO_FATAL_FAILURE(connected.VerifyProfilesNonEmpty());
        }
        RoutedPortsProfilesSnapshot after(port.id);
        ASSERT_NO_FATAL_FAILURE(after.Capture(module.get()));
        EXPECT_NO_FATAL_FAILURE(after.VerifyNoProfilesChanges(before));
    }
}

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, TwoExternalDevicesMixPortConfigsNested) {
    // Ensure that in the case when two external devices are connected to the same
    // device port, disconnecting one of them does not erase the profiles of routed mix ports.
    // In this scenario, the connections are "nested."
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> externalDevicePorts = moduleConfig->getExternalDevicePorts();
    if (externalDevicePorts.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : externalDevicePorts) {
        SCOPED_TRACE(port.toString());
        WithDevicePortConnectedState portConnected1(GenerateUniqueDeviceAddress(port));
        ASSERT_NO_FATAL_FAILURE(portConnected1.SetUp(module.get(), moduleConfig.get()));
        {
            // Connect and disconnect another device, if possible. It might not be possible
            // for point-to-point connections, like analog or SPDIF.
            WithDevicePortConnectedState portConnected2(GenerateUniqueDeviceAddress(port));
            if (auto status = portConnected2.SetUpNoChecks(module.get(), moduleConfig.get());
                !status.isOk()) {
                continue;
            }
        }
        RoutedPortsProfilesSnapshot connected(portConnected1.getId());
        ASSERT_NO_FATAL_FAILURE(connected.Capture(module.get()));
        EXPECT_NO_FATAL_FAILURE(connected.VerifyProfilesNonEmpty());
    }
}

// Note: This test relies on simulation of external device connections by the HAL module.
TEST_P(AudioCoreModule, TwoExternalDevicesMixPortConfigsInterleaved) {
    // Ensure that in the case when two external devices are connected to the same
    // device port, disconnecting one of them does not erase the profiles of routed mix ports.
    // In this scenario, the connections are "interleaved."
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    std::vector<AudioPort> externalDevicePorts = moduleConfig->getExternalDevicePorts();
    if (externalDevicePorts.empty()) {
        GTEST_SKIP() << "No external devices in the module.";
    }
    for (const auto& port : externalDevicePorts) {
        SCOPED_TRACE(port.toString());
        auto portConnected1 =
                std::make_unique<WithDevicePortConnectedState>(GenerateUniqueDeviceAddress(port));
        ASSERT_NO_FATAL_FAILURE(portConnected1->SetUp(module.get(), moduleConfig.get()));
        WithDevicePortConnectedState portConnected2(GenerateUniqueDeviceAddress(port));
        // Connect another device, if possible. It might not be possible for point-to-point
        // connections, like analog or SPDIF.
        if (auto status = portConnected2.SetUpNoChecks(module.get(), moduleConfig.get());
            !status.isOk()) {
            continue;
        }
        portConnected1.reset();
        RoutedPortsProfilesSnapshot connected(portConnected2.getId());
        ASSERT_NO_FATAL_FAILURE(connected.Capture(module.get()));
        EXPECT_NO_FATAL_FAILURE(connected.VerifyProfilesNonEmpty());
    }
}

TEST_P(AudioCoreModule, MasterMute) {
    bool isSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestAccessors<bool>(module.get(), &IModule::getMasterMute,
                                                &IModule::setMasterMute, {false, true}, {},
                                                &isSupported));
    if (!isSupported) {
        GTEST_SKIP() << "Master mute is not supported";
    }
    // TODO: Test that master mute actually mutes output.
}

TEST_P(AudioCoreModule, MasterVolume) {
    bool isSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestAccessors<float>(
            module.get(), &IModule::getMasterVolume, &IModule::setMasterVolume, {0.0f, 0.5f, 1.0f},
            {-0.1, 1.1, NAN, INFINITY, -INFINITY, 1 + std::numeric_limits<float>::epsilon()},
            &isSupported));
    if (!isSupported) {
        GTEST_SKIP() << "Master volume is not supported";
    }
    // TODO: Test that master volume actually attenuates output.
}

TEST_P(AudioCoreModule, MicMute) {
    bool isSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestAccessors<bool>(module.get(), &IModule::getMicMute,
                                                &IModule::setMicMute, {false, true}, {},
                                                &isSupported));
    if (!isSupported) {
        GTEST_SKIP() << "Mic mute is not supported";
    }
    // TODO: Test that mic mute actually mutes input.
}

TEST_P(AudioCoreModule, GetMicrophones) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    const std::vector<AudioPort> builtInMicPorts = moduleConfig->getAttachedMicrophonePorts();
    std::vector<MicrophoneInfo> micInfos;
    ScopedAStatus status = module->getMicrophones(&micInfos);
    if (!status.isOk()) {
        EXPECT_EQ(EX_UNSUPPORTED_OPERATION, status.getExceptionCode());
        ASSERT_FALSE(builtInMicPorts.empty())
                << "When the HAL module does not have built-in microphones, IModule.getMicrophones"
                << " must complete with no error and return an empty list";
        GTEST_SKIP() << "Microphone info is not supported";
    }
    std::set<int32_t> micPortIdsWithInfo;
    for (const auto& micInfo : micInfos) {
        const auto& micDevice = micInfo.device;
        const auto it =
                std::find_if(builtInMicPorts.begin(), builtInMicPorts.end(), [&](const auto& port) {
                    return port.ext.template get<AudioPortExt::Tag::device>().device == micDevice;
                });
        if (it != builtInMicPorts.end()) {
            micPortIdsWithInfo.insert(it->id);
        } else {
            ADD_FAILURE() << "No device port found with a device specified for the microphone \""
                          << micInfo.id << "\": " << micDevice.toString();
        }
    }
    if (micPortIdsWithInfo.size() != builtInMicPorts.size()) {
        std::vector<AudioPort> micPortsNoInfo;
        std::copy_if(builtInMicPorts.begin(), builtInMicPorts.end(),
                     std::back_inserter(micPortsNoInfo),
                     [&](const auto& port) { return micPortIdsWithInfo.count(port.id) == 0; });
        ADD_FAILURE() << "No MicrophoneInfo is provided for the following microphone device ports: "
                      << ::android::internal::ToString(micPortsNoInfo);
    }
}

TEST_P(AudioCoreModule, UpdateAudioMode) {
    for (const auto mode : ::ndk::enum_range<AudioMode>()) {
        if (isValidAudioMode(mode)) {
            EXPECT_IS_OK(module->updateAudioMode(mode)) << toString(mode);
        } else {
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->updateAudioMode(mode)) << toString(mode);
        }
    }
    EXPECT_IS_OK(module->updateAudioMode(AudioMode::NORMAL));
}

TEST_P(AudioCoreModule, UpdateScreenRotation) {
    for (const auto rotation : ::ndk::enum_range<IModule::ScreenRotation>()) {
        EXPECT_IS_OK(module->updateScreenRotation(rotation)) << toString(rotation);
    }
    EXPECT_IS_OK(module->updateScreenRotation(IModule::ScreenRotation::DEG_0));
}

TEST_P(AudioCoreModule, UpdateScreenState) {
    EXPECT_IS_OK(module->updateScreenState(false));
    EXPECT_IS_OK(module->updateScreenState(true));
}

TEST_P(AudioCoreModule, GenerateHwAvSyncId) {
    const auto kStatuses = {EX_NONE, EX_ILLEGAL_STATE};
    int32_t id1;
    ndk::ScopedAStatus status = module->generateHwAvSyncId(&id1);
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "HW AV Sync is not supported";
    }
    EXPECT_STATUS(kStatuses, status);
    if (status.isOk()) {
        int32_t id2;
        ASSERT_IS_OK(module->generateHwAvSyncId(&id2));
        EXPECT_NE(id1, id2) << "HW AV Sync IDs must be unique";
    }
}

TEST_P(AudioCoreModule, GetVendorParameters) {
    bool isGetterSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestGetVendorParameters(module.get(), &isGetterSupported));
    ndk::ScopedAStatus status = module->setVendorParameters({}, false);
    EXPECT_EQ(isGetterSupported, status.getExceptionCode() != EX_UNSUPPORTED_OPERATION)
            << "Support for getting and setting of vendor parameters must be consistent";
    if (!isGetterSupported) {
        GTEST_SKIP() << "Vendor parameters are not supported";
    }
}

TEST_P(AudioCoreModule, SetVendorParameters) {
    bool isSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestSetVendorParameters(module.get(), &isSupported));
    if (!isSupported) {
        GTEST_SKIP() << "Vendor parameters are not supported";
    }
}

// See b/262930731. In the absence of offloaded effect implementations,
// currently we can only pass a nullptr, and the HAL module must either reject
// it as an invalid argument, or say that offloaded effects are not supported.
TEST_P(AudioCoreModule, AddRemoveEffectInvalidArguments) {
    ndk::ScopedAStatus addEffectStatus = module->addDeviceEffect(-1, nullptr);
    ndk::ScopedAStatus removeEffectStatus = module->removeDeviceEffect(-1, nullptr);
    if (addEffectStatus.getExceptionCode() != EX_UNSUPPORTED_OPERATION) {
        EXPECT_EQ(EX_ILLEGAL_ARGUMENT, addEffectStatus.getExceptionCode());
        EXPECT_EQ(EX_ILLEGAL_ARGUMENT, removeEffectStatus.getExceptionCode());
    } else if (removeEffectStatus.getExceptionCode() != EX_UNSUPPORTED_OPERATION) {
        GTEST_FAIL() << "addDeviceEffect and removeDeviceEffect must be either supported or "
                     << "not supported together";
    } else {
        GTEST_SKIP() << "Offloaded effects not supported";
    }
    // Test rejection of a nullptr effect with a valid device port Id.
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    const auto configs = moduleConfig->getPortConfigsForAttachedDevicePorts();
    for (const auto& config : configs) {
        WithAudioPortConfig portConfig(config);
        ASSERT_NO_FATAL_FAILURE(portConfig.SetUp(module.get()));
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->addDeviceEffect(portConfig.getId(), nullptr));
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->removeDeviceEffect(portConfig.getId(), nullptr));
    }
}

TEST_P(AudioCoreModule, GetMmapPolicyInfos) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    const bool isMmapSupported = moduleConfig->isMmapSupported();
    for (const auto mmapPolicyType :
         {AudioMMapPolicyType::DEFAULT, AudioMMapPolicyType::EXCLUSIVE}) {
        std::vector<AudioMMapPolicyInfo> policyInfos;
        EXPECT_IS_OK(module->getMmapPolicyInfos(mmapPolicyType, &policyInfos))
                << toString(mmapPolicyType);
        const bool isMMapSupportedByPolicyInfos =
                std::find_if(policyInfos.begin(), policyInfos.end(), [](const auto& info) {
                    return info.mmapPolicy == AudioMMapPolicy::AUTO ||
                           info.mmapPolicy == AudioMMapPolicy::ALWAYS;
                }) != policyInfos.end();
        EXPECT_EQ(isMmapSupported, isMMapSupportedByPolicyInfos)
                << ::android::internal::ToString(policyInfos);
    }
}

TEST_P(AudioCoreModule, BluetoothVariableLatency) {
    bool isSupported = false;
    EXPECT_IS_OK(module->supportsVariableLatency(&isSupported));
    LOG(INFO) << "supportsVariableLatency: " << isSupported;
}

TEST_P(AudioCoreModule, GetAAudioMixerBurstCount) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    const bool isMmapSupported = moduleConfig->isMmapSupported();
    int32_t mixerBursts = 0;
    ndk::ScopedAStatus status = module->getAAudioMixerBurstCount(&mixerBursts);
    EXPECT_EQ(isMmapSupported, status.getExceptionCode() != EX_UNSUPPORTED_OPERATION)
            << "Support for AAudio MMAP and getting AAudio mixer burst count must be consistent";
    if (!isMmapSupported) {
        GTEST_SKIP() << "AAudio MMAP is not supported";
    }
    EXPECT_GE(mixerBursts, 0);
}

TEST_P(AudioCoreModule, GetAAudioHardwareBurstMinUsec) {
    ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    const bool isMmapSupported = moduleConfig->isMmapSupported();
    int32_t aaudioHardwareBurstMinUsec = 0;
    ndk::ScopedAStatus status = module->getAAudioHardwareBurstMinUsec(&aaudioHardwareBurstMinUsec);
    EXPECT_EQ(isMmapSupported, status.getExceptionCode() != EX_UNSUPPORTED_OPERATION)
            << "Support for AAudio MMAP and getting AAudio hardware burst minimum usec "
            << "must be consistent";
    if (!isMmapSupported) {
        GTEST_SKIP() << "AAudio MMAP is not supported";
    }
    EXPECT_GE(aaudioHardwareBurstMinUsec, 0);
}

class AudioCoreBluetooth : public AudioCoreModuleBase, public testing::TestWithParam<std::string> {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam()));
        ASSERT_IS_OK(module->getBluetooth(&bluetooth));
    }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }

    std::shared_ptr<IBluetooth> bluetooth;
};

TEST_P(AudioCoreBluetooth, SameInstance) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "Bluetooth is not supported";
    }
    std::shared_ptr<IBluetooth> bluetooth2;
    EXPECT_IS_OK(module->getBluetooth(&bluetooth2));
    ASSERT_NE(nullptr, bluetooth2.get());
    EXPECT_EQ(bluetooth->asBinder(), bluetooth2->asBinder())
            << "getBluetooth must return the same interface instance across invocations";
}

TEST_P(AudioCoreBluetooth, ScoConfig) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "Bluetooth is not supported";
    }
    ndk::ScopedAStatus status;
    IBluetooth::ScoConfig scoConfig;
    ASSERT_STATUS(kStatuses, status = bluetooth->setScoConfig({}, &scoConfig));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "BT SCO is not supported";
    }
    EXPECT_TRUE(scoConfig.isEnabled.has_value());
    EXPECT_TRUE(scoConfig.isNrecEnabled.has_value());
    EXPECT_NE(IBluetooth::ScoConfig::Mode::UNSPECIFIED, scoConfig.mode);
    IBluetooth::ScoConfig scoConfig2;
    ASSERT_IS_OK(bluetooth->setScoConfig(scoConfig, &scoConfig2));
    EXPECT_EQ(scoConfig, scoConfig2);
}

TEST_P(AudioCoreBluetooth, HfpConfig) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "Bluetooth is not supported";
    }
    ndk::ScopedAStatus status;
    IBluetooth::HfpConfig hfpConfig;
    ASSERT_STATUS(kStatuses, status = bluetooth->setHfpConfig({}, &hfpConfig));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "BT HFP is not supported";
    }
    EXPECT_TRUE(hfpConfig.isEnabled.has_value());
    EXPECT_TRUE(hfpConfig.sampleRate.has_value());
    EXPECT_TRUE(hfpConfig.volume.has_value());
    IBluetooth::HfpConfig hfpConfig2;
    ASSERT_IS_OK(bluetooth->setHfpConfig(hfpConfig, &hfpConfig2));
    EXPECT_EQ(hfpConfig, hfpConfig2);
}

TEST_P(AudioCoreBluetooth, HfpConfigInvalid) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "Bluetooth is not supported";
    }
    ndk::ScopedAStatus status;
    IBluetooth::HfpConfig hfpConfig;
    ASSERT_STATUS(kStatuses, status = bluetooth->setHfpConfig({}, &hfpConfig));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "BT HFP is not supported";
    }
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                  bluetooth->setHfpConfig({.sampleRate = Int{-1}}, &hfpConfig));
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, bluetooth->setHfpConfig({.sampleRate = Int{0}}, &hfpConfig));
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                  bluetooth->setHfpConfig({.volume = Float{IBluetooth::HfpConfig::VOLUME_MIN - 1}},
                                          &hfpConfig));
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                  bluetooth->setHfpConfig({.volume = Float{IBluetooth::HfpConfig::VOLUME_MAX + 1}},
                                          &hfpConfig));
}

class AudioCoreBluetoothA2dp : public AudioCoreModuleBase,
                               public testing::TestWithParam<std::string> {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam()));
        ASSERT_IS_OK(module->getBluetoothA2dp(&bluetooth));
    }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }

    std::shared_ptr<IBluetoothA2dp> bluetooth;
};

TEST_P(AudioCoreBluetoothA2dp, SameInstance) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothA2dp is not supported";
    }
    std::shared_ptr<IBluetoothA2dp> bluetooth2;
    EXPECT_IS_OK(module->getBluetoothA2dp(&bluetooth2));
    ASSERT_NE(nullptr, bluetooth2.get());
    EXPECT_EQ(bluetooth->asBinder(), bluetooth2->asBinder())
            << "getBluetoothA2dp must return the same interface instance across invocations";
}

TEST_P(AudioCoreBluetoothA2dp, Enabled) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothA2dp is not supported";
    }
    // Since enabling A2DP may require having an actual device connection,
    // limit testing to setting back the current value.
    bool enabled;
    ASSERT_IS_OK(bluetooth->isEnabled(&enabled));
    EXPECT_IS_OK(bluetooth->setEnabled(enabled))
            << "setEnabled without actual state change must not fail";
}

TEST_P(AudioCoreBluetoothA2dp, OffloadReconfiguration) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothA2dp is not supported";
    }
    bool isSupported;
    ASSERT_IS_OK(bluetooth->supportsOffloadReconfiguration(&isSupported));
    bool isSupported2;
    ASSERT_IS_OK(bluetooth->supportsOffloadReconfiguration(&isSupported2));
    EXPECT_EQ(isSupported, isSupported2);
    if (isSupported) {
        static const auto kStatuses = {EX_NONE, EX_ILLEGAL_STATE};
        EXPECT_STATUS(kStatuses, bluetooth->reconfigureOffload({}));
    } else {
        EXPECT_STATUS(EX_UNSUPPORTED_OPERATION, bluetooth->reconfigureOffload({}));
    }
}

class AudioCoreBluetoothLe : public AudioCoreModuleBase,
                             public testing::TestWithParam<std::string> {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam()));
        ASSERT_IS_OK(module->getBluetoothLe(&bluetooth));
    }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }

    std::shared_ptr<IBluetoothLe> bluetooth;
};

TEST_P(AudioCoreBluetoothLe, SameInstance) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothLe is not supported";
    }
    std::shared_ptr<IBluetoothLe> bluetooth2;
    EXPECT_IS_OK(module->getBluetoothLe(&bluetooth2));
    ASSERT_NE(nullptr, bluetooth2.get());
    EXPECT_EQ(bluetooth->asBinder(), bluetooth2->asBinder())
            << "getBluetoothLe must return the same interface instance across invocations";
}

TEST_P(AudioCoreBluetoothLe, Enabled) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothLe is not supported";
    }
    // Since enabling LE may require having an actual device connection,
    // limit testing to setting back the current value.
    bool enabled;
    ASSERT_IS_OK(bluetooth->isEnabled(&enabled));
    EXPECT_IS_OK(bluetooth->setEnabled(enabled))
            << "setEnabled without actual state change must not fail";
}

TEST_P(AudioCoreBluetoothLe, OffloadReconfiguration) {
    if (bluetooth == nullptr) {
        GTEST_SKIP() << "BluetoothLe is not supported";
    }
    bool isSupported;
    ASSERT_IS_OK(bluetooth->supportsOffloadReconfiguration(&isSupported));
    bool isSupported2;
    ASSERT_IS_OK(bluetooth->supportsOffloadReconfiguration(&isSupported2));
    EXPECT_EQ(isSupported, isSupported2);
    if (isSupported) {
        static const auto kStatuses = {EX_NONE, EX_ILLEGAL_STATE};
        EXPECT_STATUS(kStatuses, bluetooth->reconfigureOffload({}));
    } else {
        EXPECT_STATUS(EX_UNSUPPORTED_OPERATION, bluetooth->reconfigureOffload({}));
    }
}

class AudioCoreTelephony : public AudioCoreModuleBase, public testing::TestWithParam<std::string> {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam()));
        ASSERT_IS_OK(module->getTelephony(&telephony));
    }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }

    std::shared_ptr<ITelephony> telephony;
};

TEST_P(AudioCoreTelephony, SameInstance) {
    if (telephony == nullptr) {
        GTEST_SKIP() << "Telephony is not supported";
    }
    std::shared_ptr<ITelephony> telephony2;
    EXPECT_IS_OK(module->getTelephony(&telephony2));
    ASSERT_NE(nullptr, telephony2.get());
    EXPECT_EQ(telephony->asBinder(), telephony2->asBinder())
            << "getTelephony must return the same interface instance across invocations";
}

TEST_P(AudioCoreTelephony, GetSupportedAudioModes) {
    if (telephony == nullptr) {
        GTEST_SKIP() << "Telephony is not supported";
    }
    std::vector<AudioMode> modes1;
    ASSERT_IS_OK(telephony->getSupportedAudioModes(&modes1));
    for (const auto mode : modes1) {
        EXPECT_TRUE(isValidAudioMode(mode)) << toString(mode);
    }
    const std::vector<AudioMode> kMandatoryModes = {AudioMode::NORMAL, AudioMode::RINGTONE,
                                                    AudioMode::IN_CALL,
                                                    AudioMode::IN_COMMUNICATION};
    for (const auto mode : kMandatoryModes) {
        EXPECT_NE(modes1.end(), std::find(modes1.begin(), modes1.end(), mode))
                << "Mandatory mode not supported: " << toString(mode);
    }
    std::vector<AudioMode> modes2;
    ASSERT_IS_OK(telephony->getSupportedAudioModes(&modes2));
    ASSERT_EQ(modes1.size(), modes2.size())
            << "Sizes of audio mode arrays do not match across consequent calls to "
            << "getSupportedAudioModes";
    std::sort(modes1.begin(), modes1.end());
    std::sort(modes2.begin(), modes2.end());
    EXPECT_EQ(modes1, modes2);
};

TEST_P(AudioCoreTelephony, SwitchAudioMode) {
    if (telephony == nullptr) {
        GTEST_SKIP() << "Telephony is not supported";
    }
    std::vector<AudioMode> supportedModes;
    ASSERT_IS_OK(telephony->getSupportedAudioModes(&supportedModes));
    std::set<AudioMode> unsupportedModes = {
            // Start with all, remove supported ones
            ::ndk::enum_range<AudioMode>().begin(), ::ndk::enum_range<AudioMode>().end()};
    for (const auto mode : supportedModes) {
        EXPECT_IS_OK(telephony->switchAudioMode(mode)) << toString(mode);
        unsupportedModes.erase(mode);
    }
    for (const auto mode : unsupportedModes) {
        EXPECT_STATUS(isValidAudioMode(mode) ? EX_UNSUPPORTED_OPERATION : EX_ILLEGAL_ARGUMENT,
                      telephony->switchAudioMode(mode))
                << toString(mode);
    }
}

TEST_P(AudioCoreTelephony, TelecomConfig) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    if (telephony == nullptr) {
        GTEST_SKIP() << "Telephony is not supported";
    }
    ndk::ScopedAStatus status;
    ITelephony::TelecomConfig telecomConfig;
    ASSERT_STATUS(kStatuses, status = telephony->setTelecomConfig({}, &telecomConfig));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "Telecom is not supported";
    }
    EXPECT_TRUE(telecomConfig.voiceVolume.has_value());
    EXPECT_NE(ITelephony::TelecomConfig::TtyMode::UNSPECIFIED, telecomConfig.ttyMode);
    EXPECT_TRUE(telecomConfig.isHacEnabled.has_value());
    ITelephony::TelecomConfig telecomConfig2;
    ASSERT_IS_OK(telephony->setTelecomConfig(telecomConfig, &telecomConfig2));
    EXPECT_EQ(telecomConfig, telecomConfig2);
}

TEST_P(AudioCoreTelephony, TelecomConfigInvalid) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    if (telephony == nullptr) {
        GTEST_SKIP() << "Telephony is not supported";
    }
    ndk::ScopedAStatus status;
    ITelephony::TelecomConfig telecomConfig;
    ASSERT_STATUS(kStatuses, status = telephony->setTelecomConfig({}, &telecomConfig));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "Telecom is not supported";
    }
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                  telephony->setTelecomConfig(
                          {.voiceVolume = Float{ITelephony::TelecomConfig::VOICE_VOLUME_MIN - 1}},
                          &telecomConfig));
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                  telephony->setTelecomConfig(
                          {.voiceVolume = Float{ITelephony::TelecomConfig::VOICE_VOLUME_MAX + 1}},
                          &telecomConfig));
}

using CommandSequence = std::vector<StreamDescriptor::Command>;
class StreamLogicDriverInvalidCommand : public StreamLogicDriver {
  public:
    StreamLogicDriverInvalidCommand(const CommandSequence& commands) : mCommands(commands) {}

    std::string getUnexpectedStatuses() {
        // This method is intended to be called after the worker thread has joined,
        // thus no extra synchronization is needed.
        std::string s;
        if (!mStatuses.empty()) {
            s = std::string("Pairs of (command, actual status): ")
                        .append((android::internal::ToString(mStatuses)));
        }
        return s;
    }

    bool done() override { return mNextCommand >= mCommands.size(); }
    TransitionTrigger getNextTrigger(int, int* actualSize) override {
        if (actualSize != nullptr) *actualSize = 0;
        return mCommands[mNextCommand++];
    }
    bool interceptRawReply(const StreamDescriptor::Reply& reply) override {
        const size_t currentCommand = mNextCommand - 1;  // increased by getNextTrigger
        const bool isLastCommand = currentCommand == mCommands.size() - 1;
        // All but the last command should run correctly. The last command must return 'BAD_VALUE'
        // status.
        if ((!isLastCommand && reply.status != STATUS_OK) ||
            (isLastCommand && reply.status != STATUS_BAD_VALUE)) {
            std::string s = mCommands[currentCommand].toString();
            s.append(", ").append(statusToString(reply.status));
            mStatuses.push_back(std::move(s));
            // Process the reply, since the worker exits in case of an error.
            return false;
        }
        return isLastCommand;
    }
    bool processValidReply(const StreamDescriptor::Reply&) override { return true; }

  private:
    const CommandSequence mCommands;
    size_t mNextCommand = 0;
    std::vector<std::string> mStatuses;
};

// A helper which sets up necessary HAL structures for a proper stream initialization.
//
// The full sequence of actions to set up a stream is as follows:
//
//  device port -> connect if necessary -> set up port config   | -> set up patch
//  mix port -> set up port config, unless it has been provided |
//
//  then, from the patch, figure out the minimum HAL buffer size -> set up stream
//
// This sequence is reflected in the order of fields declaration.
// Various tests need to be able to start and stop at various point in this sequence,
// this is why there are methods that do just part of the work.
//
// Note: To maximize test coverage, this class relies on simulation of external device
// connections by the HAL module.
template <typename Stream>
class StreamFixture {
  public:
    // Tests might need to override the direction.
    StreamFixture(bool isInput = IOTraits<Stream>::is_input) : mIsInput(isInput) {}

    void SetUpPortConfigAnyMixPort(IModule* module, ModuleConfig* moduleConfig,
                                   bool connectedOnly) {
        const auto mixPorts = moduleConfig->getMixPorts(mIsInput, connectedOnly);
        mSkipTestReason = "No mix ports";
        for (const auto& mixPort : mixPorts) {
            mSkipTestReason = "";
            ASSERT_NO_FATAL_FAILURE(SetUpPortConfigForMixPortOrConfig(module, moduleConfig, mixPort,
                                                                      connectedOnly));
            if (mSkipTestReason.empty()) break;
        }
    }

    void SetUpPortConfigForMixPortOrConfig(
            IModule* module, ModuleConfig* moduleConfig, const AudioPort& initialMixPort,
            bool connectedOnly, const std::optional<AudioPortConfig>& mixPortConfig = {}) {
        if (mixPortConfig.has_value() && !connectedOnly) {
            // Connecting an external device may cause change in mix port profiles and the provided
            // config may become invalid.
            LOG(FATAL) << __func__ << ": when specifying a mix port config, it is not allowed "
                       << "to change connected devices, thus `connectedOnly` must be `true`";
        }
        std::optional<AudioPort> connectedDevicePort;
        ASSERT_NO_FATAL_FAILURE(SetUpDevicePortForMixPort(module, moduleConfig, initialMixPort,
                                                          connectedOnly, &connectedDevicePort));
        if (!mSkipTestReason.empty()) return;
        if (mixPortConfig.has_value()) {
            ASSERT_NO_FATAL_FAILURE(
                    SetUpPortConfig(module, moduleConfig, *mixPortConfig, *connectedDevicePort));
        } else {
            // If an external device was connected, the profiles of the mix port might have changed.
            AudioPort mixPort;
            ASSERT_NO_FATAL_FAILURE(module->getAudioPort(initialMixPort.id, &mixPort));
            ASSERT_NO_FATAL_FAILURE(
                    SetUpPortConfig(module, moduleConfig, mixPort, *connectedDevicePort));
        }
    }

    void SetUpPortConfig(IModule* module, ModuleConfig* moduleConfig, const AudioPort& mixPort,
                         const AudioPort& devicePort) {
        auto mixPortConfig = moduleConfig->getSingleConfigForMixPort(mIsInput, mixPort);
        ASSERT_TRUE(mixPortConfig.has_value())
                << "Unable to generate port config for mix port " << mixPort.toString();
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfig(module, moduleConfig, *mixPortConfig, devicePort));
    }
    void SetUpPortConfig(IModule* module, ModuleConfig* moduleConfig,
                         const AudioPortConfig& mixPortConfig, const AudioPort& devicePort) {
        ASSERT_NO_FATAL_FAILURE(SetUpPatch(module, moduleConfig, mixPortConfig, devicePort));
        mStream = std::make_unique<WithStream<Stream>>(mMixPortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpPortConfig(module));
    }

    ScopedAStatus SetUpStreamNoChecks(IModule* module) {
        return mStream->SetUpNoChecks(module, getMinimumStreamBufferSizeFrames());
    }
    void SetUpStream(IModule* module) {
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpStream(module, getMinimumStreamBufferSizeFrames()));
    }

    void SetUpStreamForDevicePort(
            IModule* module, ModuleConfig* moduleConfig, const AudioPort& devicePort,
            bool connectedOnly = false,
            const std::optional<AudioDeviceAddress>& connectionAddress = std::nullopt) {
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfigForDevicePort(module, moduleConfig, devicePort,
                                                             connectedOnly, connectionAddress));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpStreamForAnyMixPort(IModule* module, ModuleConfig* moduleConfig,
                                  bool connectedOnly = false) {
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfigAnyMixPort(module, moduleConfig, connectedOnly));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpStreamForMixPort(IModule* module, ModuleConfig* moduleConfig,
                               const AudioPort& mixPort, bool connectedOnly = false) {
        ASSERT_NO_FATAL_FAILURE(
                SetUpPortConfigForMixPortOrConfig(module, moduleConfig, mixPort, connectedOnly));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpStreamForPortsPair(IModule* module, ModuleConfig* moduleConfig,
                                 const AudioPort& mixPort, const AudioPort& devicePort) {
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfig(module, moduleConfig, mixPort, devicePort));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpStreamForMixPortConfig(IModule* module, ModuleConfig* moduleConfig,
                                     const AudioPortConfig& mixPortConfig) {
        // Since mix port configs may change after connecting an external device,
        // only connected device ports are considered.
        constexpr bool connectedOnly = true;
        const auto& ports = moduleConfig->getMixPorts(mIsInput, connectedOnly);
        const auto mixPortIt = findById<AudioPort>(ports, mixPortConfig.portId);
        ASSERT_NE(mixPortIt, ports.end()) << "Port id " << mixPortConfig.portId << " not found";
        ASSERT_NO_FATAL_FAILURE(SetUpPortConfigForMixPortOrConfig(module, moduleConfig, *mixPortIt,
                                                                  connectedOnly, mixPortConfig));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpStreamForNewMixPortConfig(IModule* module, ModuleConfig*,
                                        const AudioPortConfig& existingMixPortConfig,
                                        const AudioPortConfig& existingDevicePortConfig) {
        auto mixPortConfig = existingMixPortConfig;
        mixPortConfig.id = 0;
        mMixPortConfig = std::make_unique<WithAudioPortConfig>(mixPortConfig);
        ASSERT_NO_FATAL_FAILURE(mMixPortConfig->SetUp(module));
        mDevicePortConfig = std::make_unique<WithAudioPortConfig>(existingDevicePortConfig);
        ASSERT_NO_FATAL_FAILURE(mDevicePortConfig->SetUp(module));
        mDevice = existingDevicePortConfig.ext.get<AudioPortExt::device>().device;
        mPatch = std::make_unique<WithAudioPatch>(mIsInput, mMixPortConfig->get(),
                                                  mDevicePortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mPatch->SetUp(module));
        mStream = std::make_unique<WithStream<Stream>>(mMixPortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpPortConfig(module));
        ASSERT_NO_FATAL_FAILURE(SetUpStream(module));
    }
    void SetUpPatchForMixPortConfig(IModule* module, ModuleConfig* moduleConfig,
                                    const AudioPortConfig& mixPortConfig) {
        constexpr bool connectedOnly = true;
        const auto& ports = moduleConfig->getMixPorts(mIsInput, connectedOnly);
        const auto mixPortIt = findById<AudioPort>(ports, mixPortConfig.portId);
        ASSERT_NE(mixPortIt, ports.end()) << "Port id " << mixPortConfig.portId << " not found";
        std::optional<AudioPort> connectedDevicePort;
        ASSERT_NO_FATAL_FAILURE(SetUpDevicePortForMixPort(module, moduleConfig, *mixPortIt,
                                                          connectedOnly, &connectedDevicePort));
        if (!mSkipTestReason.empty()) return;
        ASSERT_NO_FATAL_FAILURE(
                SetUpPatch(module, moduleConfig, mixPortConfig, *connectedDevicePort));
    }

    void ReconnectPatch(IModule* module) {
        mPatch = std::make_unique<WithAudioPatch>(mIsInput, mMixPortConfig->get(),
                                                  mDevicePortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mPatch->SetUp(module));
    }
    void TeardownPatch() { mPatch.reset(); }
    // Assuming that the patch is set up, while the stream isn't yet,
    // tear the patch down and set up stream.
    void TeardownPatchSetUpStream(IModule* module) {
        const int32_t bufferSize = getMinimumStreamBufferSizeFrames();
        ASSERT_NO_FATAL_FAILURE(TeardownPatch());
        mStream = std::make_unique<WithStream<Stream>>(mMixPortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpPortConfig(module));
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpStream(module, bufferSize));
    }

    const AudioDevice& getDevice() const { return mDevice; }
    const AudioPortConfig& getDevicePortConfig() const { return mDevicePortConfig->get(); }
    int32_t getMinimumStreamBufferSizeFrames() const {
        return mPatch->getMinimumStreamBufferSizeFrames();
    }
    const AudioPatch& getPatch() const { return mPatch->get(); }
    const AudioPortConfig& getPortConfig() const { return mMixPortConfig->get(); }
    int32_t getPortId() const { return mMixPortConfig->getId(); }
    Stream* getStream() const { return mStream->get(); }
    const StreamContext* getStreamContext() const { return mStream->getContext(); }
    StreamEventReceiver* getStreamEventReceiver() { return mStream->getEventReceiver(); }
    std::shared_ptr<Stream> getStreamSharedPointer() const { return mStream->getSharedPointer(); }
    StreamWorkerMethods* getStreamWorkerMethods() const { return mStream.get(); }
    const std::string& skipTestReason() const { return mSkipTestReason; }

  private:
    void SetUpDevicePort(IModule* module, ModuleConfig* moduleConfig,
                         const std::set<int32_t>& devicePortIds, bool connectedOnly,
                         std::optional<AudioPort>* connectedDevicePort,
                         const std::optional<AudioDeviceAddress>& connectionAddress) {
        const auto attachedDevicePorts = moduleConfig->getAttachedDevicePorts();
        if (auto it = findAny<AudioPort>(attachedDevicePorts, devicePortIds);
            it != attachedDevicePorts.end()) {
            *connectedDevicePort = *it;
            LOG(DEBUG) << __func__ << ": found attached port " << it->toString();
        }
        const auto connectedDevicePorts = moduleConfig->getConnectedExternalDevicePorts();
        if (auto it = findAny<AudioPort>(connectedDevicePorts, devicePortIds);
            it != connectedDevicePorts.end()) {
            *connectedDevicePort = *it;
            LOG(DEBUG) << __func__ << ": found connected port " << it->toString();
        }
        if (!connectedOnly && !connectedDevicePort->has_value()) {
            const auto externalDevicePorts = moduleConfig->getExternalDevicePorts();
            if (auto it = findAny<AudioPort>(externalDevicePorts, devicePortIds);
                it != externalDevicePorts.end()) {
                AudioPort portWithData = *it;
                if (connectionAddress.has_value()) {
                    portWithData.ext.get<AudioPortExt::Tag::device>().device.address =
                            *connectionAddress;
                }
                portWithData = GenerateUniqueDeviceAddress(portWithData);
                mPortConnected = std::make_unique<WithDevicePortConnectedState>(portWithData);
                ASSERT_NO_FATAL_FAILURE(mPortConnected->SetUp(module, moduleConfig));
                *connectedDevicePort = mPortConnected->get();
                LOG(DEBUG) << __func__ << ": connected port " << mPortConnected->get().toString();
            }
        }
    }
    void SetUpDevicePortForMixPort(IModule* module, ModuleConfig* moduleConfig,
                                   const AudioPort& mixPort, bool connectedOnly,
                                   std::optional<AudioPort>* connectedDevicePort) {
        const auto devicePorts =
                moduleConfig->getRoutableDevicePortsForMixPort(mixPort, connectedOnly);
        if (devicePorts.empty()) {
            mSkipTestReason = std::string("No routable device ports found for mix port id ")
                                      .append(std::to_string(mixPort.id));
            LOG(DEBUG) << __func__ << ": " << mSkipTestReason;
            return;
        };
        ASSERT_NO_FATAL_FAILURE(SetUpDevicePort(
                module, moduleConfig, extractIds<AudioPort>(devicePorts), connectedOnly,
                connectedDevicePort, std::nullopt /*connectionAddress*/));
        if (!connectedDevicePort->has_value()) {
            mSkipTestReason = std::string("Unable to find a device port pair for mix port id ")
                                      .append(std::to_string(mixPort.id));
            LOG(DEBUG) << __func__ << ": " << mSkipTestReason;
            return;
        }
    }
    void SetUpPortConfigForDevicePort(
            IModule* module, ModuleConfig* moduleConfig, const AudioPort& devicePort,
            bool connectedOnly,
            const std::optional<AudioDeviceAddress>& connectionAddress = std::nullopt) {
        std::optional<AudioPort> connectedDevicePort;
        ASSERT_NO_FATAL_FAILURE(SetUpDevicePort(module, moduleConfig, {devicePort.id},
                                                connectedOnly, &connectedDevicePort,
                                                connectionAddress));
        if (!connectedDevicePort.has_value()) {
            mSkipTestReason = std::string("Device port id ")
                                      .append(std::to_string(devicePort.id))
                                      .append(" is not attached and can not be connected");
            return;
        }
        const auto mixPorts = moduleConfig->getRoutableMixPortsForDevicePort(
                *connectedDevicePort, true /*connectedOnly*/);
        if (mixPorts.empty()) {
            mSkipTestReason = std::string("No routable mix ports found for device port id ")
                                      .append(std::to_string(devicePort.id));
            return;
        }
        ASSERT_NO_FATAL_FAILURE(
                SetUpPortConfig(module, moduleConfig, *mixPorts.begin(), *connectedDevicePort));
    }
    void SetUpPatch(IModule* module, ModuleConfig* moduleConfig,
                    const AudioPortConfig& mixPortConfig, const AudioPort& devicePort) {
        mMixPortConfig = std::make_unique<WithAudioPortConfig>(mixPortConfig);
        ASSERT_NO_FATAL_FAILURE(mMixPortConfig->SetUp(module));
        mDevicePortConfig = std::make_unique<WithAudioPortConfig>(
                moduleConfig->getSingleConfigForDevicePort(devicePort));
        ASSERT_NO_FATAL_FAILURE(mDevicePortConfig->SetUp(module));
        mDevice = devicePort.ext.get<AudioPortExt::device>().device;
        mPatch = std::make_unique<WithAudioPatch>(mIsInput, mMixPortConfig->get(),
                                                  mDevicePortConfig->get());
        ASSERT_NO_FATAL_FAILURE(mPatch->SetUp(module));
    }

    const bool mIsInput;
    std::string mSkipTestReason;
    std::unique_ptr<WithDevicePortConnectedState> mPortConnected;
    AudioDevice mDevice;
    std::unique_ptr<WithAudioPortConfig> mMixPortConfig;
    std::unique_ptr<WithAudioPortConfig> mDevicePortConfig;
    std::unique_ptr<WithAudioPatch> mPatch;
    std::unique_ptr<WithStream<Stream>> mStream;
};

class StreamLogicDefaultDriver : public StreamLogicDriver {
  public:
    StreamLogicDefaultDriver(std::shared_ptr<StateSequence> commands, size_t frameSizeBytes,
                             bool isMmap)
        : mCommands(commands), mFrameSizeBytes(frameSizeBytes), mIsMmap(isMmap) {
        mCommands->rewind();
    }

    // The five methods below is intended to be called after the worker
    // thread has joined, thus no extra synchronization is needed.
    bool hasObservablePositionIncrease() const { return mObservable.hasPositionIncrease; }
    bool hasObservableRetrogradePosition() const { return mObservable.hasRetrogradePosition; }
    bool hasHardwarePositionIncrease() const {
        // For non-MMap, always return true to pass the validation.
        return mIsMmap ? mHardware.hasPositionIncrease : true;
    }
    bool hasHardwareRetrogradePosition() const {
        // For non-MMap, always return false to pass the validation.
        return mIsMmap ? mHardware.hasRetrogradePosition : false;
    }
    std::string getUnexpectedStateTransition() const { return mUnexpectedTransition; }

    bool done() override { return mCommands->done(); }
    TransitionTrigger getNextTrigger(int maxDataSize, int* actualSize) override {
        auto trigger = mCommands->getTrigger();
        if (StreamDescriptor::Command* command = std::get_if<StreamDescriptor::Command>(&trigger);
            command != nullptr) {
            if (command->getTag() == StreamDescriptor::Command::Tag::burst) {
                if (actualSize != nullptr) {
                    // In the output scenario, reduce slightly the fmqByteCount to verify
                    // that the HAL module always consumes all data from the MQ.
                    if (maxDataSize > static_cast<int>(mFrameSizeBytes)) {
                        LOG(DEBUG) << __func__ << ": reducing data size by " << mFrameSizeBytes;
                        maxDataSize -= mFrameSizeBytes;
                    }
                    *actualSize = maxDataSize;
                }
                command->set<StreamDescriptor::Command::Tag::burst>(maxDataSize);
            } else {
                if (actualSize != nullptr) *actualSize = 0;
            }
        }
        return trigger;
    }
    bool interceptRawReply(const StreamDescriptor::Reply&) override { return false; }
    bool processValidReply(const StreamDescriptor::Reply& reply) override {
        mObservable.update(reply.observable.frames);
        if (mIsMmap) {
            mHardware.update(reply.hardware.frames);
        }

        auto expected = mCommands->getExpectedStates();
        if (expected.count(reply.state) == 0) {
            std::string s =
                    std::string("Unexpected transition from the state ")
                            .append(mPreviousState.has_value() ? toString(mPreviousState.value())
                                                               : "<initial state>")
                            .append(" to ")
                            .append(toString(reply.state))
                            .append(" (expected one of ")
                            .append(::android::internal::ToString(expected))
                            .append(") caused by the ")
                            .append(toString(mCommands->getTrigger()));
            LOG(ERROR) << __func__ << ": " << s;
            mUnexpectedTransition = std::move(s);
            return false;
        }
        mCommands->advance(reply.state);
        mPreviousState = reply.state;
        return true;
    }

  protected:
    struct FramesCounter {
        std::optional<int64_t> previous;
        bool hasPositionIncrease = false;
        bool hasRetrogradePosition = false;

        void update(int64_t position) {
            if (position == StreamDescriptor::Position::UNKNOWN) return;
            if (previous.has_value()) {
                if (position > previous.value()) {
                    hasPositionIncrease = true;
                } else if (position < previous.value()) {
                    hasRetrogradePosition = true;
                }
            }
            previous = position;
        }
    };

    std::shared_ptr<StateSequence> mCommands;
    const size_t mFrameSizeBytes;
    const bool mIsMmap;
    std::optional<StreamDescriptor::State> mPreviousState;
    FramesCounter mObservable;
    FramesCounter mHardware;
    std::string mUnexpectedTransition;
};

// Defined later together with state transition sequences.
std::shared_ptr<StateSequence> makeBurstCommands(bool isSync, size_t burstCount = 10,
                                                 bool standbyInputWhenDone = false);

// Certain types of ports can not be used without special preconditions.
static bool skipStreamIoTestForMixPortConfig(const AudioPortConfig& portConfig) {
    return (portConfig.flags.value().getTag() == AudioIoFlags::input &&
            isAnyBitPositionFlagSet(portConfig.flags.value().template get<AudioIoFlags::input>(),
                                    {AudioInputFlags::VOIP_TX, AudioInputFlags::HW_HOTWORD,
                                     AudioInputFlags::HOTWORD_TAP})) ||
           (portConfig.flags.value().getTag() == AudioIoFlags::output &&
            (isAnyBitPositionFlagSet(portConfig.flags.value().template get<AudioIoFlags::output>(),
                                     {AudioOutputFlags::VOIP_RX, AudioOutputFlags::INCALL_MUSIC}) ||
             (isBitPositionFlagSet(portConfig.flags.value().template get<AudioIoFlags::output>(),
                                   AudioOutputFlags::COMPRESS_OFFLOAD) &&
              !getMediaFileInfoForConfig(portConfig))));
}

// Certain types of devices can not be used without special preconditions.
static bool skipStreamIoTestForDevice(const AudioDevice& device) {
    return device.type.type == AudioDeviceType::IN_ECHO_REFERENCE;
}

// MMap implementation on the HAL version <= 3 was not test compliant,
// unless the stream provides 'createMmapBuffer'
static bool skipStreamIoTestForStream(const StreamContext* context, StreamWorkerMethods* stream) {
    return context->isMmapped() && !stream->supportsCreateMmapBuffer();
}

template <typename Stream>
class StreamFixtureWithWorker {
  public:
    explicit StreamFixtureWithWorker(bool isSync) : mIsSync(isSync) {}

    void SetUp(IModule* module, ModuleConfig* moduleConfig, const AudioPort& devicePort,
               const std::optional<AudioDeviceAddress>& connectionAddress = std::nullopt) {
        mStream = std::make_unique<StreamFixture<Stream>>();
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpStreamForDevicePort(
                module, moduleConfig, devicePort, false /*connectedOnly*/, connectionAddress));
        MaybeSetSkipTestReason();
    }

    void SetUp(IModule* module, ModuleConfig* moduleConfig, const AudioPort& mixPort,
               const AudioPort& devicePort) {
        mStream = std::make_unique<StreamFixture<Stream>>();
        ASSERT_NO_FATAL_FAILURE(
                mStream->SetUpStreamForPortsPair(module, moduleConfig, mixPort, devicePort));
        MaybeSetSkipTestReason();
    }

    void SetUp(IModule* module, ModuleConfig* moduleConfig,
               const AudioPortConfig& existingMixPortConfig,
               const AudioPortConfig& existingDevicePortConfig) {
        mStream = std::make_unique<StreamFixture<Stream>>();
        ASSERT_NO_FATAL_FAILURE(mStream->SetUpStreamForNewMixPortConfig(
                module, moduleConfig, existingMixPortConfig, existingDevicePortConfig));
        MaybeSetSkipTestReason();
    }

    void SendBurstCommands(bool validatePosition = true, size_t burstCount = 10,
                           bool standbyInputWhenDone = false) {
        ASSERT_NO_FATAL_FAILURE(StartWorkerToSendBurstCommands(burstCount, standbyInputWhenDone));
        ASSERT_NO_FATAL_FAILURE(JoinWorkerAfterBurstCommands(validatePosition));
    }

    void StartWorkerToSendBurstCommands(size_t burstCount = 10, bool standbyInputWhenDone = false) {
        if (!IOTraits<Stream>::is_input) {
            ASSERT_FALSE(standbyInputWhenDone) << "Only supported for input";
        }
        const StreamContext* context = mStream->getStreamContext();
        mWorkerDriver = std::make_unique<StreamLogicDefaultDriver>(
                makeBurstCommands(mIsSync, burstCount, standbyInputWhenDone),
                context->getFrameSizeBytes(), context->isMmapped());
        mWorker = std::make_unique<typename IOTraits<Stream>::Worker>(
                *context, mWorkerDriver.get(), mStream->getStreamWorkerMethods(),
                mStream->getStreamEventReceiver());
        LOG(DEBUG) << __func__ << ": starting " << IOTraits<Stream>::directionStr << " worker...";
        ASSERT_TRUE(mWorker->start());
    }

    void JoinWorkerAfterBurstCommands(bool validatePosition = true,
                                      bool callPrepareToClose = true) {
        if (callPrepareToClose) {
            std::shared_ptr<IStreamCommon> common;
            ASSERT_IS_OK(mStream->getStream()->getStreamCommon(&common));
            ASSERT_IS_OK(common->prepareToClose());
        }
        LOG(DEBUG) << __func__ << ": joining " << IOTraits<Stream>::directionStr << " worker...";
        mWorker->join();
        EXPECT_FALSE(mWorker->hasError()) << mWorker->getError();
        EXPECT_EQ("", mWorkerDriver->getUnexpectedStateTransition());
        if (validatePosition) {
            EXPECT_TRUE(mWorkerDriver->hasObservablePositionIncrease());
            EXPECT_TRUE(mWorkerDriver->hasHardwarePositionIncrease());
            EXPECT_FALSE(mWorkerDriver->hasObservableRetrogradePosition());
            EXPECT_FALSE(mWorkerDriver->hasHardwareRetrogradePosition());
        }
        mLastData = mWorker->getData();
        mWorker.reset();
        mWorkerDriver.reset();
    }

    void TeardownPatch() { mStream->TeardownPatch(); }

    const AudioDevice& getDevice() const { return mStream->getDevice(); }
    const AudioPortConfig& getDevicePortConfig() const { return mStream->getDevicePortConfig(); }
    const std::vector<int8_t>& getLastData() const { return mLastData; }
    const AudioPortConfig& getPortConfig() const { return mStream->getPortConfig(); }
    Stream* getStream() const { return mStream->getStream(); }
    std::string skipTestReason() const {
        return !mSkipTestReason.empty() ? mSkipTestReason : mStream->skipTestReason();
    }

  private:
    void MaybeSetSkipTestReason() {
        if (skipStreamIoTestForMixPortConfig(mStream->getPortConfig())) {
            mSkipTestReason = "Mix port config is not supported for stream I/O tests";
        }
        if (skipStreamIoTestForStream(mStream->getStreamContext(),
                                      mStream->getStreamWorkerMethods())) {
            mSkipTestReason = "Stream can not be used in I/O tests";
        }
    }

    const bool mIsSync;
    std::string mSkipTestReason;
    std::unique_ptr<StreamFixture<Stream>> mStream;
    std::unique_ptr<StreamLogicDefaultDriver> mWorkerDriver;
    std::unique_ptr<typename IOTraits<Stream>::Worker> mWorker;
    std::vector<int8_t> mLastData;
};

template <typename Stream>
class AudioStream : public AudioCoreModule {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(AudioCoreModule::SetUp());
        ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    }

    void GetStreamCommon() {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        std::shared_ptr<IStreamCommon> streamCommon1;
        EXPECT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon1));
        std::shared_ptr<IStreamCommon> streamCommon2;
        EXPECT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon2));
        ASSERT_NE(nullptr, streamCommon1);
        ASSERT_NE(nullptr, streamCommon2);
        EXPECT_EQ(streamCommon1->asBinder(), streamCommon2->asBinder())
                << "getStreamCommon must return the same interface instance across invocations";
    }

    void CloseTwice() {
        std::shared_ptr<Stream> heldStream;
        {
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(
                    stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
            if (auto reason = stream.skipTestReason(); !reason.empty()) {
                GTEST_SKIP() << reason;
            }
            heldStream = stream.getStreamSharedPointer();
        }
        EXPECT_STATUS(EX_ILLEGAL_STATE, WithStream<Stream>::callClose(heldStream))
                << "when closing the stream twice";
    }

    void PrepareToCloseTwice() {
        std::shared_ptr<IStreamCommon> heldStreamCommon;
        {
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(
                    stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
            if (auto reason = stream.skipTestReason(); !reason.empty()) {
                GTEST_SKIP() << reason;
            }
            std::shared_ptr<IStreamCommon> streamCommon;
            ASSERT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon));
            heldStreamCommon = streamCommon;
            EXPECT_IS_OK(streamCommon->prepareToClose());
            EXPECT_IS_OK(streamCommon->prepareToClose())
                    << "when calling prepareToClose second time";
        }
        EXPECT_STATUS(EX_ILLEGAL_STATE, heldStreamCommon->prepareToClose())
                << "when calling prepareToClose on a closed stream";
    }

    void OpenAllConfigs() {
        const auto allPortConfigs =
                moduleConfig->getPortConfigsForMixPorts(IOTraits<Stream>::is_input);
        if (allPortConfigs.empty()) {
            GTEST_SKIP() << "No mix ports for attached devices";
        }
        for (const auto& portConfig : allPortConfigs) {
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPortConfig(
                    module.get(), moduleConfig.get(), portConfig));
        }
    }

    void OpenInvalidBufferSize() {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(IOTraits<Stream>::is_input);
        if (!portConfig.has_value()) {
            GTEST_SKIP() << "No mix port for attached devices";
        }
        WithStream<Stream> stream(portConfig.value());
        ASSERT_NO_FATAL_FAILURE(stream.SetUpPortConfig(module.get()));
        for (long bufferSize : std::array<long, 3>{-1, 0, std::numeric_limits<long>::max()}) {
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, stream.SetUpNoChecks(module.get(), bufferSize))
                    << "for the buffer size " << bufferSize;
            EXPECT_EQ(nullptr, stream.get());
        }
    }

    void OpenInvalidDirection() {
        // Important! The direction of the port config must be reversed.
        StreamFixture<Stream> stream(!IOTraits<Stream>::is_input);
        ASSERT_NO_FATAL_FAILURE(stream.SetUpPortConfigAnyMixPort(module.get(), moduleConfig.get(),
                                                                 false /*connectedOnly*/));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, stream.SetUpStreamNoChecks(module.get()))
                << "port config ID " << stream.getPortId();
        EXPECT_EQ(nullptr, stream.getStream());
    }

    void OpenOverMaxCount() {
        constexpr bool connectedOnly = true;
        constexpr bool isInput = IOTraits<Stream>::is_input;
        auto ports = moduleConfig->getMixPorts(isInput, connectedOnly);
        bool hasSingleRun = false;
        for (const auto& port : ports) {
            const size_t maxStreamCount = port.ext.get<AudioPortExt::Tag::mix>().maxOpenStreamCount;
            if (maxStreamCount == 0) {
                continue;
            }
            auto portConfigs = moduleConfig->getPortConfigsForMixPorts(isInput, port);
            if (portConfigs.size() < maxStreamCount + 1) {
                // Not able to open a sufficient number of streams for this port.
                continue;
            }
            hasSingleRun = true;
            StreamFixture<Stream> streams[maxStreamCount + 1];
            for (size_t i = 0; i <= maxStreamCount; ++i) {
                ASSERT_NO_FATAL_FAILURE(streams[i].SetUpPortConfigForMixPortOrConfig(
                        module.get(), moduleConfig.get(), port, connectedOnly, portConfigs[i]));
                ASSERT_EQ("", streams[i].skipTestReason());
                auto& stream = streams[i];
                if (i < maxStreamCount) {
                    ASSERT_NO_FATAL_FAILURE(stream.SetUpStream(module.get()));
                } else {
                    EXPECT_STATUS(EX_ILLEGAL_STATE, stream.SetUpStreamNoChecks(module.get()))
                            << "port config ID " << stream.getPortId() << ", maxOpenStreamCount is "
                            << maxStreamCount;
                }
            }
        }
        if (!hasSingleRun) {
            GTEST_SKIP() << "Not enough ports to test max open stream count";
        }
    }

    void OpenTwiceSamePortConfig() {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(IOTraits<Stream>::is_input);
        if (!portConfig.has_value()) {
            GTEST_SKIP() << "No mix port for attached devices";
        }
        EXPECT_NO_FATAL_FAILURE(OpenTwiceSamePortConfigImpl(portConfig.value()));
    }

    void ResetPortConfigWithOpenStream() {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        EXPECT_STATUS(EX_ILLEGAL_STATE, module->resetAudioPortConfig(stream.getPortId()))
                << "port config ID " << stream.getPortId();
    }

    void SendInvalidCommand() {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(IOTraits<Stream>::is_input);
        if (!portConfig.has_value()) {
            GTEST_SKIP() << "No mix port for attached devices";
        }
        EXPECT_NO_FATAL_FAILURE(SendInvalidCommandImpl(portConfig.value()));
    }

    void UpdateHwAvSyncId() {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        std::shared_ptr<IStreamCommon> streamCommon;
        ASSERT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon));
        ASSERT_NE(nullptr, streamCommon);
        const auto kStatuses = {EX_NONE, EX_ILLEGAL_ARGUMENT, EX_ILLEGAL_STATE};
        for (const auto id : {-100, -1, 0, 1, 100}) {
            ndk::ScopedAStatus status = streamCommon->updateHwAvSyncId(id);
            if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
                GTEST_SKIP() << "HW AV Sync is not supported";
            }
            EXPECT_STATUS(kStatuses, status) << "id: " << id;
        }
    }

    void GetVendorParameters() {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        std::shared_ptr<IStreamCommon> streamCommon;
        ASSERT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon));
        ASSERT_NE(nullptr, streamCommon);

        bool isGetterSupported = false;
        EXPECT_NO_FATAL_FAILURE(TestGetVendorParameters(module.get(), &isGetterSupported));
        ndk::ScopedAStatus status = module->setVendorParameters({}, false);
        EXPECT_EQ(isGetterSupported, status.getExceptionCode() != EX_UNSUPPORTED_OPERATION)
                << "Support for getting and setting of vendor parameters must be consistent";
        if (!isGetterSupported) {
            GTEST_SKIP() << "Vendor parameters are not supported";
        }
    }

    void SetVendorParameters() {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForAnyMixPort(module.get(), moduleConfig.get()));
        if (auto reason = stream.skipTestReason(); !reason.empty()) {
            GTEST_SKIP() << reason;
        }
        std::shared_ptr<IStreamCommon> streamCommon;
        ASSERT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon));
        ASSERT_NE(nullptr, streamCommon);

        bool isSupported = false;
        EXPECT_NO_FATAL_FAILURE(TestSetVendorParameters(module.get(), &isSupported));
        if (!isSupported) {
            GTEST_SKIP() << "Vendor parameters are not supported";
        }
    }

    void HwGainHwVolume() {
        // Since device connection emulation does not cover complete functionality,
        // only use this test with connected devices.
        constexpr bool connectedOnly = true;
        const auto ports = moduleConfig->getMixPorts(IOTraits<Stream>::is_input, connectedOnly);
        if (ports.empty()) {
            GTEST_SKIP() << "No mix ports";
        }
        bool atLeastOneSupports = false;
        for (const auto& port : ports) {
            SCOPED_TRACE(port.toString());
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(),
                                                                 port, connectedOnly));
            if (!stream.skipTestReason().empty()) continue;
            const auto portConfig = stream.getPortConfig();
            SCOPED_TRACE(portConfig.toString());
            std::vector<std::vector<float>> validValues, invalidValues;
            bool isSupported = false;
            if constexpr (IOTraits<Stream>::is_input) {
                GenerateTestArrays<float>(getChannelCount(portConfig.channelMask.value()),
                                          IStreamIn::HW_GAIN_MIN, IStreamIn::HW_GAIN_MAX,
                                          &validValues, &invalidValues);
                EXPECT_NO_FATAL_FAILURE(TestAccessors<std::vector<float>>(
                        stream.getStream(), &IStreamIn::getHwGain, &IStreamIn::setHwGain,
                        validValues, invalidValues, &isSupported));
            } else {
                GenerateTestArrays<float>(getChannelCount(portConfig.channelMask.value()),
                                          IStreamOut::HW_VOLUME_MIN, IStreamOut::HW_VOLUME_MAX,
                                          &validValues, &invalidValues);
                EXPECT_NO_FATAL_FAILURE(TestAccessors<std::vector<float>>(
                        stream.getStream(), &IStreamOut::getHwVolume, &IStreamOut::setHwVolume,
                        validValues, invalidValues, &isSupported));
            }
            if (isSupported) atLeastOneSupports = true;
        }
        if (!atLeastOneSupports) {
            GTEST_SKIP() << "Hardware gain / volume is not supported";
        }
    }

    // See b/262930731. In the absence of offloaded effect implementations,
    // currently we can only pass a nullptr, and the HAL module must either reject
    // it as an invalid argument, or say that offloaded effects are not supported.
    void AddRemoveEffectInvalidArguments() {
        constexpr bool connectedOnly = true;
        const auto ports = moduleConfig->getMixPorts(IOTraits<Stream>::is_input, connectedOnly);
        if (ports.empty()) {
            GTEST_SKIP() << "No mix ports";
        }
        bool atLeastOneSupports = false;
        for (const auto& port : ports) {
            SCOPED_TRACE(port.toString());
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(),
                                                                 port, connectedOnly));
            if (!stream.skipTestReason().empty()) continue;
            const auto portConfig = stream.getPortConfig();
            SCOPED_TRACE(portConfig.toString());
            std::shared_ptr<IStreamCommon> streamCommon;
            ASSERT_IS_OK(stream.getStream()->getStreamCommon(&streamCommon));
            ASSERT_NE(nullptr, streamCommon);
            ndk::ScopedAStatus addEffectStatus = streamCommon->addEffect(nullptr);
            ndk::ScopedAStatus removeEffectStatus = streamCommon->removeEffect(nullptr);
            if (addEffectStatus.getExceptionCode() != EX_UNSUPPORTED_OPERATION) {
                EXPECT_EQ(EX_ILLEGAL_ARGUMENT, addEffectStatus.getExceptionCode());
                EXPECT_EQ(EX_ILLEGAL_ARGUMENT, removeEffectStatus.getExceptionCode());
                atLeastOneSupports = true;
            } else if (removeEffectStatus.getExceptionCode() != EX_UNSUPPORTED_OPERATION) {
                ADD_FAILURE() << "addEffect and removeEffect must be either supported or "
                              << "not supported together";
                atLeastOneSupports = true;
            }
        }
        if (!atLeastOneSupports) {
            GTEST_SKIP() << "Offloaded effects not supported";
        }
    }

    void OpenTwiceSamePortConfigImpl(const AudioPortConfig& portConfig) {
        StreamFixture<Stream> stream1;
        ASSERT_NO_FATAL_FAILURE(
                stream1.SetUpStreamForMixPortConfig(module.get(), moduleConfig.get(), portConfig));
        ASSERT_EQ("", stream1.skipTestReason());
        WithStream<Stream> stream2;
        EXPECT_STATUS(EX_ILLEGAL_STATE,
                      stream2.SetUpNoChecks(module.get(), stream1.getPortConfig(),
                                            stream1.getMinimumStreamBufferSizeFrames()))
                << "when opening a stream twice for the same port config ID "
                << stream1.getPortId();
    }

    void SendInvalidCommandImpl(const AudioPortConfig& portConfig) {
        using TestSequence = std::pair<std::string, CommandSequence>;
        // The last command in 'CommandSequence' is the one that must trigger
        // an error status. All preceding commands are to put the state machine
        // into a state which accepts the last command.
        std::vector<TestSequence> sequences{
                std::make_pair(std::string("HalReservedExit"),
                               std::vector{StreamDescriptor::Command::make<
                                       StreamDescriptor::Command::Tag::halReservedExit>(0)}),
                std::make_pair(std::string("BurstNeg"),
                               std::vector{kStartCommand,
                                           StreamDescriptor::Command::make<
                                                   StreamDescriptor::Command::Tag::burst>(-1)}),
                std::make_pair(
                        std::string("BurstMinInt"),
                        std::vector{kStartCommand, StreamDescriptor::Command::make<
                                                           StreamDescriptor::Command::Tag::burst>(
                                                           std::numeric_limits<int32_t>::min())})};
        if (IOTraits<Stream>::is_input) {
            sequences.emplace_back("DrainAll",
                                   std::vector{kStartCommand, kBurstCommand, kDrainOutAllCommand});
            sequences.emplace_back(
                    "DrainEarly", std::vector{kStartCommand, kBurstCommand, kDrainOutEarlyCommand});
        } else {
            sequences.emplace_back("DrainUnspecified",
                                   std::vector{kStartCommand, kBurstCommand, kDrainInCommand});
        }
        for (const auto& seq : sequences) {
            SCOPED_TRACE(std::string("Sequence ").append(seq.first));
            LOG(DEBUG) << __func__ << ": Sequence " << seq.first;
            StreamFixture<Stream> stream;
            ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPortConfig(
                    module.get(), moduleConfig.get(), portConfig));
            ASSERT_EQ("", stream.skipTestReason());
            StreamLogicDriverInvalidCommand driver(seq.second);
            typename IOTraits<Stream>::Worker worker(*stream.getStreamContext(), &driver,
                                                     stream.getStreamWorkerMethods(),
                                                     stream.getStreamEventReceiver());
            LOG(DEBUG) << __func__ << ": starting worker...";
            ASSERT_TRUE(worker.start());
            LOG(DEBUG) << __func__ << ": joining worker...";
            worker.join();
            EXPECT_EQ("", driver.getUnexpectedStatuses());
        }
    }
};
using AudioStreamIn = AudioStream<IStreamIn>;
using AudioStreamOut = AudioStream<IStreamOut>;

#define TEST_IN_AND_OUT_STREAM(method_name)     \
    TEST_P(AudioStreamIn, method_name) {        \
        ASSERT_NO_FATAL_FAILURE(method_name()); \
    }                                           \
    TEST_P(AudioStreamOut, method_name) {       \
        ASSERT_NO_FATAL_FAILURE(method_name()); \
    }

TEST_IN_AND_OUT_STREAM(CloseTwice);
TEST_IN_AND_OUT_STREAM(PrepareToCloseTwice);
TEST_IN_AND_OUT_STREAM(GetStreamCommon);
TEST_IN_AND_OUT_STREAM(OpenAllConfigs);
TEST_IN_AND_OUT_STREAM(OpenInvalidBufferSize);
TEST_IN_AND_OUT_STREAM(OpenInvalidDirection);
TEST_IN_AND_OUT_STREAM(OpenOverMaxCount);
TEST_IN_AND_OUT_STREAM(OpenTwiceSamePortConfig);
TEST_IN_AND_OUT_STREAM(ResetPortConfigWithOpenStream);
TEST_IN_AND_OUT_STREAM(SendInvalidCommand);
TEST_IN_AND_OUT_STREAM(UpdateHwAvSyncId);
TEST_IN_AND_OUT_STREAM(GetVendorParameters);
TEST_IN_AND_OUT_STREAM(SetVendorParameters);
TEST_IN_AND_OUT_STREAM(HwGainHwVolume);
TEST_IN_AND_OUT_STREAM(AddRemoveEffectInvalidArguments);

namespace aidl::android::hardware::audio::core {
std::ostream& operator<<(std::ostream& os, const IStreamIn::MicrophoneDirection& md) {
    os << toString(md);
    return os;
}
}  // namespace aidl::android::hardware::audio::core

TEST_P(AudioStreamIn, ActiveMicrophones) {
    std::vector<MicrophoneInfo> micInfos;
    ScopedAStatus status = module->getMicrophones(&micInfos);
    if (!status.isOk()) {
        GTEST_SKIP() << "Microphone info is not supported";
    }
    const auto ports = moduleConfig->getInputMixPorts(true /*connectedOnly*/);
    if (ports.empty()) {
        GTEST_SKIP() << "No input mix ports for attached devices";
    }
    bool atLeastOnePort = false;
    for (const auto& port : ports) {
        auto micDevicePorts = ModuleConfig::getBuiltInMicPorts(
                moduleConfig->getConnectedSourceDevicesPortsForMixPort(port));
        if (micDevicePorts.empty()) continue;
        atLeastOnePort = true;
        SCOPED_TRACE(port.toString());
        StreamFixtureWithWorker<IStreamIn> stream(true /*isSync*/);
        ASSERT_NO_FATAL_FAILURE(
                stream.SetUp(module.get(), moduleConfig.get(), port, micDevicePorts[0]));
        if (!stream.skipTestReason().empty()) continue;

        ASSERT_NO_FATAL_FAILURE(stream.SendBurstCommands(false /*validatePosition*/));
        std::vector<MicrophoneDynamicInfo> activeMics;
        EXPECT_IS_OK(stream.getStream()->getActiveMicrophones(&activeMics));
        EXPECT_FALSE(activeMics.empty());
        for (const auto& mic : activeMics) {
            EXPECT_NE(micInfos.end(),
                      std::find_if(micInfos.begin(), micInfos.end(),
                                   [&](const auto& micInfo) { return micInfo.id == mic.id; }))
                    << "active microphone \"" << mic.id << "\" is not listed in "
                    << "microphone infos returned by the module: "
                    << ::android::internal::ToString(micInfos);
            EXPECT_NE(0UL, mic.channelMapping.size())
                    << "No channels specified for the microphone \"" << mic.id << "\"";
        }

        stream.TeardownPatch();
        // Now the port of the stream is not connected, check that there are no active microphones.
        std::vector<MicrophoneDynamicInfo> emptyMics;
        EXPECT_IS_OK(stream.getStream()->getActiveMicrophones(&emptyMics));
        EXPECT_TRUE(emptyMics.empty()) << "a stream on an unconnected port returns a "
                                          "non-empty list of active microphones";
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No input mix ports could be routed to built-in microphone devices";
    }
}

TEST_P(AudioStreamIn, MicrophoneDirection) {
    using MD = IStreamIn::MicrophoneDirection;
    constexpr bool connectedOnly = true;
    const auto ports = moduleConfig->getInputMixPorts(connectedOnly);
    if (ports.empty()) {
        GTEST_SKIP() << "No input mix ports for attached devices";
    }
    bool isSupported = false, atLeastOnePort = false;
    for (const auto& port : ports) {
        SCOPED_TRACE(port.toString());
        StreamFixture<IStreamIn> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(), port,
                                                             connectedOnly));
        if (!stream.skipTestReason().empty()) continue;
        atLeastOnePort = true;
        EXPECT_NO_FATAL_FAILURE(
                TestAccessors<MD>(stream.getStream(), &IStreamIn::getMicrophoneDirection,
                                  &IStreamIn::setMicrophoneDirection,
                                  std::vector<MD>(enum_range<MD>().begin(), enum_range<MD>().end()),
                                  {}, &isSupported));
        if (!isSupported) break;
    }
    if (!isSupported) {
        GTEST_SKIP() << "Microphone direction is not supported";
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No input mix ports could be routed to built-in microphone devices";
    }
}

TEST_P(AudioStreamIn, MicrophoneFieldDimension) {
    constexpr bool connectedOnly = true;
    const auto ports = moduleConfig->getInputMixPorts(connectedOnly);
    if (ports.empty()) {
        GTEST_SKIP() << "No input mix ports for attached devices";
    }
    bool isSupported = false, atLeastOnePort = false;
    for (const auto& port : ports) {
        SCOPED_TRACE(port.toString());
        StreamFixture<IStreamIn> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(), port,
                                                             connectedOnly));
        if (!stream.skipTestReason().empty()) continue;
        atLeastOnePort = true;
        EXPECT_NO_FATAL_FAILURE(TestAccessors<float>(
                stream.getStream(), &IStreamIn::getMicrophoneFieldDimension,
                &IStreamIn::setMicrophoneFieldDimension,
                {IStreamIn::MIC_FIELD_DIMENSION_WIDE_ANGLE,
                 IStreamIn::MIC_FIELD_DIMENSION_WIDE_ANGLE / 2.0f,
                 IStreamIn::MIC_FIELD_DIMENSION_NO_ZOOM,
                 IStreamIn::MIC_FIELD_DIMENSION_MAX_ZOOM / 2.0f,
                 IStreamIn::MIC_FIELD_DIMENSION_MAX_ZOOM},
                {IStreamIn::MIC_FIELD_DIMENSION_WIDE_ANGLE * 2,
                 IStreamIn::MIC_FIELD_DIMENSION_MAX_ZOOM * 2,
                 IStreamIn::MIC_FIELD_DIMENSION_WIDE_ANGLE * 1.1f,
                 IStreamIn::MIC_FIELD_DIMENSION_MAX_ZOOM * 1.1f, -INFINITY, INFINITY, -NAN, NAN},
                &isSupported));
        if (!isSupported) break;
    }
    if (!isSupported) {
        GTEST_SKIP() << "Microphone direction is not supported";
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No input mix ports could be routed to built-in microphone devices";
    }
}

TEST_P(AudioStreamOut, OpenTwicePrimary) {
    const auto mixPorts =
            moduleConfig->getPrimaryMixPorts(true /*connectedOnly*/, true /*singlePort*/);
    if (mixPorts.empty()) {
        GTEST_SKIP() << "No primary mix port which could be routed to attached devices";
    }
    const auto portConfig = moduleConfig->getSingleConfigForMixPort(false, *mixPorts.begin());
    ASSERT_TRUE(portConfig.has_value()) << "No profiles specified for the primary mix port";
    EXPECT_NO_FATAL_FAILURE(OpenTwiceSamePortConfigImpl(portConfig.value()));
}

TEST_P(AudioStreamOut, RequireOffloadInfo) {
    constexpr bool connectedOnly = true;
    const auto offloadMixPorts =
            moduleConfig->getOffloadMixPorts(connectedOnly, true /*singlePort*/);
    if (offloadMixPorts.empty()) {
        GTEST_SKIP()
                << "No mix port for compressed offload that could be routed to attached devices";
    }
    StreamFixture<IStreamOut> stream;
    ASSERT_NO_FATAL_FAILURE(stream.SetUpPortConfigForMixPortOrConfig(
            module.get(), moduleConfig.get(), *offloadMixPorts.begin(), connectedOnly));
    if (auto reason = stream.skipTestReason(); !reason.empty()) {
        GTEST_SKIP() << reason;
    }
    const auto portConfig = stream.getPortConfig();
    StreamDescriptor descriptor;
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamArguments args;
    args.portConfigId = portConfig.id;
    args.sourceMetadata = GenerateSourceMetadata(portConfig);
    args.bufferSizeFrames = kDefaultLargeBufferSizeFrames;
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamReturn ret;
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->openOutputStream(args, &ret))
            << "when no offload info is provided for a compressed offload mix port";
    if (ret.stream != nullptr) {
        (void)WithStream<IStreamOut>::callClose(ret.stream);
    }
}

TEST_P(AudioStreamOut, RequireAsyncCallback) {
    constexpr bool connectedOnly = true;
    const auto nonBlockingMixPorts =
            moduleConfig->getNonBlockingMixPorts(connectedOnly, true /*singlePort*/);
    if (nonBlockingMixPorts.empty()) {
        GTEST_SKIP()
                << "No mix port for non-blocking output that could be routed to attached devices";
    }
    StreamFixture<IStreamOut> stream;
    ASSERT_NO_FATAL_FAILURE(stream.SetUpPortConfigForMixPortOrConfig(
            module.get(), moduleConfig.get(), *nonBlockingMixPorts.begin(), connectedOnly));
    if (auto reason = stream.skipTestReason(); !reason.empty()) {
        GTEST_SKIP() << reason;
    }
    const auto portConfig = stream.getPortConfig();
    StreamDescriptor descriptor;
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamArguments args;
    args.portConfigId = portConfig.id;
    args.sourceMetadata = GenerateSourceMetadata(portConfig);
    args.offloadInfo = generateOffloadInfoIfNeeded(portConfig);
    args.bufferSizeFrames = stream.getPatch().minimumStreamBufferSizeFrames;
    aidl::android::hardware::audio::core::IModule::OpenOutputStreamReturn ret;
    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->openOutputStream(args, &ret))
            << "when no async callback is provided for a non-blocking mix port";
    if (ret.stream != nullptr) {
        (void)WithStream<IStreamOut>::callClose(ret.stream);
    }
}

TEST_P(AudioStreamOut, AudioDescriptionMixLevel) {
    constexpr bool connectedOnly = true;
    const auto ports = moduleConfig->getOutputMixPorts(connectedOnly);
    if (ports.empty()) {
        GTEST_SKIP() << "No output mix ports for attached devices";
    }
    bool atLeastOneSupports = false, atLeastOnePort = false;
    for (const auto& port : ports) {
        SCOPED_TRACE(port.toString());
        StreamFixture<IStreamOut> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(), port,
                                                             connectedOnly));
        if (!stream.skipTestReason().empty()) continue;
        atLeastOnePort = true;
        bool isSupported = false;
        EXPECT_NO_FATAL_FAILURE(
                TestAccessors<float>(stream.getStream(), &IStreamOut::getAudioDescriptionMixLevel,
                                     &IStreamOut::setAudioDescriptionMixLevel,
                                     {IStreamOut::AUDIO_DESCRIPTION_MIX_LEVEL_MAX,
                                      IStreamOut::AUDIO_DESCRIPTION_MIX_LEVEL_MAX - 1, 0,
                                      -INFINITY /*IStreamOut::AUDIO_DESCRIPTION_MIX_LEVEL_MIN*/},
                                     {IStreamOut::AUDIO_DESCRIPTION_MIX_LEVEL_MAX * 2,
                                      IStreamOut::AUDIO_DESCRIPTION_MIX_LEVEL_MAX * 1.1f},
                                     &isSupported));
        if (isSupported) atLeastOneSupports = true;
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No output mix ports could be routed to devices";
    }
    if (!atLeastOneSupports) {
        GTEST_SKIP() << "Audio description mix level is not supported";
    }
}

TEST_P(AudioStreamOut, DualMonoMode) {
    constexpr bool connectedOnly = true;
    const auto ports = moduleConfig->getOutputMixPorts(connectedOnly);
    if (ports.empty()) {
        GTEST_SKIP() << "No output mix ports for attached devices";
    }
    bool atLeastOneSupports = false, atLeastOnePort = false;
    for (const auto& port : ports) {
        SCOPED_TRACE(port.toString());
        StreamFixture<IStreamOut> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(), port,
                                                             connectedOnly));
        if (!stream.skipTestReason().empty()) continue;
        atLeastOnePort = true;
        bool isSupported = false;
        EXPECT_NO_FATAL_FAILURE(TestAccessors<AudioDualMonoMode>(
                stream.getStream(), &IStreamOut::getDualMonoMode, &IStreamOut::setDualMonoMode,
                std::vector<AudioDualMonoMode>(enum_range<AudioDualMonoMode>().begin(),
                                               enum_range<AudioDualMonoMode>().end()),
                {}, &isSupported));
        if (isSupported) atLeastOneSupports = true;
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No output mix ports could be routed to devices";
    }
    if (!atLeastOneSupports) {
        GTEST_SKIP() << "Audio dual mono mode is not supported";
    }
}

TEST_P(AudioStreamOut, LatencyMode) {
    constexpr bool connectedOnly = true;
    const auto ports = moduleConfig->getOutputMixPorts(connectedOnly);
    if (ports.empty()) {
        GTEST_SKIP() << "No output mix ports for attached devices";
    }
    bool atLeastOneSupports = false, atLeastOnePort = false;
    for (const auto& port : ports) {
        SCOPED_TRACE(port.toString());
        StreamFixture<IStreamOut> stream;
        ASSERT_NO_FATAL_FAILURE(stream.SetUpStreamForMixPort(module.get(), moduleConfig.get(), port,
                                                             connectedOnly));
        if (!stream.skipTestReason().empty()) continue;
        atLeastOnePort = true;
        std::vector<AudioLatencyMode> supportedModes;
        ndk::ScopedAStatus status = stream.getStream()->getRecommendedLatencyModes(&supportedModes);
        if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) continue;
        atLeastOneSupports = true;
        if (!status.isOk()) {
            ADD_FAILURE() << "When latency modes are supported, getRecommendedLatencyModes "
                          << "must succeed on a non-closed stream, but it failed with " << status;
            continue;
        }
        std::set<AudioLatencyMode> unsupportedModes(enum_range<AudioLatencyMode>().begin(),
                                                    enum_range<AudioLatencyMode>().end());
        for (const auto mode : supportedModes) {
            unsupportedModes.erase(mode);
            ndk::ScopedAStatus status = stream.getStream()->setLatencyMode(mode);
            if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
                ADD_FAILURE() << "When latency modes are supported, both getRecommendedLatencyModes"
                              << " and setLatencyMode must be supported";
            }
            EXPECT_IS_OK(status) << "Setting of supported latency mode must succeed";
        }
        for (const auto mode : unsupportedModes) {
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, stream.getStream()->setLatencyMode(mode));
        }
    }
    if (!atLeastOneSupports) {
        GTEST_SKIP() << "Audio latency modes are not supported";
    }
    if (!atLeastOnePort) {
        GTEST_SKIP() << "No output mix ports could be routed to devices";
    }
}

TEST_P(AudioStreamOut, PlaybackRate) {
    static const auto kStatuses = {EX_NONE, EX_UNSUPPORTED_OPERATION};
    const auto offloadMixPorts =
            moduleConfig->getOffloadMixPorts(true /*connectedOnly*/, false /*singlePort*/);
    if (offloadMixPorts.empty()) {
        GTEST_SKIP()
                << "No mix port for compressed offload that could be routed to attached devices";
    }
    ndk::ScopedAStatus status;
    IModule::SupportedPlaybackRateFactors factors;
    EXPECT_STATUS(kStatuses, status = module.get()->getSupportedPlaybackRateFactors(&factors));
    if (status.getExceptionCode() == EX_UNSUPPORTED_OPERATION) {
        GTEST_SKIP() << "Audio playback rate configuration is not supported";
    }
    EXPECT_LE(factors.minSpeed, factors.maxSpeed);
    EXPECT_LE(factors.minPitch, factors.maxPitch);
    EXPECT_LE(factors.minSpeed, 1.0f);
    EXPECT_GE(factors.maxSpeed, 1.0f);
    EXPECT_LE(factors.minPitch, 1.0f);
    EXPECT_GE(factors.maxPitch, 1.0f);
    constexpr auto tsDefault = AudioPlaybackRate::TimestretchMode::DEFAULT;
    constexpr auto tsVoice = AudioPlaybackRate::TimestretchMode::VOICE;
    constexpr auto fbFail = AudioPlaybackRate::TimestretchFallbackMode::FAIL;
    constexpr auto fbMute = AudioPlaybackRate::TimestretchFallbackMode::MUTE;
    const std::vector<AudioPlaybackRate> validValues = {
            AudioPlaybackRate{1.0f, 1.0f, tsDefault, fbFail},
            AudioPlaybackRate{1.0f, 1.0f, tsDefault, fbMute},
            AudioPlaybackRate{factors.maxSpeed, factors.maxPitch, tsDefault, fbMute},
            AudioPlaybackRate{factors.minSpeed, factors.minPitch, tsDefault, fbMute},
            AudioPlaybackRate{1.0f, 1.0f, tsVoice, fbMute},
            AudioPlaybackRate{1.0f, 1.0f, tsVoice, fbFail},
            AudioPlaybackRate{factors.maxSpeed, factors.maxPitch, tsVoice, fbMute},
            AudioPlaybackRate{factors.minSpeed, factors.minPitch, tsVoice, fbMute},
    };
    const std::vector<AudioPlaybackRate> invalidValues = {
            AudioPlaybackRate{factors.maxSpeed, factors.maxPitch * 2, tsDefault, fbFail},
            AudioPlaybackRate{factors.maxSpeed * 2, factors.maxPitch, tsDefault, fbFail},
            AudioPlaybackRate{factors.minSpeed, factors.minPitch / 2, tsDefault, fbFail},
            AudioPlaybackRate{factors.minSpeed / 2, factors.minPitch, tsDefault, fbFail},
            AudioPlaybackRate{factors.maxSpeed, factors.maxPitch * 2, tsVoice, fbFail},
            AudioPlaybackRate{factors.maxSpeed * 2, factors.maxPitch, tsVoice, fbFail},
            AudioPlaybackRate{factors.minSpeed, factors.minPitch / 2, tsVoice, fbFail},
            AudioPlaybackRate{factors.minSpeed / 2, factors.minPitch, tsVoice, fbFail},
            AudioPlaybackRate{1.0f, 1.0f, tsDefault,
                              AudioPlaybackRate::TimestretchFallbackMode::SYS_RESERVED_CUT_REPEAT},
            AudioPlaybackRate{1.0f, 1.0f, tsDefault,
                              AudioPlaybackRate::TimestretchFallbackMode::SYS_RESERVED_DEFAULT},
    };
    const std::vector<AudioPlaybackRate> ambivalentValues = {
            // Out of range speed / pitch values may optionally be rejected if the fallback mode
            // is "mute".
            AudioPlaybackRate{factors.maxSpeed * 2, factors.maxPitch * 2, tsDefault, fbMute},
            AudioPlaybackRate{factors.minSpeed / 2, factors.minPitch / 2, tsDefault, fbMute},
            AudioPlaybackRate{factors.maxSpeed * 2, factors.maxPitch * 2, tsVoice, fbMute},
            AudioPlaybackRate{factors.minSpeed / 2, factors.minPitch / 2, tsVoice, fbMute},
    };
    bool atLeastOneSupports = false;
    for (const auto& port : offloadMixPorts) {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(false, port);
        ASSERT_TRUE(portConfig.has_value()) << "No profiles specified for output mix port";
        WithStream<IStreamOut> stream(portConfig.value());
        ASSERT_NO_FATAL_FAILURE(stream.SetUp(module.get(), kDefaultLargeBufferSizeFrames));
        bool isSupported = false;
        EXPECT_NO_FATAL_FAILURE(TestAccessors<AudioPlaybackRate>(
                stream.get(), &IStreamOut::getPlaybackRateParameters,
                &IStreamOut::setPlaybackRateParameters, validValues, invalidValues, &isSupported,
                &ambivalentValues));
        if (isSupported) atLeastOneSupports = true;
    }
    if (!atLeastOneSupports) {
        GTEST_SKIP() << "Audio playback rate configuration is not supported";
    }
}

TEST_P(AudioStreamOut, SelectPresentation) {
    static const auto kStatuses = {EX_ILLEGAL_ARGUMENT, EX_UNSUPPORTED_OPERATION};
    const auto offloadMixPorts =
            moduleConfig->getOffloadMixPorts(true /*connectedOnly*/, false /*singlePort*/);
    if (offloadMixPorts.empty()) {
        GTEST_SKIP()
                << "No mix port for compressed offload that could be routed to attached devices";
    }
    bool atLeastOneSupports = false;
    for (const auto& port : offloadMixPorts) {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(false, port);
        ASSERT_TRUE(portConfig.has_value()) << "No profiles specified for output mix port";
        WithStream<IStreamOut> stream(portConfig.value());
        ASSERT_NO_FATAL_FAILURE(stream.SetUp(module.get(), kDefaultLargeBufferSizeFrames));
        ndk::ScopedAStatus status;
        EXPECT_STATUS(kStatuses, status = stream.get()->selectPresentation(0, 0));
        if (status.getExceptionCode() != EX_UNSUPPORTED_OPERATION) atLeastOneSupports = true;
    }
    if (!atLeastOneSupports) {
        GTEST_SKIP() << "Presentation selection is not supported";
    }
}

TEST_P(AudioStreamOut, UpdateOffloadMetadata) {
    const auto offloadMixPorts =
            moduleConfig->getOffloadMixPorts(true /*connectedOnly*/, false /*singlePort*/);
    if (offloadMixPorts.empty()) {
        GTEST_SKIP()
                << "No mix port for compressed offload that could be routed to attached devices";
    }
    for (const auto& port : offloadMixPorts) {
        const auto portConfig = moduleConfig->getSingleConfigForMixPort(false, port);
        ASSERT_TRUE(portConfig.has_value()) << "No profiles specified for output mix port";
        WithStream<IStreamOut> stream(portConfig.value());
        ASSERT_NO_FATAL_FAILURE(stream.SetUp(module.get(), kDefaultLargeBufferSizeFrames));
        AudioOffloadMetadata validMetadata{
                .sampleRate = portConfig.value().sampleRate.value().value,
                .channelMask = portConfig.value().channelMask.value(),
                .averageBitRatePerSecond = 256000,
                .delayFrames = 0,
                .paddingFrames = 0};
        EXPECT_IS_OK(stream.get()->updateOffloadMetadata(validMetadata));
        AudioOffloadMetadata invalidMetadata{.sampleRate = -1,
                                             .averageBitRatePerSecond = -1,
                                             .delayFrames = -1,
                                             .paddingFrames = -1};
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, stream.get()->updateOffloadMetadata(invalidMetadata));
    }
}

enum {
    NAMED_CMD_NAME,
    NAMED_CMD_MIN_INTERFACE_VERSION,
    NAMED_CMD_FEATURE_PROPERTY,
    NAMED_CMD_DELAY_MS,
    NAMED_CMD_STREAM_TYPE,
    NAMED_CMD_CMDS,
    NAMED_CMD_VALIDATE_POS_INCREASE
};
enum class StreamTypeFilter { ANY, SYNC, ASYNC, OFFLOAD };
using NamedCommandSequence =
        std::tuple<std::string, int /*minInterfaceVersion*/, std::string /*featureProperty*/,
                   int /*cmdDelayMs*/, StreamTypeFilter, std::shared_ptr<StateSequence>,
                   bool /*validatePositionIncrease*/>;
enum { PARAM_MODULE_NAME, PARAM_CMD_SEQ, PARAM_SETUP_SEQ };
using StreamIoTestParameters =
        std::tuple<std::string /*moduleName*/, NamedCommandSequence, bool /*useSetupSequence2*/>;
template <typename Stream>
class AudioStreamIo : public AudioCoreModuleBase,
                      public testing::TestWithParam<StreamIoTestParameters> {
  public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(std::get<PARAM_MODULE_NAME>(GetParam())));
        ASSERT_GE(aidlVersion, kAidlVersion1);
        const int minVersion =
                std::get<NAMED_CMD_MIN_INTERFACE_VERSION>(std::get<PARAM_CMD_SEQ>(GetParam()));
        if (aidlVersion < minVersion) {
            GTEST_SKIP() << "Skip for audio HAL version lower than " << minVersion;
        }
        // When an associated feature property is defined, need to check that either that the HAL
        // exposes this property, or it's of the version 'NAMED_CMD_MIN_INTERFACE_VERSION' + 1
        // which must have this functionality implemented by default.
        if (const std::string featureProperty =
                    std::get<NAMED_CMD_FEATURE_PROPERTY>(std::get<PARAM_CMD_SEQ>(GetParam()));
            !featureProperty.empty() && aidlVersion < (minVersion + 1)) {
            std::vector<VendorParameter> parameters;
            ScopedAStatus result = module->getVendorParameters({featureProperty}, &parameters);
            if (!result.isOk() || parameters.size() != 1) {
                GTEST_SKIP() << "Skip as audio HAL does not support feature \"" << featureProperty
                             << "\"";
            }
        }
        ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    }

    void Run() {
        const auto allPortConfigs =
                moduleConfig->getPortConfigsForMixPorts(IOTraits<Stream>::is_input);
        if (allPortConfigs.empty()) {
            GTEST_SKIP() << "No mix ports have attached devices";
        }
        const auto& commandsAndStates =
                std::get<NAMED_CMD_CMDS>(std::get<PARAM_CMD_SEQ>(GetParam()));
        const bool validatePositionIncrease =
                std::get<NAMED_CMD_VALIDATE_POS_INCREASE>(std::get<PARAM_CMD_SEQ>(GetParam()));
        auto runStreamIoCommands = [&](const AudioPortConfig& portConfig) {
            if (!std::get<PARAM_SETUP_SEQ>(GetParam())) {
                ASSERT_NO_FATAL_FAILURE(RunStreamIoCommandsImplSeq1(portConfig, commandsAndStates,
                                                                    validatePositionIncrease));
            } else {
                ASSERT_NO_FATAL_FAILURE(RunStreamIoCommandsImplSeq2(portConfig, commandsAndStates,
                                                                    validatePositionIncrease));
            }
        };

        for (const auto& portConfig : allPortConfigs) {
            auto port = moduleConfig->getPort(portConfig.portId);
            ASSERT_TRUE(port.has_value());
            SCOPED_TRACE(port->toString());
            SCOPED_TRACE(portConfig.toString());
            if (skipStreamIoTestForMixPortConfig(portConfig)) continue;
            const bool isNonBlocking =
                    IOTraits<Stream>::is_input
                            ? false
                            :
                            // TODO: Uncomment when support for asynchronous input is implemented.
                            /*isBitPositionFlagSet(
                              portConfig.flags.value().template get<AudioIoFlags::Tag::input>(),
                              AudioInputFlags::NON_BLOCKING) :*/
                            isBitPositionFlagSet(portConfig.flags.value()
                                                         .template get<AudioIoFlags::Tag::output>(),
                                                 AudioOutputFlags::NON_BLOCKING);
            const bool isOffload =
                    IOTraits<Stream>::is_input
                            ? false
                            : isBitPositionFlagSet(
                                      portConfig.flags.value()
                                              .template get<AudioIoFlags::Tag::output>(),
                                      AudioOutputFlags::COMPRESS_OFFLOAD);
            if (auto streamType =
                        std::get<NAMED_CMD_STREAM_TYPE>(std::get<PARAM_CMD_SEQ>(GetParam()));
                (isNonBlocking && streamType == StreamTypeFilter::SYNC) ||
                (!isNonBlocking && streamType == StreamTypeFilter::ASYNC) ||
                (!isOffload && streamType == StreamTypeFilter::OFFLOAD)) {
                continue;
            }
            WithDebugFlags delayTransientStates = WithDebugFlags::createNested(*debug);
            delayTransientStates.flags().streamTransientStateDelayMs =
                    std::get<NAMED_CMD_DELAY_MS>(std::get<PARAM_CMD_SEQ>(GetParam()));
            ASSERT_NO_FATAL_FAILURE(delayTransientStates.SetUp(module.get()));
            ASSERT_NO_FATAL_FAILURE(runStreamIoCommands(portConfig));
            if (isNonBlocking) {
                // Also try running the same sequence with "aosp.forceTransientBurst" set.
                // This will only work with the default implementation. When it works, the stream
                // tries always to move to the 'TRANSFERRING' state after a burst.
                // This helps to check more paths for our test scenarios.
                WithModuleParameter forceTransientBurst("aosp.forceTransientBurst", Boolean{true});
                if (forceTransientBurst.SetUpNoChecks(module.get(), true /*failureExpected*/)
                            .isOk()) {
                    ASSERT_NO_FATAL_FAILURE(runStreamIoCommands(portConfig));
                }
            } else if (!IOTraits<Stream>::is_input) {
                // Also try running the same sequence with "aosp.forceSynchronousDrain" set.
                // This will only work with the default implementation. When it works, the stream
                // tries always to move to the 'IDLE' state after a drain.
                // This helps to check more paths for our test scenarios.
                WithModuleParameter forceSynchronousDrain("aosp.forceSynchronousDrain",
                                                          Boolean{true});
                if (forceSynchronousDrain.SetUpNoChecks(module.get(), true /*failureExpected*/)
                            .isOk()) {
                    ASSERT_NO_FATAL_FAILURE(runStreamIoCommands(portConfig));
                }
            }
        }
    }

    bool ValidatePosition(const AudioDevice& device) {
        return !isTelephonyDeviceType(device.type.type);
    }

    // Set up a patch first, then open a stream.
    void RunStreamIoCommandsImplSeq1(const AudioPortConfig& portConfig,
                                     std::shared_ptr<StateSequence> commandsAndStates,
                                     bool validatePositionIncrease) {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(
                stream.SetUpStreamForMixPortConfig(module.get(), moduleConfig.get(), portConfig));
        if (skipStreamIoTestForDevice(stream.getDevice())) return;
        if (skipStreamIoTestForStream(stream.getStreamContext(), stream.getStreamWorkerMethods())) {
            return;
        }
        ASSERT_EQ("", stream.skipTestReason());
        StreamLogicDefaultDriver driver(commandsAndStates,
                                        stream.getStreamContext()->getFrameSizeBytes(),
                                        stream.getStreamContext()->isMmapped());
        typename IOTraits<Stream>::Worker worker(*stream.getStreamContext(), &driver,
                                                 stream.getStreamWorkerMethods(),
                                                 stream.getStreamEventReceiver());

        LOG(DEBUG) << __func__ << ": starting worker...";
        ASSERT_TRUE(worker.start());
        LOG(DEBUG) << __func__ << ": joining worker...";
        worker.join();
        EXPECT_FALSE(worker.hasError()) << worker.getError();
        EXPECT_EQ("", driver.getUnexpectedStateTransition());
        if (ValidatePosition(stream.getDevice())) {
            if (validatePositionIncrease) {
                EXPECT_TRUE(driver.hasObservablePositionIncrease());
                EXPECT_TRUE(driver.hasHardwarePositionIncrease());
            }
            EXPECT_FALSE(driver.hasObservableRetrogradePosition());
            EXPECT_FALSE(driver.hasHardwareRetrogradePosition());
        }
    }

    // Open a stream, then set up a patch for it. Since first it is needed to get
    // the minimum buffer size, a preliminary patch is set up, then removed.
    void RunStreamIoCommandsImplSeq2(const AudioPortConfig& portConfig,
                                     std::shared_ptr<StateSequence> commandsAndStates,
                                     bool validatePositionIncrease) {
        StreamFixture<Stream> stream;
        ASSERT_NO_FATAL_FAILURE(
                stream.SetUpPatchForMixPortConfig(module.get(), moduleConfig.get(), portConfig));
        if (skipStreamIoTestForDevice(stream.getDevice())) return;
        ASSERT_EQ("", stream.skipTestReason());
        ASSERT_NO_FATAL_FAILURE(stream.TeardownPatchSetUpStream(module.get()));
        if (skipStreamIoTestForStream(stream.getStreamContext(), stream.getStreamWorkerMethods())) {
            return;
        }
        StreamLogicDefaultDriver driver(commandsAndStates,
                                        stream.getStreamContext()->getFrameSizeBytes(),
                                        stream.getStreamContext()->isMmapped());
        typename IOTraits<Stream>::Worker worker(*stream.getStreamContext(), &driver,
                                                 stream.getStreamWorkerMethods(),
                                                 stream.getStreamEventReceiver());
        ASSERT_NO_FATAL_FAILURE(stream.ReconnectPatch(module.get()));

        LOG(DEBUG) << __func__ << ": starting worker...";
        ASSERT_TRUE(worker.start());
        LOG(DEBUG) << __func__ << ": joining worker...";
        worker.join();
        EXPECT_FALSE(worker.hasError()) << worker.getError();
        EXPECT_EQ("", driver.getUnexpectedStateTransition());
        if (ValidatePosition(stream.getDevice())) {
            if (validatePositionIncrease) {
                EXPECT_TRUE(driver.hasObservablePositionIncrease());
                EXPECT_TRUE(driver.hasHardwarePositionIncrease());
            }
            EXPECT_FALSE(driver.hasObservableRetrogradePosition());
            EXPECT_FALSE(driver.hasHardwareRetrogradePosition());
        }
    }
};
using AudioStreamIoIn = AudioStreamIo<IStreamIn>;
using AudioStreamIoOut = AudioStreamIo<IStreamOut>;

#define TEST_IN_AND_OUT_STREAM_IO(method_name)  \
    TEST_P(AudioStreamIoIn, method_name) {      \
        ASSERT_NO_FATAL_FAILURE(method_name()); \
    }                                           \
    TEST_P(AudioStreamIoOut, method_name) {     \
        ASSERT_NO_FATAL_FAILURE(method_name()); \
    }

TEST_IN_AND_OUT_STREAM_IO(Run);

// Tests specific to audio patches. The fixure class is named 'AudioModulePatch'
// to avoid clashing with 'AudioPatch' class.
class AudioModulePatch : public AudioCoreModule {
  public:
    static std::string direction(bool isInput, bool capitalize) {
        return isInput ? (capitalize ? "Input" : "input") : (capitalize ? "Output" : "output");
    }

    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(AudioCoreModule::SetUp());
        ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    }

    void SetInvalidPatchHelper(int32_t expectedException, const std::vector<int32_t>& sources,
                               const std::vector<int32_t>& sinks) {
        AudioPatch patch;
        patch.sourcePortConfigIds = sources;
        patch.sinkPortConfigIds = sinks;
        ASSERT_STATUS(expectedException, module->setAudioPatch(patch, &patch))
                << "patch source ids: " << android::internal::ToString(sources)
                << "; sink ids: " << android::internal::ToString(sinks);
    }

    void ResetPortConfigUsedByPatch(bool isInput) {
        auto srcSinkGroups = moduleConfig->getRoutableSrcSinkGroups(isInput);
        if (srcSinkGroups.empty()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        auto srcSinkGroup = *srcSinkGroups.begin();
        auto srcSink = *srcSinkGroup.second.begin();
        WithAudioPatch patch(srcSink.first, srcSink.second);
        ASSERT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
        std::vector<int32_t> sourceAndSinkPortConfigIds(patch.get().sourcePortConfigIds);
        sourceAndSinkPortConfigIds.insert(sourceAndSinkPortConfigIds.end(),
                                          patch.get().sinkPortConfigIds.begin(),
                                          patch.get().sinkPortConfigIds.end());
        for (const auto portConfigId : sourceAndSinkPortConfigIds) {
            EXPECT_STATUS(EX_ILLEGAL_STATE, module->resetAudioPortConfig(portConfigId))
                    << "port config ID " << portConfigId;
        }
    }

    void SetInvalidPatch(bool isInput) {
        auto srcSinkPair = moduleConfig->getRoutableSrcSinkPair(isInput);
        if (!srcSinkPair.has_value()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        WithAudioPortConfig srcPortConfig(srcSinkPair.value().first);
        ASSERT_NO_FATAL_FAILURE(srcPortConfig.SetUp(module.get()));
        WithAudioPortConfig sinkPortConfig(srcSinkPair.value().second);
        ASSERT_NO_FATAL_FAILURE(sinkPortConfig.SetUp(module.get()));
        {  // Check that the pair can actually be used for setting up a patch.
            WithAudioPatch patch(srcPortConfig.get(), sinkPortConfig.get());
            ASSERT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
        }
        EXPECT_NO_FATAL_FAILURE(
                SetInvalidPatchHelper(EX_ILLEGAL_ARGUMENT, {}, {sinkPortConfig.getId()}));
        EXPECT_NO_FATAL_FAILURE(SetInvalidPatchHelper(
                EX_ILLEGAL_ARGUMENT, {srcPortConfig.getId(), srcPortConfig.getId()},
                {sinkPortConfig.getId()}));
        EXPECT_NO_FATAL_FAILURE(
                SetInvalidPatchHelper(EX_ILLEGAL_ARGUMENT, {srcPortConfig.getId()}, {}));
        EXPECT_NO_FATAL_FAILURE(
                SetInvalidPatchHelper(EX_ILLEGAL_ARGUMENT, {srcPortConfig.getId()},
                                      {sinkPortConfig.getId(), sinkPortConfig.getId()}));

        std::set<int32_t> portConfigIds;
        ASSERT_NO_FATAL_FAILURE(GetAllPortConfigIds(&portConfigIds));
        for (const auto portConfigId : GetNonExistentIds(portConfigIds)) {
            EXPECT_NO_FATAL_FAILURE(SetInvalidPatchHelper(EX_ILLEGAL_ARGUMENT, {portConfigId},
                                                          {sinkPortConfig.getId()}));
            EXPECT_NO_FATAL_FAILURE(SetInvalidPatchHelper(EX_ILLEGAL_ARGUMENT,
                                                          {srcPortConfig.getId()}, {portConfigId}));
        }
    }

    void SetNonRoutablePatch(bool isInput) {
        auto srcSinkPair = moduleConfig->getNonRoutableSrcSinkPair(isInput);
        if (!srcSinkPair.has_value()) {
            GTEST_SKIP() << "All possible source/sink pairs are routable";
        }
        WithAudioPatch patch(srcSinkPair.value().first, srcSinkPair.value().second);
        ASSERT_NO_FATAL_FAILURE(patch.SetUpPortConfigs(module.get()));
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, patch.SetUpNoChecks(module.get()))
                << "when setting up a patch from " << srcSinkPair.value().first.toString() << " to "
                << srcSinkPair.value().second.toString() << " that does not have a route";
    }

    void SetPatch(bool isInput) {
        auto srcSinkGroups = moduleConfig->getRoutableSrcSinkGroups(isInput);
        if (srcSinkGroups.empty()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        for (const auto& srcSinkGroup : srcSinkGroups) {
            const auto& route = srcSinkGroup.first;
            std::vector<std::unique_ptr<WithAudioPatch>> patches;
            for (const auto& srcSink : srcSinkGroup.second) {
                if (!route.isExclusive) {
                    patches.push_back(
                            std::make_unique<WithAudioPatch>(srcSink.first, srcSink.second));
                    EXPECT_NO_FATAL_FAILURE(patches[patches.size() - 1]->SetUp(module.get()));
                    EXPECT_NO_FATAL_FAILURE(
                            patches[patches.size() - 1]->VerifyAgainstAllPatches(module.get()));
                } else {
                    WithAudioPatch patch(srcSink.first, srcSink.second);
                    EXPECT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
                    EXPECT_NO_FATAL_FAILURE(patch.VerifyAgainstAllPatches(module.get()));
                }
            }
        }
    }

    void UpdatePatch(bool isInput) {
        auto srcSinkGroups = moduleConfig->getRoutableSrcSinkGroups(isInput);
        if (srcSinkGroups.empty()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        for (const auto& srcSinkGroup : srcSinkGroups) {
            for (const auto& srcSink : srcSinkGroup.second) {
                WithAudioPatch patch(srcSink.first, srcSink.second);
                ASSERT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
                AudioPatch ignored;
                EXPECT_NO_FATAL_FAILURE(module->setAudioPatch(patch.get(), &ignored));
            }
        }
    }

    void UpdatePatchPorts(bool isInput) {
        auto srcSinkGroups = moduleConfig->getRoutableSrcSinkGroups(isInput);
        if (srcSinkGroups.empty()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        bool hasAtLeastOnePair = false;
        for (const auto& srcSinkGroup : srcSinkGroups) {
            const auto& srcSinks = srcSinkGroup.second;
            if (srcSinks.size() < 2) continue;
            hasAtLeastOnePair = true;
            const auto& pair1 = srcSinks[0];
            const auto& pair2 = srcSinks[1];
            WithAudioPatch patch(pair1.first, pair1.second);
            ASSERT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
            WithAudioPatch update(patch, pair2.first, pair2.second);
            EXPECT_NO_FATAL_FAILURE(update.SetUp(module.get()));
            EXPECT_NO_FATAL_FAILURE(update.VerifyAgainstAllPatches(module.get()));
        }
        if (!hasAtLeastOnePair) {
            GTEST_SKIP() << "No routes with multiple sources";
        }
    }

    void UpdateInvalidPatchId(bool isInput) {
        auto srcSinkGroups = moduleConfig->getRoutableSrcSinkGroups(isInput);
        if (srcSinkGroups.empty()) {
            GTEST_SKIP() << "No routes to any attached " << direction(isInput, false) << " devices";
        }
        // First, set up a patch to ensure that its settings are accepted.
        auto srcSinkGroup = *srcSinkGroups.begin();
        auto srcSink = *srcSinkGroup.second.begin();
        WithAudioPatch patch(srcSink.first, srcSink.second);
        ASSERT_NO_FATAL_FAILURE(patch.SetUp(module.get()));
        // Then use the same patch setting, except for having an invalid ID.
        std::set<int32_t> patchIds;
        ASSERT_NO_FATAL_FAILURE(GetAllPatchIds(&patchIds));
        for (const auto patchId : GetNonExistentIds(patchIds, false /*includeZero*/)) {
            AudioPatch patchWithNonExistendId = patch.get();
            patchWithNonExistendId.id = patchId;
            EXPECT_STATUS(EX_ILLEGAL_ARGUMENT,
                          module->setAudioPatch(patchWithNonExistendId, &patchWithNonExistendId))
                    << "patch ID " << patchId;
        }
    }
};

// Not all tests require both directions, so parametrization would require
// more abstractions.
#define TEST_PATCH_BOTH_DIRECTIONS(method_name)      \
    TEST_P(AudioModulePatch, method_name##Input) {   \
        ASSERT_NO_FATAL_FAILURE(method_name(true));  \
    }                                                \
    TEST_P(AudioModulePatch, method_name##Output) {  \
        ASSERT_NO_FATAL_FAILURE(method_name(false)); \
    }

TEST_PATCH_BOTH_DIRECTIONS(ResetPortConfigUsedByPatch);
TEST_PATCH_BOTH_DIRECTIONS(SetInvalidPatch);
TEST_PATCH_BOTH_DIRECTIONS(SetNonRoutablePatch);
TEST_PATCH_BOTH_DIRECTIONS(SetPatch);
TEST_PATCH_BOTH_DIRECTIONS(UpdateInvalidPatchId);
TEST_PATCH_BOTH_DIRECTIONS(UpdatePatch);
TEST_PATCH_BOTH_DIRECTIONS(UpdatePatchPorts);

TEST_P(AudioModulePatch, ResetInvalidPatchId) {
    std::set<int32_t> patchIds;
    ASSERT_NO_FATAL_FAILURE(GetAllPatchIds(&patchIds));
    for (const auto patchId : GetNonExistentIds(patchIds)) {
        EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, module->resetAudioPatch(patchId))
                << "patch ID " << patchId;
    }
}

class AudioCoreSoundDose : public AudioCoreModuleBase, public testing::TestWithParam<std::string> {
  public:
    class NoOpHalSoundDoseCallback : public ISoundDose::BnHalSoundDoseCallback {
      public:
        ndk::ScopedAStatus onMomentaryExposureWarning(float in_currentDbA,
                                                      const AudioDevice& in_audioDevice) override;
        ndk::ScopedAStatus onNewMelValues(
                const ISoundDose::IHalSoundDoseCallback::MelRecord& in_melRecord,
                const AudioDevice& in_audioDevice) override;
    };

    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam()));
        ASSERT_IS_OK(module->getSoundDose(&soundDose));
        callback = ndk::SharedRefBase::make<NoOpHalSoundDoseCallback>();
    }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownImpl()); }

    std::shared_ptr<ISoundDose> soundDose;
    std::shared_ptr<ISoundDose::IHalSoundDoseCallback> callback;
};

ndk::ScopedAStatus AudioCoreSoundDose::NoOpHalSoundDoseCallback::onMomentaryExposureWarning(
        float in_currentDbA, const AudioDevice& in_audioDevice) {
    // Do nothing
    (void)in_currentDbA;
    (void)in_audioDevice;
    LOG(INFO) << "NoOpHalSoundDoseCallback::onMomentaryExposureWarning called";

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus AudioCoreSoundDose::NoOpHalSoundDoseCallback::onNewMelValues(
        const ISoundDose::IHalSoundDoseCallback::MelRecord& in_melRecord,
        const AudioDevice& in_audioDevice) {
    // Do nothing
    (void)in_melRecord;
    (void)in_audioDevice;
    LOG(INFO) << "NoOpHalSoundDoseCallback::onNewMelValues called";

    return ndk::ScopedAStatus::ok();
}

// @VsrTest = VSR-5.5-002.001
TEST_P(AudioCoreSoundDose, SameInstance) {
    if (soundDose == nullptr) {
        GTEST_SKIP() << "SoundDose is not supported";
    }
    std::shared_ptr<ISoundDose> soundDose2;
    EXPECT_IS_OK(module->getSoundDose(&soundDose2));
    ASSERT_NE(nullptr, soundDose2.get());
    EXPECT_EQ(soundDose->asBinder(), soundDose2->asBinder())
            << "getSoundDose must return the same interface instance across invocations";
}

// @VsrTest = VSR-5.5-002.001
TEST_P(AudioCoreSoundDose, GetSetOutputRs2UpperBound) {
    if (soundDose == nullptr) {
        GTEST_SKIP() << "SoundDose is not supported";
    }

    bool isSupported = false;
    EXPECT_NO_FATAL_FAILURE(TestAccessors<float>(soundDose.get(),
                                                 &ISoundDose::getOutputRs2UpperBound,
                                                 &ISoundDose::setOutputRs2UpperBound,
                                                 /*validValues=*/{80.f, 90.f, 100.f},
                                                 /*invalidValues=*/{79.f, 101.f}, &isSupported));
    EXPECT_TRUE(isSupported) << "Getting/Setting RS2 upper bound must be supported";
}

// @VsrTest = VSR-5.5-002.001
TEST_P(AudioCoreSoundDose, CheckDefaultRs2UpperBound) {
    if (soundDose == nullptr) {
        GTEST_SKIP() << "SoundDose is not supported";
    }

    float rs2Value;
    ASSERT_IS_OK(soundDose->getOutputRs2UpperBound(&rs2Value));
    EXPECT_EQ(rs2Value, ISoundDose::DEFAULT_MAX_RS2);
}

// @VsrTest = VSR-5.5-002.001
TEST_P(AudioCoreSoundDose, RegisterSoundDoseCallbackTwiceThrowsException) {
    if (soundDose == nullptr) {
        GTEST_SKIP() << "SoundDose is not supported";
    }

    ASSERT_IS_OK(soundDose->registerSoundDoseCallback(callback));
    EXPECT_STATUS(EX_ILLEGAL_STATE, soundDose->registerSoundDoseCallback(callback))
            << "Registering sound dose callback twice should throw EX_ILLEGAL_STATE";
}

// @VsrTest = VSR-5.5-002.001
TEST_P(AudioCoreSoundDose, RegisterSoundDoseNullCallbackThrowsException) {
    if (soundDose == nullptr) {
        GTEST_SKIP() << "SoundDose is not supported";
    }

    EXPECT_STATUS(EX_ILLEGAL_ARGUMENT, soundDose->registerSoundDoseCallback(nullptr))
            << "Registering nullptr sound dose callback should throw EX_ILLEGAL_ARGUMENT";
}

INSTANTIATE_TEST_SUITE_P(AudioCoreModuleTest, AudioCoreModule,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreModule);
INSTANTIATE_TEST_SUITE_P(AudioCoreBluetoothTest, AudioCoreBluetooth,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreBluetooth);
INSTANTIATE_TEST_SUITE_P(AudioCoreBluetoothA2dpTest, AudioCoreBluetoothA2dp,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreBluetoothA2dp);
INSTANTIATE_TEST_SUITE_P(AudioCoreBluetoothLeTest, AudioCoreBluetoothLe,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreBluetoothLe);
INSTANTIATE_TEST_SUITE_P(AudioCoreTelephonyTest, AudioCoreTelephony,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreTelephony);
INSTANTIATE_TEST_SUITE_P(AudioStreamInTest, AudioStreamIn,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioStreamIn);
INSTANTIATE_TEST_SUITE_P(AudioStreamOutTest, AudioStreamOut,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioStreamOut);
INSTANTIATE_TEST_SUITE_P(AudioCoreSoundDoseTest, AudioCoreSoundDose,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioCoreSoundDose);

// This is the value used in test sequences for which the test needs to ensure
// that the HAL stays in a transient state long enough to receive the next command.
static const int kStreamTransientStateTransitionDelayMs = 3000;

// TODO: Add async test cases for input once it is implemented.

// Allow optional routing via the TRANSFERRING state on bursts.
StateDag::Node makeAsyncBurstCommands(StateDag* d, size_t burstCount, StateDag::Node last) {
    using State = StreamDescriptor::State;
    std::reference_wrapper<StateDag::value_type> prev = last;
    for (size_t i = 0; i < burstCount; ++i) {
        StateDag::Node active = d->makeNode(State::ACTIVE, kBurstCommand, prev);
        active.children().push_back(d->makeNode(State::TRANSFERRING, kTransferReadyEvent, prev));
        prev = active;
    }
    return prev;
}

std::shared_ptr<StateSequence> makeBurstCommands(bool isSync, size_t burstCount,
                                                 bool standbyInputWhenDone) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node active = d->makeFinalNode(State::ACTIVE);
    StateDag::Node paused = d->makeNodes({std::make_pair(State::ACTIVE, kPauseCommand),
                                          std::make_pair(State::PAUSED, kFlushCommand)},
                                         State::STANDBY);
    StateDag::Node& last = standbyInputWhenDone ? paused : active;
    if (isSync) {
        StateDag::Node idle = d->makeNode(
                State::IDLE, kBurstCommand,
                // Use several bursts to ensure that the driver starts reporting the position.
                d->makeNodes(State::ACTIVE, kBurstCommand, burstCount, last));
        d->makeNode(State::STANDBY, kStartCommand, idle);
    } else {
        StateDag::Node active = makeAsyncBurstCommands(d.get(), burstCount, last);
        StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
        idle.children().push_back(d->makeNode(State::TRANSFERRING, kTransferReadyEvent, active));
        d->makeNode(State::STANDBY, kStartCommand, idle);
    }
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kReadSeq =
        std::make_tuple(std::string("Read"), kAidlVersion1, "", 0, StreamTypeFilter::ANY,
                        makeBurstCommands(true), true /*validatePositionIncrease*/);
static const NamedCommandSequence kWriteSyncSeq =
        std::make_tuple(std::string("Write"), kAidlVersion1, "", 0, StreamTypeFilter::SYNC,
                        makeBurstCommands(true), true /*validatePositionIncrease*/);
static const NamedCommandSequence kWriteAsyncSeq =
        std::make_tuple(std::string("Write"), kAidlVersion1, "", 0, StreamTypeFilter::ASYNC,
                        makeBurstCommands(false), true /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeAsyncDrainCommands(bool isInput) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    if (isInput) {
        d->makeNodes({std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kBurstCommand),
                      std::make_pair(State::ACTIVE, kDrainInCommand),
                      std::make_pair(State::DRAINING, kStartCommand),
                      std::make_pair(State::ACTIVE, kDrainInCommand)},
                     State::DRAINING);
    } else {
        StateDag::Node draining =
                d->makeNodes({std::make_pair(State::DRAINING, kBurstCommand),
                              std::make_pair(State::TRANSFERRING, kDrainOutAllCommand)},
                             State::DRAINING);
        StateDag::Node idle =
                d->makeNodes({std::make_pair(State::IDLE, kBurstCommand),
                              std::make_pair(State::TRANSFERRING, kDrainOutAllCommand)},
                             draining);
        // If we get straight into ACTIVE on burst, no further testing is possible.
        draining.children().push_back(d->makeFinalNode(State::ACTIVE));
        idle.children().push_back(d->makeFinalNode(State::ACTIVE));
        d->makeNode(State::STANDBY, kStartCommand, idle);
    }
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kWriteDrainAsyncSeq = std::make_tuple(
        std::string("WriteDrain"), kAidlVersion1, "", kStreamTransientStateTransitionDelayMs,
        StreamTypeFilter::ASYNC, makeAsyncDrainCommands(false), false /*validatePositionIncrease*/);
static const NamedCommandSequence kDrainInSeq =
        std::make_tuple(std::string("Drain"), kAidlVersion1, "", 0, StreamTypeFilter::ANY,
                        makeAsyncDrainCommands(true), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeDrainOutCommands(bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node last = d->makeFinalNode(State::IDLE);
    StateDag::Node active = d->makeNodes(
            {std::make_pair(State::ACTIVE, kDrainOutAllCommand),
             std::make_pair(State::DRAINING, isSync ? TransitionTrigger(kGetStatusCommand)
                                                    : TransitionTrigger(kDrainReadyEvent))},
            last);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    if (!isSync) {
        idle.children().push_back(d->makeNode(State::TRANSFERRING, kTransferReadyEvent, active));
    } else {
        active.children().push_back(last);
    }
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainOutSyncSeq =
        std::make_tuple(std::string("Drain"), kAidlVersion1, "", 0, StreamTypeFilter::SYNC,
                        makeDrainOutCommands(true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kDrainOutAsyncSeq =
        std::make_tuple(std::string("Drain"), kAidlVersion3, "", 0, StreamTypeFilter::ASYNC,
                        makeDrainOutCommands(false), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeDrainEarlyOutCommands() {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    // In the "early notify" case, the transition to the `IDLE` state following
    // the 'onDrainReady' event can take some time. Waiting for an arbitrary amount
    // of time may make the test fragile. Instead, for successful completion
    // is registered if the stream has entered `IDLE` or `DRAINING` state.
    StateDag::Node lastIdle = d->makeFinalNode(State::IDLE);
    StateDag::Node lastDraining = d->makeFinalNode(State::DRAINING);
    StateDag::Node draining =
            d->makeNode(State::DRAINING, kDrainReadyEvent, lastIdle, lastDraining);
    StateDag::Node active = d->makeNode(State::ACTIVE, kDrainOutEarlyCommand, draining);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    idle.children().push_back(d->makeNode(State::TRANSFERRING, kTransferReadyEvent, active));
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainEarlyOutAsyncSeq =
        std::make_tuple(std::string("DrainEarly"), kAidlVersion3, "", 0, StreamTypeFilter::ASYNC,
                        makeDrainEarlyOutCommands(), false /*validatePositionIncrease*/);

// DRAINING_en ->(onDrainReady) DRAINING_en_sent ->(onDrainReady) IDLE | TRANSFERRING
std::shared_ptr<StateSequence> makeDrainEarlyOffloadCommands() {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node lastIdle = d->makeFinalNode(State::IDLE);
    StateDag::Node lastTransferring = d->makeFinalNode(State::TRANSFERRING);
    // The second onDrainReady event.
    StateDag::Node continueDraining =
            d->makeNode(State::DRAINING, kDrainReadyEvent, lastIdle, lastTransferring);
    // The first onDrainReady event.
    StateDag::Node draining = d->makeNode(State::DRAINING, kDrainReadyEvent, continueDraining);
    StateDag::Node drain = d->makeNode(State::ACTIVE, kDrainOutEarlyCommand, draining);
    StateDag::Node active = makeAsyncBurstCommands(d.get(), 10, drain);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    idle.children().push_back(d->makeNode(State::TRANSFERRING, kTransferReadyEvent, active));
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainEarlyOffloadSeq =
        std::make_tuple(std::string("DrainEarly"), kAidlVersion3, "aosp.clipTransitionSupport", 0,
                        StreamTypeFilter::OFFLOAD, makeDrainEarlyOffloadCommands(),
                        true /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeDrainPauseOutCommands(bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node draining = d->makeNodes({std::make_pair(State::DRAINING, kPauseCommand),
                                            std::make_pair(State::DRAIN_PAUSED, kStartCommand),
                                            std::make_pair(State::DRAINING, kPauseCommand),
                                            std::make_pair(State::DRAIN_PAUSED, kBurstCommand)},
                                           isSync ? State::PAUSED : State::TRANSFER_PAUSED);
    StateDag::Node active = d->makeNode(State::ACTIVE, kDrainOutAllCommand, draining);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    if (!isSync) {
        idle.children().push_back(d->makeNode(State::TRANSFERRING, kDrainOutAllCommand, draining));
    } else {
        // If we get straight into IDLE on drain, no further testing is possible.
        active.children().push_back(d->makeFinalNode(State::IDLE));
    }
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainPauseOutSyncSeq =
        std::make_tuple(std::string("DrainPause"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::SYNC,
                        makeDrainPauseOutCommands(true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kDrainPauseOutAsyncSeq =
        std::make_tuple(std::string("DrainPause"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makeDrainPauseOutCommands(false), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeDrainEarlyPauseOutCommands() {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node draining = d->makeNodes({std::make_pair(State::DRAINING, kPauseCommand),
                                            std::make_pair(State::DRAIN_PAUSED, kStartCommand),
                                            std::make_pair(State::DRAINING, kPauseCommand),
                                            std::make_pair(State::DRAIN_PAUSED, kBurstCommand)},
                                           State::TRANSFER_PAUSED);
    StateDag::Node active = d->makeNode(State::ACTIVE, kDrainOutEarlyCommand, draining);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    idle.children().push_back(d->makeNode(State::TRANSFERRING, kDrainOutEarlyCommand, draining));
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainEarlyPauseOutAsyncSeq =
        std::make_tuple(std::string("DrainEarlyPause"), kAidlVersion3, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makeDrainEarlyPauseOutCommands(), false /*validatePositionIncrease*/);

// This sequence also verifies that the capture / presentation position is not reset on standby.
std::shared_ptr<StateSequence> makeStandbyCommands(bool isInput, bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    if (isInput) {
        d->makeNodes({std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kStandbyCommand),
                      std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kBurstCommand),
                      std::make_pair(State::ACTIVE, kPauseCommand),
                      std::make_pair(State::PAUSED, kFlushCommand),
                      std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kBurstCommand)},
                     State::ACTIVE);
    } else {
        StateDag::Node idle3 =
                d->makeNode(State::IDLE, kBurstCommand, d->makeFinalNode(State::ACTIVE));
        StateDag::Node idle2 = d->makeNodes({std::make_pair(State::IDLE, kStandbyCommand),
                                             std::make_pair(State::STANDBY, kStartCommand)},
                                            idle3);
        StateDag::Node active = d->makeNodes({std::make_pair(State::ACTIVE, kPauseCommand),
                                              std::make_pair(State::PAUSED, kFlushCommand)},
                                             idle2);
        StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
        if (!isSync) {
            idle3.children().push_back(d->makeFinalNode(State::TRANSFERRING));
            StateDag::Node transferring =
                    d->makeNodes({std::make_pair(State::TRANSFERRING, kPauseCommand),
                                  std::make_pair(State::TRANSFER_PAUSED, kFlushCommand)},
                                 idle2);
            idle.children().push_back(transferring);
        }
        d->makeNodes({std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kStandbyCommand),
                      std::make_pair(State::STANDBY, kStartCommand)},
                     idle);
    }
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kStandbyInSeq =
        std::make_tuple(std::string("Standby"), kAidlVersion1, "", 0, StreamTypeFilter::ANY,
                        makeStandbyCommands(true, false), false /*validatePositionIncrease*/);
static const NamedCommandSequence kStandbyOutSyncSeq =
        std::make_tuple(std::string("Standby"), kAidlVersion1, "", 0, StreamTypeFilter::SYNC,
                        makeStandbyCommands(false, true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kStandbyOutAsyncSeq =
        std::make_tuple(std::string("Standby"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makeStandbyCommands(false, false), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makePauseCommands(bool isInput, bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    if (isInput) {
        d->makeNodes({std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kBurstCommand),
                      std::make_pair(State::ACTIVE, kPauseCommand),
                      std::make_pair(State::PAUSED, kBurstCommand),
                      std::make_pair(State::ACTIVE, kPauseCommand),
                      std::make_pair(State::PAUSED, kFlushCommand)},
                     State::STANDBY);
    } else {
        StateDag::Node idle = d->makeNodes({std::make_pair(State::IDLE, kBurstCommand),
                                            std::make_pair(State::ACTIVE, kPauseCommand),
                                            std::make_pair(State::PAUSED, kStartCommand),
                                            std::make_pair(State::ACTIVE, kPauseCommand),
                                            std::make_pair(State::PAUSED, kBurstCommand),
                                            std::make_pair(State::PAUSED, kFlushCommand)},
                                           State::IDLE);
        if (!isSync) {
            idle.children().push_back(
                    d->makeNodes({std::make_pair(State::TRANSFERRING, kPauseCommand),
                                  std::make_pair(State::TRANSFER_PAUSED, kStartCommand),
                                  std::make_pair(State::TRANSFERRING, kPauseCommand),
                                  std::make_pair(State::TRANSFER_PAUSED, kDrainOutAllCommand),
                                  std::make_pair(State::DRAIN_PAUSED, kBurstCommand)},
                                 State::TRANSFER_PAUSED));
        }
        d->makeNode(State::STANDBY, kStartCommand, idle);
    }
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kPauseInSeq =
        std::make_tuple(std::string("Pause"), kAidlVersion1, "", 0, StreamTypeFilter::ANY,
                        makePauseCommands(true, false), false /*validatePositionIncrease*/);
static const NamedCommandSequence kPauseOutSyncSeq =
        std::make_tuple(std::string("Pause"), kAidlVersion1, "", 0, StreamTypeFilter::SYNC,
                        makePauseCommands(false, true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kPauseOutAsyncSeq =
        std::make_tuple(std::string("Pause"), kAidlVersion3, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makePauseCommands(false, false), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeFlushCommands(bool isInput, bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    if (isInput) {
        d->makeNodes({std::make_pair(State::STANDBY, kStartCommand),
                      std::make_pair(State::IDLE, kBurstCommand),
                      std::make_pair(State::ACTIVE, kPauseCommand),
                      std::make_pair(State::PAUSED, kFlushCommand)},
                     State::STANDBY);
    } else {
        StateDag::Node last = d->makeFinalNode(State::IDLE);
        StateDag::Node idle = d->makeNodes({std::make_pair(State::IDLE, kBurstCommand),
                                            std::make_pair(State::ACTIVE, kPauseCommand),
                                            std::make_pair(State::PAUSED, kFlushCommand)},
                                           last);
        if (!isSync) {
            idle.children().push_back(
                    d->makeNodes({std::make_pair(State::TRANSFERRING, kPauseCommand),
                                  std::make_pair(State::TRANSFER_PAUSED, kFlushCommand)},
                                 last));
        }
        d->makeNode(State::STANDBY, kStartCommand, idle);
    }
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kFlushInSeq =
        std::make_tuple(std::string("Flush"), kAidlVersion1, "", 0, StreamTypeFilter::ANY,
                        makeFlushCommands(true, false), false /*validatePositionIncrease*/);
static const NamedCommandSequence kFlushOutSyncSeq =
        std::make_tuple(std::string("Flush"), kAidlVersion1, "", 0, StreamTypeFilter::SYNC,
                        makeFlushCommands(false, true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kFlushOutAsyncSeq =
        std::make_tuple(std::string("Flush"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makeFlushCommands(false, false), false /*validatePositionIncrease*/);

std::shared_ptr<StateSequence> makeDrainPauseFlushOutCommands(bool isSync) {
    using State = StreamDescriptor::State;
    auto d = std::make_unique<StateDag>();
    StateDag::Node draining = d->makeNodes({std::make_pair(State::DRAINING, kPauseCommand),
                                            std::make_pair(State::DRAIN_PAUSED, kFlushCommand)},
                                           State::IDLE);
    StateDag::Node active = d->makeNode(State::ACTIVE, kDrainOutAllCommand, draining);
    StateDag::Node idle = d->makeNode(State::IDLE, kBurstCommand, active);
    if (!isSync) {
        idle.children().push_back(d->makeNode(State::TRANSFERRING, kDrainOutAllCommand, draining));
    } else {
        // If we get straight into IDLE on drain, no further testing is possible.
        active.children().push_back(d->makeFinalNode(State::IDLE));
    }
    d->makeNode(State::STANDBY, kStartCommand, idle);
    return std::make_shared<StateSequenceFollower>(std::move(d));
}
static const NamedCommandSequence kDrainPauseFlushOutSyncSeq =
        std::make_tuple(std::string("DrainPauseFlush"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::SYNC,
                        makeDrainPauseFlushOutCommands(true), false /*validatePositionIncrease*/);
static const NamedCommandSequence kDrainPauseFlushOutAsyncSeq =
        std::make_tuple(std::string("DrainPauseFlush"), kAidlVersion1, "",
                        kStreamTransientStateTransitionDelayMs, StreamTypeFilter::ASYNC,
                        makeDrainPauseFlushOutCommands(false), false /*validatePositionIncrease*/);

// Note, this isn't the "official" enum printer, it is only used to make the test name suffix.
std::string PrintStreamFilterToString(StreamTypeFilter filter) {
    switch (filter) {
        case StreamTypeFilter::ANY:
            return "";
        case StreamTypeFilter::SYNC:
            return "Sync";
        case StreamTypeFilter::ASYNC:
            return "Async";
        case StreamTypeFilter::OFFLOAD:
            return "Offload";
    }
    return std::string("Unknown").append(std::to_string(static_cast<int32_t>(filter)));
}
std::string GetStreamIoTestName(const testing::TestParamInfo<StreamIoTestParameters>& info) {
    return android::PrintInstanceNameToString(
                   testing::TestParamInfo<std::string>{std::get<PARAM_MODULE_NAME>(info.param),
                                                       info.index})
            .append("_")
            .append(std::get<NAMED_CMD_NAME>(std::get<PARAM_CMD_SEQ>(info.param)))
            .append(PrintStreamFilterToString(
                    std::get<NAMED_CMD_STREAM_TYPE>(std::get<PARAM_CMD_SEQ>(info.param))))
            .append("_SetupSeq")
            .append(std::get<PARAM_SETUP_SEQ>(info.param) ? "2" : "1");
}

INSTANTIATE_TEST_SUITE_P(
        AudioStreamIoInTest, AudioStreamIoIn,
        testing::Combine(testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         testing::Values(kReadSeq, kDrainInSeq, kStandbyInSeq, kPauseInSeq,
                                         kFlushInSeq),
                         testing::Values(false, true)),
        GetStreamIoTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioStreamIoIn);
INSTANTIATE_TEST_SUITE_P(
        AudioStreamIoOutTest, AudioStreamIoOut,
        testing::Combine(testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         testing::Values(kWriteSyncSeq, kWriteAsyncSeq, kWriteDrainAsyncSeq,
                                         kDrainOutSyncSeq, kDrainOutAsyncSeq,
                                         kDrainEarlyOutAsyncSeq, kDrainPauseOutSyncSeq,
                                         kDrainPauseOutAsyncSeq, kDrainEarlyPauseOutAsyncSeq,
                                         kStandbyOutSyncSeq, kStandbyOutAsyncSeq, kPauseOutSyncSeq,
                                         kPauseOutAsyncSeq, kFlushOutSyncSeq, kFlushOutAsyncSeq,
                                         kDrainPauseFlushOutSyncSeq, kDrainPauseFlushOutAsyncSeq,
                                         kDrainEarlyOffloadSeq),
                         testing::Values(false, true)),
        GetStreamIoTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioStreamIoOut);

INSTANTIATE_TEST_SUITE_P(AudioPatchTest, AudioModulePatch,
                         testing::ValuesIn(android::getAidlHalInstanceNames(IModule::descriptor)),
                         android::PrintInstanceNameToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioModulePatch);

static std::vector<std::string> getRemoteSubmixModuleInstance() {
    auto instances = android::getAidlHalInstanceNames(IModule::descriptor);
    for (auto instance : instances) {
        if (instance.ends_with("/r_submix")) return (std::vector<std::string>{instance});
    }
    return {};
}

template <typename Stream>
class WithRemoteSubmix {
  public:
    WithRemoteSubmix() : mStream(true /*isSync*/) {}
    explicit WithRemoteSubmix(AudioDeviceAddress address)
        : mStream(true /*isSync*/), mAddress(address) {
        LOG(DEBUG) << __func__ << ": Creating " << IOTraits<Stream>::directionStr
                   << " stream for: " << mAddress.value_or(AudioDeviceAddress{}).toString();
    }
    WithRemoteSubmix(const WithRemoteSubmix&) = delete;
    WithRemoteSubmix& operator=(const WithRemoteSubmix&) = delete;
    ~WithRemoteSubmix() {
        LOG(DEBUG) << __func__ << ": Deleting " << IOTraits<Stream>::directionStr
                   << " stream for: " << mAddress.value_or(AudioDeviceAddress{}).toString();
    }

    static std::optional<AudioPort> getRemoteSubmixAudioPort(ModuleConfig* moduleConfig) {
        auto ports =
                moduleConfig->getRemoteSubmixPorts(IOTraits<Stream>::is_input, true /*singlePort*/);
        if (ports.empty()) return {};
        return ports.front();
    }

    void SetUp(IModule* module, ModuleConfig* moduleConfig) {
        auto devicePort = getRemoteSubmixAudioPort(moduleConfig);
        ASSERT_TRUE(devicePort.has_value()) << "Device port for remote submix device not found";
        ASSERT_NO_FATAL_FAILURE(mStream.SetUp(module, moduleConfig, *devicePort, mAddress));
        mAddress = mStream.getDevice().address;
    }
    void SetUp(IModule* module, ModuleConfig* moduleConfig,
               const AudioPortConfig& existingMixPortConfig,
               const AudioPortConfig& existingDevicePortConfig) {
        ASSERT_NO_FATAL_FAILURE(mStream.SetUp(module, moduleConfig, existingMixPortConfig,
                                              existingDevicePortConfig));
        mAddress = mStream.getDevice().address;
    }
    void StartWorkerToSendBurstCommands(size_t burstCount = 10, bool standbyInputWhenDone = false) {
        ASSERT_NO_FATAL_FAILURE(
                mStream.StartWorkerToSendBurstCommands(burstCount, standbyInputWhenDone));
    }

    void JoinWorkerAfterBurstCommands(bool callPrepareToCloseBeforeJoin) {
        ASSERT_NO_FATAL_FAILURE(mStream.JoinWorkerAfterBurstCommands(
                true /*validatePositionIncrease*/, callPrepareToCloseBeforeJoin));
    }

    void JoinWorkerAfterBurstCommands(bool validatePositionIncrease,
                                      bool callPrepareToCloseBeforeJoin) {
        ASSERT_NO_FATAL_FAILURE(mStream.JoinWorkerAfterBurstCommands(validatePositionIncrease,
                                                                     callPrepareToCloseBeforeJoin));
    }

    void SendBurstCommands(bool callPrepareToCloseBeforeJoin, size_t burstCount = 10,
                           bool standbyInputWhenDone = false) {
        ASSERT_NO_FATAL_FAILURE(StartWorkerToSendBurstCommands(burstCount, standbyInputWhenDone));
        // When 'burstCount == 0', there is no "previous" frame count, thus the check for
        // the position increase fails.
        ASSERT_NO_FATAL_FAILURE(JoinWorkerAfterBurstCommands(
                burstCount > 0 /*validatePositionIncrease*/, callPrepareToCloseBeforeJoin));
    }

    std::optional<AudioDeviceAddress> getAudioDeviceAddress() const { return mAddress; }
    const AudioPortConfig& getDevicePortConfig() const { return mStream.getDevicePortConfig(); }
    int8_t getLastBurstIteration() const { return mStream.getLastData()[0]; }
    const AudioPortConfig& getPortConfig() const { return mStream.getPortConfig(); }
    std::string skipTestReason() const { return mStream.skipTestReason(); }

  private:
    StreamFixtureWithWorker<Stream> mStream;
    std::optional<AudioDeviceAddress> mAddress;
};

class AudioModuleRemoteSubmix : public AudioCoreModule {
  public:
    void SetUp() override {
        // Turn off "debug" which enables connections simulation. Since devices of the remote
        // submix module are virtual, there is no need for simulation.
        ASSERT_NO_FATAL_FAILURE(SetUpImpl(GetParam(), false /*setUpDebug*/));
        if (int32_t version; module->getInterfaceVersion(&version).isOk() && version < 2) {
            GTEST_SKIP() << "V1 uses a deprecated remote submix device type encoding";
        }
        ASSERT_NO_FATAL_FAILURE(SetUpModuleConfig());
    }

    void TearDown() override {
        streamIn.reset();
        streamOut.reset();
    }

    void CreateOutputStream() {
        streamOut = std::make_unique<WithRemoteSubmix<IStreamOut>>();
        ASSERT_NO_FATAL_FAILURE(streamOut->SetUp(module.get(), moduleConfig.get()));
        // Note: any issue with connection attempts is considered as a problem.
        ASSERT_EQ("", streamOut->skipTestReason());
        ASSERT_TRUE(streamOut->getAudioDeviceAddress().has_value());
    }

    void CreateInputStream(const std::optional<AudioDeviceAddress>& address = std::nullopt) {
        if (address.has_value()) {
            streamIn = std::make_unique<WithRemoteSubmix<IStreamIn>>(address.value());
        } else {
            ASSERT_TRUE(streamOut->getAudioDeviceAddress().has_value());
            streamIn = std::make_unique<WithRemoteSubmix<IStreamIn>>(
                    streamOut->getAudioDeviceAddress().value());
        }
        ASSERT_NO_FATAL_FAILURE(streamIn->SetUp(module.get(), moduleConfig.get()));
        ASSERT_EQ("", streamIn->skipTestReason());
        auto inAddress = streamIn->getAudioDeviceAddress();
        ASSERT_TRUE(inAddress.has_value());
        if (address.has_value()) {
            if (address.value() != AudioDeviceAddress{}) {
                ASSERT_EQ(address.value(), inAddress.value());
            }
        } else {
            ASSERT_EQ(streamOut->getAudioDeviceAddress().value(), inAddress.value());
        }
    }

    std::unique_ptr<WithRemoteSubmix<IStreamOut>> streamOut;
    std::unique_ptr<WithRemoteSubmix<IStreamIn>> streamIn;
};

TEST_P(AudioModuleRemoteSubmix, OutputDoesNotBlockWhenNoInput) {
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(streamOut->SendBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
}

TEST_P(AudioModuleRemoteSubmix, OutputDoesNotBlockWhenInputInStandby) {
    if (int32_t version; module->getInterfaceVersion(&version).isOk() && version < 3) {
        GTEST_SKIP() << "Default remote submix implementation <V3 could not pass this test";
    }
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(CreateInputStream());
    ASSERT_NO_FATAL_FAILURE(streamOut->StartWorkerToSendBurstCommands());
    // Send just 1 burst command. This triggers the condition "input is in standby after
    // being active." The output must flush the fifo before writing to avoid being blocked.
    ASSERT_NO_FATAL_FAILURE(
            streamIn->StartWorkerToSendBurstCommands(1, true /*stanbyInputWhenDone*/));
    // The output must be able to close without shutting down the pipe first (due to a call
    // to 'prepareToClose').
    ASSERT_NO_FATAL_FAILURE(
            streamOut->JoinWorkerAfterBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
    ASSERT_NO_FATAL_FAILURE(
            streamIn->JoinWorkerAfterBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
}

TEST_P(AudioModuleRemoteSubmix, BlockedOutputUnblocksOnClose) {
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(CreateInputStream());
    ASSERT_NO_FATAL_FAILURE(streamOut->StartWorkerToSendBurstCommands());
    // Send just 3 burst command, but do not enter standby. This is a stalled input.
    ASSERT_NO_FATAL_FAILURE(streamIn->StartWorkerToSendBurstCommands(3));
    ASSERT_NO_FATAL_FAILURE(
            streamOut->JoinWorkerAfterBurstCommands(true /*callPrepareToCloseBeforeJoin*/));
    ASSERT_NO_FATAL_FAILURE(
            streamIn->JoinWorkerAfterBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
}

TEST_P(AudioModuleRemoteSubmix, OutputBlocksUntilInputStarts) {
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(CreateInputStream());
    ASSERT_NO_FATAL_FAILURE(streamOut->StartWorkerToSendBurstCommands());
    // Read the head of the pipe and check that it starts with the first output burst, that is,
    // the contents of the very first write has not been superseded due to pipe overflow.
    // The burstCount is '0' because the very first burst is used to exit from the 'IDLE' state,
    // see 'makeBurstCommands'.
    ASSERT_NO_FATAL_FAILURE(streamIn->SendBurstCommands(false /*callPrepareToCloseBeforeJoin*/, 0,
                                                        true /*standbyInputWhenDone*/));
    EXPECT_EQ(1, streamIn->getLastBurstIteration());
    ASSERT_NO_FATAL_FAILURE(
            streamOut->JoinWorkerAfterBurstCommands(true /*callPrepareToCloseBeforeJoin*/));
}

TEST_P(AudioModuleRemoteSubmix, OutputAndInput) {
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(CreateInputStream());
    // Start writing into the output stream.
    ASSERT_NO_FATAL_FAILURE(streamOut->StartWorkerToSendBurstCommands());
    // Simultaneously, read from the input stream.
    ASSERT_NO_FATAL_FAILURE(streamIn->SendBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
    ASSERT_NO_FATAL_FAILURE(
            streamOut->JoinWorkerAfterBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
}

TEST_P(AudioModuleRemoteSubmix, OpenInputMultipleTimes) {
    ASSERT_NO_FATAL_FAILURE(CreateOutputStream());
    ASSERT_NO_FATAL_FAILURE(CreateInputStream());
    ASSERT_NO_FATAL_FAILURE(streamOut->StartWorkerToSendBurstCommands());
    ASSERT_NO_FATAL_FAILURE(streamIn->SendBurstCommands(false /*callPrepareToCloseBeforeJoin*/, 1,
                                                        true /*standbyInputWhenDone*/));
    // For the new stream, only create a new mix port config and a new patch.
    const size_t extraStreamInCount = 2;
    std::vector<std::unique_ptr<WithRemoteSubmix<IStreamIn>>> streamIns(extraStreamInCount);
    for (size_t i = 0; i < extraStreamInCount; i++) {
        streamIns[i] = std::make_unique<WithRemoteSubmix<IStreamIn>>();
        ASSERT_NO_FATAL_FAILURE(streamIns[i]->SetUp(module.get(), moduleConfig.get(),
                                                    streamIn->getPortConfig(),
                                                    streamIn->getDevicePortConfig()));
        ASSERT_EQ("", streamIns[i]->skipTestReason());
        const auto inAddress = streamIns[i]->getAudioDeviceAddress();
        ASSERT_TRUE(inAddress.has_value());
        ASSERT_EQ(streamOut->getAudioDeviceAddress().value(), inAddress.value());
        ASSERT_NO_FATAL_FAILURE(streamIns[i]->SendBurstCommands(
                false /*callPrepareToCloseBeforeJoin*/, 1, true /*standbyInputWhenDone*/));
    }
    ASSERT_NO_FATAL_FAILURE(
            streamOut->JoinWorkerAfterBurstCommands(false /*callPrepareToCloseBeforeJoin*/));
}

INSTANTIATE_TEST_SUITE_P(AudioModuleRemoteSubmixTest, AudioModuleRemoteSubmix,
                         ::testing::ValuesIn(getRemoteSubmixModuleInstance()));
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AudioModuleRemoteSubmix);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new TestExecutionTracer());
    android::base::SetMinimumLogSeverity(::android::base::DEBUG);
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();
    return RUN_ALL_TESTS();
}
