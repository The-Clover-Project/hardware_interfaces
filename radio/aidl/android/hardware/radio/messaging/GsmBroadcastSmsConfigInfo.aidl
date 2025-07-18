/*
 * Copyright (C) 2021 The Android Open Source Project
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

package android.hardware.radio.messaging;

/**
 * Which types of Cell Broadcast Message (CBM) are to be received by the ME
 * @hide
 */
@VintfStability
@JavaDerive(toString=true)
@RustDerive(Clone=true, Eq=true, PartialEq=true)
parcelable GsmBroadcastSmsConfigInfo {
    /**
     * Beginning of the range of CBM message identifiers whose value is 0x0000 - 0xFFFF as defined
     * in TS 23.041 9.4.1.2.2 for GMS and 9.4.4.2.2 for UMTS.
     * All other values must be treated as empty CBM message ID.
     */
    int fromServiceId;
    /**
     * End of the range of CBM message identifiers whose value is 0x0000 - 0xFFFF as defined in
     * TS 23.041 9.4.1.2.2 for GMS and 9.4.4.2.2 for UMTS.
     * All other values must be treated as empty CBM message ID.
     */
    int toServiceId;
    /**
     * Beginning of the range of CBM data coding schemes whose value is 0x00 - 0xFF as defined in
     * TS 23.041 9.4.1.2.3 for GMS and 9.4.4.2.3 for UMTS.
     * All other values must be treated as empty CBM data coding scheme.
     */
    int fromCodeScheme;
    /**
     * End of the range of CBM data coding schemes whose value is 0x00 - 0xFF as defined in
     * TS 23.041 9.4.1.2.3 for GMS and 9.4.4.2.3 for UMTS.
     * All other values must be treated as empty CBM data coding scheme.
     */
    int toCodeScheme;
    /**
     * False means message types specified in <fromServiceId, toServiceId>
     * and <fromCodeScheme, toCodeScheme> are not accepted, while true means accepted.
     */
    boolean selected;
}
