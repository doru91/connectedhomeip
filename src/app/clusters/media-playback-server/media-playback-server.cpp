/**
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *
 *    Copyright (c) 2021 Silicon Labs
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
/****************************************************************************
 * @file
 * @brief Routines for the Media Playback plugin, the
 *server implementation of the Media Playback cluster.
 *******************************************************************************
 ******************************************************************************/

#include <app/util/af.h>

bool emberAfMediaPlaybackClusterFastForwardRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterNextRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterPauseRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterPlayRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterPreviousRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterRewindRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterStopRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterSkipBackwardRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterStartOverRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}

bool emberAfMediaPlaybackClusterSkipForwardRequestCallback()
{
    EmberAfStatus status = EMBER_ZCL_STATUS_SUCCESS;
    emberAfSendImmediateDefaultResponse(status);
    return true;
}
