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
#include <fuzzbinder/libbinder_ndk_driver.h>
#include <fuzzer/FuzzedDataProvider.h>
#include "hwcryptokeyimpl.h"

using android::fuzzService;
using ndk::SharedRefBase;

static const char* TIPC_DEFAULT_DEVNAME = "/dev/trusty-ipc-dev0";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto hwCryptoServer =
            android::trusty::hwcryptohalservice::HwCryptoKey::Create(TIPC_DEFAULT_DEVNAME);

    fuzzService(hwCryptoServer->asBinder().get(), FuzzedDataProvider(data, size));

    return 0;
}
