/*
 * Copyright (C) 2024 The Android Open Source Project
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

package android.hardware.gnss.gnss_assistance;

/**
 * Contains Klobuchar ionospheric model coefficients used by GPS, BDS, QZSS.
 * This is defined in IS-GPS-200 20.3.3.5.1.7.
 *
 * @hide
 */
@VintfStability
parcelable KlobucharIonosphericModel {
    /** Alpha0 coefficient in seconds. */
    double alpha0;

    /** Alpha1 coefficient in seconds per semi-circle. */
    double alpha1;

    /** Alpha2 coefficient in seconds per semi-circle squared. */
    double alpha2;

    /** Alpha3 coefficient in seconds per semi-circle cubed. */
    double alpha3;

    /** Beta0 coefficient in seconds. */
    double beta0;

    /** Beta1 coefficient in seconds per semi-circle. */
    double beta1;

    /** Beta2 coefficient in seconds per semi-circle squared. */
    double beta2;

    /** Beta3 coefficient in seconds per semi-circle cubed. */
    double beta3;
}
