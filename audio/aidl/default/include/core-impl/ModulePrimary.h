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

#include "core-impl/Module.h"

namespace aidl::android::hardware::audio::core {

class ModulePrimary final : public Module {
  public:
    ModulePrimary(std::unique_ptr<Configuration>&& config)
        : Module(Type::DEFAULT, std::move(config)) {}

  protected:
    ndk::ScopedAStatus getTelephony(std::shared_ptr<ITelephony>* _aidl_return) override;

    ndk::ScopedAStatus calculateBufferSizeFrames(
            const ::aidl::android::media::audio::common::AudioFormatDescription& format,
            int32_t latencyMs, int32_t sampleRateHz, int32_t* bufferSizeFrames) override;
    ndk::ScopedAStatus createInputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones,
            std::shared_ptr<StreamIn>* result) override;
    ndk::ScopedAStatus createOutputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo,
            std::shared_ptr<StreamOut>* result) override;
    ndk::ScopedAStatus createMmapBuffer(
            const ::aidl::android::media::audio::common::AudioPortConfig& portConfig,
            int32_t bufferSizeFrames, int32_t frameSizeBytes, MmapBufferDescriptor* desc) override;
    int32_t getNominalLatencyMs(
            const ::aidl::android::media::audio::common::AudioPortConfig& portConfig) override;

  private:
    ChildInterface<ITelephony> mTelephony;
};

}  // namespace aidl::android::hardware::audio::core
