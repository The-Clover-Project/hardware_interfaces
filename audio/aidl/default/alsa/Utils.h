/*
 * Copyright (C) 2023 The Android Open Source Project
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

#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <aidl/android/media/audio/common/AudioChannelLayout.h>
#include <aidl/android/media/audio/common/AudioFormatDescription.h>
#include <aidl/android/media/audio/common/AudioPort.h>

#include "core-impl/Stream.h"

extern "C" {
#include <tinyalsa/pcm.h>
#include "alsa_device_profile.h"
#include "alsa_device_proxy.h"
}

namespace aidl::android::hardware::audio::core::alsa {

struct DeviceProfile {
    int card;
    int device;
    int direction; /* PCM_OUT or PCM_IN */
    bool isExternal;
};
std::ostream& operator<<(std::ostream& os, const DeviceProfile& device);

class DeviceProxy {
  public:
    DeviceProxy();  // Constructs a "null" proxy.
    explicit DeviceProxy(const DeviceProfile& deviceProfile);
    alsa_device_profile* getProfile() const { return mProfile.get(); }
    alsa_device_proxy* get() const { return mProxy.get(); }

  private:
    static void alsaProxyDeleter(alsa_device_proxy* proxy);
    using AlsaProxy = std::unique_ptr<alsa_device_proxy, decltype(alsaProxyDeleter)*>;

    std::unique_ptr<alsa_device_profile> mProfile;
    AlsaProxy mProxy;
};

void applyGain(void* buffer, float gain, size_t bytesToTransfer, enum pcm_format pcmFormat,
               int channelCount);
::aidl::android::media::audio::common::AudioChannelLayout getChannelLayoutMaskFromChannelCount(
        unsigned int channelCount, int isInput);
::aidl::android::media::audio::common::AudioChannelLayout getChannelIndexMaskFromChannelCount(
        unsigned int channelCount);
unsigned int getChannelCountFromChannelMask(
        const ::aidl::android::media::audio::common::AudioChannelLayout& channelMask, bool isInput);
std::vector<::aidl::android::media::audio::common::AudioChannelLayout> getChannelMasksFromProfile(
        const alsa_device_profile* profile);
std::optional<DeviceProfile> getDeviceProfile(
        const ::aidl::android::media::audio::common::AudioDevice& audioDevice, bool isInput);
std::optional<DeviceProfile> getDeviceProfile(
        const ::aidl::android::media::audio::common::AudioPort& audioPort);
std::optional<struct pcm_config> getPcmConfig(const StreamContext& context, bool isInput);
std::vector<int> getSampleRatesFromProfile(const alsa_device_profile* profile);
DeviceProxy openProxyForAttachedDevice(const DeviceProfile& deviceProfile,
                                       struct pcm_config* pcmConfig, size_t bufferFrameCount);
DeviceProxy openProxyForExternalDevice(const DeviceProfile& deviceProfile,
                                       struct pcm_config* pcmConfig, bool requireExactMatch);
DeviceProxy readAlsaDeviceInfo(const DeviceProfile& deviceProfile);
void resetTransferredFrames(DeviceProxy& proxy, uint64_t frames);

::aidl::android::media::audio::common::AudioFormatDescription
c2aidl_pcm_format_AudioFormatDescription(enum pcm_format legacy);
pcm_format aidl2c_AudioFormatDescription_pcm_format(
        const ::aidl::android::media::audio::common::AudioFormatDescription& aidl);

}  // namespace aidl::android::hardware::audio::core::alsa
