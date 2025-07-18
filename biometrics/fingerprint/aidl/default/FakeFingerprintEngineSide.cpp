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

#include "FakeFingerprintEngineSide.h"

#include <android-base/logging.h>

#include <fingerprint.sysprop.h>

#include "util/CancellationSignal.h"
#include "util/Util.h"

using namespace ::android::fingerprint::virt;

namespace aidl::android::hardware::biometrics::fingerprint {
SensorLocation FakeFingerprintEngineSide::defaultLocation[] = {
        // default to CF display
        {.sensorLocationX = 0, 200, 90, "local:4619827353912518656"}};

FakeFingerprintEngineSide::FakeFingerprintEngineSide() : FakeFingerprintEngine() {}

void FakeFingerprintEngineSide::getDefaultSensorLocation(
        std::vector<SensorLocation>& sensorLocation) {
    for (int i = 0; i < (sizeof(defaultLocation) / sizeof(defaultLocation[0])); i++) {
        sensorLocation.push_back(defaultLocation[i]);
    }
}

}  // namespace aidl::android::hardware::biometrics::fingerprint
