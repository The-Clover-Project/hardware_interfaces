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

package android.hardware.radio.ims.media;

@VintfStability
@RustDerive(Clone=true, Eq=true, PartialEq=true)
parcelable RtpReceptionStats {
    /** The timestamp of the latest RTP packet received */
    int rtpTimestamp;

    /** The timestamp of the latest RTCP-SR packet received */
    int rtcpSrTimestamp;

    /** The NTP timestamp of latest RTCP-SR packet received */
    long rtcpSrNtpTimestamp;

    /**
     * The mean jitter buffer delay of a media stream from received to playback, measured in
     *  milliseconds, within the reporting interval
     */
    int jitterBufferMs;

    /** The round trip time delay in millisecond when latest RTP packet received */
    int roundTripTimeMs;
}
