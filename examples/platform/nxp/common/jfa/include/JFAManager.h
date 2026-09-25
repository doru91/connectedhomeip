/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *    All rights reserved.
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

#pragma once

#include <app/server/JointFabricAdministrator.h>
#include <app/server/Server.h>
#include <controller/CHIPDeviceController.h>
#include <credentials/CHIPCert.h>
#include <crypto/CHIPCryptoPAL.h>

#include <functional>
#include <stdbool.h>
#include <stdint.h>

#include <lib/core/CHIPError.h>

#include "JFARpc.h"

namespace chip {

class JFAManager : public app::JointFabricAdministrator::Delegate
{
public:
    JFAManager() :
        mOnConnectedCallback(OnConnected, this), mOnConnectionFailureCallback(OnConnectionFailure, this),
        mOnRestoreConnectedCallback(OnRestoreConnected, this), mOnRestoreConnectionFailureCallback(OnRestoreConnectionFailure, this)
    {}

    CHIP_ERROR Init(Server & server);
    void HandleCommissioningCompleteEvent();
    static void OnRestoreSubscriptionsTimer(chip::System::Layer * layer, void * appState);
    void OnNetworkReady();
    CHIP_ERROR FinalizeCommissioning(NodeId nodeId, bool isJCM, chip::Crypto::P256PublicKey & trustedIcacPublicKeyB,
                                     uint16_t peerAdminJFAdminClusterEndpointId);

    void SetJFARpc(JFARpc & aJFARpc);
    JFARpc * GetJFARpc();

    /* app::JointFabricAdministrator::Delegate */
    CHIP_ERROR GetIcacCsr(MutableByteSpan & icacCsr) override;

    CHIP_ERROR GetJointFabricMode(uint8_t & jointFabricMode);

    bool IsDeviceCommissioned() { return jfFabricIndex != kUndefinedFabricId; }
    bool IsDeviceJFAdmin();
    bool IsDeviceJFAnchor();

    // Client commands: send cluster commands to peer devices already on the joint fabric.
    CHIP_ERROR SendOnOff(NodeId nodeId, EndpointId endpointId, bool on);
    CHIP_ERROR SendOnOffToggle(NodeId nodeId, EndpointId endpointId);
    CHIP_ERROR SendFanOnOff(NodeId nodeId, EndpointId endpointId, bool on);
    CHIP_ERROR ReadContactState(NodeId nodeId, EndpointId endpointId);
    CHIP_ERROR SendLevelControlMoveToLevel(NodeId nodeId, EndpointId endpointId, uint8_t level, uint16_t transitionTime);

private:
    // Various actions to take when OnConnected callback is called
    enum OnConnectedAction
    {
        kStandardCommissioningComplete = 0,
        kJCMCommissioning              = 1,
        kOnOffOn                       = 2,
        kOnOffOff                      = 3,
        kOnOffToggle                   = 4,
        kFanOn                         = 5,
        kFanOff                        = 6,
        kReadContact                   = 7,
        kLevelControlMoveToLevel       = 8,
    };

    friend JFAManager & JFAMgr(void);

    static JFAManager sJFA;
    static void OnConnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle);
    static void OnConnectionFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error);

    // Reboot-restore path: establish the CASE session through the JFA
    // commissioner's own CASESessionManager/ExchangeManager (not the Server's
    // Interaction Model engine) so that the UDP endpoint used to initiate CASE
    // is the same one that owns the resulting subscription's ReadClient.
    static void OnRestoreConnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle);
    static void OnRestoreConnectionFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error);

    Server * mServer                          = nullptr;
    CASESessionManager * mCASESessionManager  = nullptr;
    Messaging::ExchangeManager * mExchangeMgr = nullptr;
    JFARpc * mJFARpc                          = nullptr;
    SessionHolder mSessionHolder;
    Callback::Callback<OnDeviceConnected> mOnConnectedCallback;
    Callback::Callback<OnDeviceConnectionFailure> mOnConnectionFailureCallback;
    Callback::Callback<OnDeviceConnected> mOnRestoreConnectedCallback;
    Callback::Callback<OnDeviceConnectionFailure> mOnRestoreConnectionFailureCallback;

    NodeId mNodeId                               = kUndefinedNodeId;
    OnConnectedAction mOnConnectedAction         = kStandardCommissioningComplete;
    FabricId jfFabricIndex                       = kUndefinedFabricId;
    EndpointId peerAdminJFAdminClusterEndpointId = kInvalidEndpointId;
    Crypto::P256PublicKey peerAdminICACPubKey;
    uint8_t mICACBuffer[Credentials::kMaxDERCertLength];
    size_t mICACBufferLen         = 0;
    bool mCommissionerInitialized = false;

    // Endpoint used for client cluster commands (OnOff, FanControl, BooleanState reads).
    EndpointId mClientCmdEndpointId = kInvalidEndpointId;

    // Parameters for LevelControl MoveToLevel command.
    uint8_t mLevelControlLevel           = 0;
    uint16_t mLevelControlTransitionTime = 0;

    void ConnectToNode(ScopedNodeId scopedNodeId, OnConnectedAction onConnectedAction);
    // Opens a wildcard subscription to the just-onboarded commissionee over the
    // active CASE session.
    void OpenCommissioneeSubscription();
    CHIP_ERROR SendCommissioningComplete();
    CHIP_ERROR AnnounceJointFabricAdministrator();
    CHIP_ERROR SendICACSRRequest();

    // Private helpers dispatched from OnConnected for client cluster commands.
    CHIP_ERROR DoSendOnOff(bool on);
    CHIP_ERROR DoSendOnOffToggle();
    CHIP_ERROR DoSendFanMode(bool on);
    CHIP_ERROR DoReadContactState();
    CHIP_ERROR DoSendLevelControlMoveToLevel();

    static void OnCommissioningCompleteResponse(
        void * context, const app::Clusters::GeneralCommissioning::Commands::CommissioningCompleteResponse::DecodableType & data);
    static void OnCommissioningCompleteFailure(void * context, CHIP_ERROR error);
    static void OnAnnounceJointFabricAdministratorResponse(void * context, const chip::app::DataModel::NullObjectType &);
    static void OnAnnounceJointFabricAdministratorFailure(void * context, CHIP_ERROR error);
    static void
    OnSendICACSRRequestResponse(void * context,
                                const app::Clusters::JointFabricAdministrator::Commands::ICACCSRResponse::DecodableType & icaccsr);
    static void OnSendICACSRRequestFailure(void * context, CHIP_ERROR error);

    // Callbacks for OnOff cluster commands.
    static void OnOnOffSuccess(void * context, const app::DataModel::NullObjectType & data);
    static void OnOnOffFailure(void * context, CHIP_ERROR error);

    // Callbacks for FanControl FanMode attribute write.
    static void OnFanWriteSuccess(void * context);
    static void OnFanWriteFailure(void * context, CHIP_ERROR error);

    // Callbacks for BooleanState StateValue attribute read.
    static void OnContactReadSuccess(void * context, bool value);
    static void OnContactReadFailure(void * context, CHIP_ERROR error);

    // Callbacks for LevelControl MoveToLevel command.
    static void OnLevelControlSuccess(void * context, const app::DataModel::NullObjectType & data);
    static void OnLevelControlFailure(void * context, CHIP_ERROR error);

    void ReleaseSession();
};

inline JFAManager & JFAMgr(void)
{
    return JFAManager::sJFA;
}

} // namespace chip
