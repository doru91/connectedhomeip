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

#include "JFAManager.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/server/Server.h>

#include "JFAPlatform.h"
#include "JFAWildcardSubscription.h"

#include <app/CASESessionManager.h>
#include <clusters/BooleanState/Attributes.h>
#include <clusters/FanControl/Attributes.h>
#include <clusters/FanControl/Enums.h>
#include <clusters/LevelControl/Commands.h>
#include <clusters/OnOff/Commands.h>
#include <controller/CHIPCluster.h>
#include <controller/CHIPDeviceController.h>
#include <controller/ReadInteraction.h>
#include <controller/WriteInteraction.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::Controller;
using namespace chip::Crypto;
using namespace chip::app::Clusters::JointFabricAdministrator;

constexpr uint8_t kJFAvailableShift = 0;
constexpr uint8_t kJFAdminShift     = 1;
constexpr uint8_t kJFAnchorShift    = 2;
constexpr uint8_t kJFDatastoreShift = 3;

constexpr chip::EndpointId kJointFabricAdminEndpointId = 1;
constexpr uint32_t kPostRebootSubscriptionDelaySeconds = 60;

JFAManager JFAManager::sJFA;

void JFAManager::OnRestoreSubscriptionsTimer(chip::System::Layer * /*layer*/, void * appState)
{
    JFAManager * self = static_cast<JFAManager *>(appState);
    auto & datastore  = Server::GetInstance().GetJointFabricDatastore();

    Controller::DeviceCommissioner * commissioner = JFAGetDeviceCommissioner();
    VerifyOrReturn(commissioner != nullptr,
                   ChipLogError(JointFabric, "Cannot restore subscriptions: commissioner not initialized"));

    CASESessionManager * caseSessionManager = commissioner->CASESessionMgr();
    VerifyOrReturn(caseSessionManager != nullptr,
                   ChipLogError(JointFabric, "Cannot restore subscriptions: commissioner CASESessionManager unavailable"));

    for (const auto & entry : datastore.GetNodeInformationEntries())
    {
        ChipLogProgress(JointFabric, "Restoring wildcard subscription for nodeId=0x" ChipLogFormatX64,
                        ChipLogValueX64(entry.nodeID));

        // FindOrEstablishSession establishes CASE over the commissioner's
        // endpoint; the subscription is opened from OnRestoreConnected using the
        // exchange manager handed to that callback.
        caseSessionManager->FindOrEstablishSession(ScopedNodeId(entry.nodeID, commissioner->GetFabricIndex()),
                                                   &self->mOnRestoreConnectedCallback, &self->mOnRestoreConnectionFailureCallback);
    }
}

void JFAManager::OnRestoreConnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
{
    (void) context;

    ScopedNodeId peer = sessionHandle->GetPeer();
    ChipLogProgress(JointFabric, "Restore: CASE established to node=0x" ChipLogFormatX64 ", opening wildcard subscription",
                    ChipLogValueX64(peer.GetNodeId()));

    // Open the subscription on the just-established session over the
    // commissioner's exchange manager. The ReadClient inherits the
    // commissioner-owned session; if the peer later drops, the subscription
    // re-establishes CASE through the commissioner's CASESessionManager.
    NXP::App::JFA::OpenWildcardSubscription(exchangeMgr, sessionHandle, peer);
}

void JFAManager::OnRestoreConnectionFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error)
{
    (void) context;
    ChipLogError(JointFabric, "Restore: failed to establish CASE to 0x" ChipLogFormatX64 " on fabric index %d: %" CHIP_ERROR_FORMAT,
                 ChipLogValueX64(peerId.GetNodeId()), peerId.GetFabricIndex(), error.Format());
}

CHIP_ERROR JFAManager::Init(Server & server)
{
    mServer             = &server;
    mCASESessionManager = server.GetCASESessionManager();

    // Restore jfFabricIndex from persisted fabric table after a reboot.
    for (const auto & fabricTable : mServer->GetFabricTable())
    {
        FabricIndex fabricIndex = fabricTable.GetFabricIndex();
        CATValues cats;
        if (mServer->GetFabricTable().FetchCATs(fabricIndex, cats) != CHIP_NO_ERROR)
        {
            continue;
        }
        if (cats.ContainsIdentifier(kAdminCATIdentifier) && cats.ContainsIdentifier(kAnchorCATIdentifier))
        {
            jfFabricIndex = fabricIndex;
            ChipLogProgress(JointFabric, "Restored JF fabric index %u from persisted fabric table", fabricIndex);
            // Commissioner init is intentionally deferred to OnNetworkReady
            // (kDnssdInitialized). JFAInitCommissioner calls AdvertiseOperational,
            // which requires DNS-SD to be up; attempting it here, before the
            // network is ready, fails.
            break;
        }
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::GetJointFabricMode(uint8_t & jointFabricMode)
{
    jointFabricMode = ((IsDeviceCommissioned() ? 0 : 1) << kJFAvailableShift) |
        (IsDeviceCommissioned() ? (IsDeviceJFAdmin() ? (1 << kJFAdminShift) : 0) : 0) |
        (IsDeviceCommissioned() ? (IsDeviceJFAnchor() ? (1 << kJFAnchorShift) : 0) : 0) |
        (IsDeviceCommissioned() ? (1 << kJFDatastoreShift) : 0);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::FinalizeCommissioning(NodeId nodeId, bool isJCM, P256PublicKey & trustedIcacPublicKeyB, uint16_t endpointId)
{
    if (jfFabricIndex == kUndefinedFabricId)
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }

    ChipLogProgress(JointFabric, "FinalizeCommissioning for NodeID: 0x" ChipLogFormatX64 ", isJCM = %d, peerEndpointId = %d",
                    ChipLogValueX64(nodeId), isJCM, endpointId);

    peerAdminJFAdminClusterEndpointId = endpointId;
    peerAdminICACPubKey               = trustedIcacPublicKeyB;
    ScopedNodeId scopedNodeId         = ScopedNodeId(nodeId, jfFabricIndex);

    // Per spec 11.24.4.1 [11.1705], the Commissioner populates the Datastore with a
    // pending node entry before completing commissioning, so the node is guaranteed
    // to appear in the Datastore before it is brought onto the fabric. RefreshNode
    // (which marks the entry committed) is invoked only after CommissioningComplete
    // succeeds, in OnCommissioningCompleteResponse. On any failure path the pending
    // entry is removed per [11.1707].
    //
    // Only the standard-commissioning flow uses the Datastore node list; the JCM flow
    // does not add a pending node here.
    if (!isJCM)
    {
        auto & datastore = Server::GetInstance().GetJointFabricDatastore();

        char friendlyNameBuffer[32];
        int written = snprintf(friendlyNameBuffer, sizeof(friendlyNameBuffer), "node-0x" ChipLogFormatX64, ChipLogValueX64(nodeId));
        CharSpan friendlyName = CharSpan(friendlyNameBuffer, static_cast<size_t>(written));

        // Defensively remove any stale pending entry from a previous, aborted
        // commissioning attempt so AddPendingNode does not fail with ConstraintError.
        // RemoveNode returns a ConstraintError when no entry exists; that is expected
        // for a fresh node and is intentionally ignored.
        (void) datastore.RemoveNode(nodeId);
        ReturnErrorOnFailure(datastore.AddPendingNode(nodeId, friendlyName));
    }

    ConnectToNode(scopedNodeId, isJCM ? kJCMCommissioning : kStandardCommissioningComplete);

    return CHIP_NO_ERROR;
}

void JFAManager::SetJFARpc(JFARpc & aJFARpc)
{
    mJFARpc = &aJFARpc;
}

JFARpc * JFAManager::GetJFARpc()
{
    return mJFARpc;
}

void JFAManager::HandleCommissioningCompleteEvent()
{
    for (const auto & fb : mServer->GetFabricTable())
    {
        FabricIndex fabricIndex = fb.GetFabricIndex();
        CATValues cats;

        if ((jfFabricIndex == kUndefinedFabricIndex) && mServer->GetFabricTable().FetchCATs(fabricIndex, cats) == CHIP_NO_ERROR)
        {
            /* When JFA is commissioned, it has to be issued a NOC with Anchor CAT and Administrator CAT */
            if (cats.ContainsIdentifier(kAdminCATIdentifier) && cats.ContainsIdentifier(kAnchorCATIdentifier))
            {
                (void) app::Clusters::JointFabricAdministrator::Attributes::AdministratorFabricIndex::Set(
                    kJointFabricAdminEndpointId, fabricIndex);

                jfFabricIndex = fabricIndex;

                // Record the joint fabric index so the datastore can be purged if this fabric is ever removed.
                Server::GetInstance().GetJointFabricDatastore().SetAnchorFabricIndex(fabricIndex);

                // Set AnchorRootCA
                uint8_t mAnchorRootCABuffer[Credentials::kMaxDERCertLength];
                MutableByteSpan rootCertSpan(mAnchorRootCABuffer);
                if (mServer->GetFabricTable().FetchRootCert(fabricIndex, rootCertSpan) == CHIP_NO_ERROR)
                {
                    if (Server::GetInstance().GetJointFabricDatastore().SetAnchorRootCA(rootCertSpan) == CHIP_NO_ERROR)
                    {
                        ChipLogProgress(JointFabric, "Set AnchorRootCA (%u bytes)", static_cast<unsigned>(rootCertSpan.size()));
                    }
                    else
                    {
                        ChipLogError(JointFabric, "Failed to set AnchorRootCA");
                    }
                }

                // obtain nodeid and use it to set anchornodeid
                NodeId nodeId = fb.GetNodeId();
                if (nodeId != kUndefinedNodeId)
                {
                    if (Server::GetInstance().GetJointFabricDatastore().SetAnchorNodeId(nodeId) == CHIP_NO_ERROR)
                    {
                        ChipLogProgress(JointFabric, "Set AnchorNodeId to 0x" ChipLogFormatX64, ChipLogValueX64(nodeId));
                    }
                    else
                    {
                        ChipLogError(JointFabric, "Failed to set AnchorNodeId to 0x" ChipLogFormatX64, ChipLogValueX64(nodeId));
                    }
                }

                VendorId vendorId = fb.GetVendorId();
                if (vendorId != VendorId::NotSpecified)
                {
                    if (Server::GetInstance().GetJointFabricDatastore().SetAnchorVendorId(vendorId) == CHIP_NO_ERROR)
                    {
                        ChipLogProgress(JointFabric, "Set AnchorVendorId to %d", vendorId);
                    }
                    else
                    {
                        ChipLogError(JointFabric, "Failed to set AnchorVendorId to %d", vendorId);
                    }
                }

                if (vendorId != VendorId::NotSpecified && nodeId != kUndefinedNodeId)
                {
                    char friendlyNameBuffer[32];
                    int written = snprintf(friendlyNameBuffer, sizeof(friendlyNameBuffer), "jfa-0x%04X-0x" ChipLogFormatX64,
                                           to_underlying(vendorId), ChipLogValueX64(nodeId));
                    CharSpan friendlyName = CharSpan(friendlyNameBuffer, static_cast<size_t>(written));

                    if (Server::GetInstance().GetJointFabricDatastore().SetFriendlyName(friendlyName) == CHIP_NO_ERROR)
                    {
                        ChipLogProgress(JointFabric, "Set FriendlyName to %.*s", static_cast<int>(friendlyName.size()),
                                        friendlyName.data());
                    }
                    else
                    {
                        ChipLogError(JointFabric, "Failed to set FriendlyName to %.*s", static_cast<int>(friendlyName.size()),
                                     friendlyName.data());
                    }
                }

                Server::GetInstance().GetJointFabricDatastore().SetStatus(
                    Clusters::JointFabricDatastore::DatastoreStateEnum::kPending,
                    static_cast<uint32_t>(System::SystemClock().GetMonotonicTimestamp().count()), 0);

                Clusters::JointFabricDatastore::Structs::DatastoreGroupKeySetStruct::Type defaultGroupKeySetEntry{ 0 };
                LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().AddGroupKeySetEntry(defaultGroupKeySetEntry));
                LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().ForceAddNodeKeySetEntry(0, nodeId));

                // Add default group entry for the JFA itself
                // Uses internal method that bypasses CAT restrictions since JFA setup needs both Admin and Anchor CATs
                Clusters::JointFabricDatastore::Commands::AddGroup::DecodableType addGroupCommandData;
                addGroupCommandData.groupID       = 0;
                addGroupCommandData.friendlyName  = "Default JFA Group"_span;
                addGroupCommandData.groupKeySetID = 0;
                addGroupCommandData.groupCAT      = kAdminCATIdentifier; // Use Admin CAT for default group
                addGroupCommandData.groupCATVersion.SetNonNull(1);
                addGroupCommandData.groupPermission =
                    Clusters::JointFabricDatastore::DatastoreAccessControlEntryPrivilegeEnum::kAdminister;
                LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().ForceAddGroup(addGroupCommandData));
                addGroupCommandData.groupID  = 1;
                addGroupCommandData.groupCAT = kAnchorCATIdentifier; // Use Anchor CAT for second default group
                LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().ForceAddGroup(addGroupCommandData));

                Clusters::JointFabricDatastore::Structs::DatastoreAdministratorInformationEntryStruct::Type newAdmin;
                newAdmin.nodeID       = nodeId;
                newAdmin.friendlyName = "Default JFA Admin"_span;
                newAdmin.vendorID     = vendorId;
                newAdmin.icac         = ByteSpan(mICACBuffer, mICACBufferLen);

                LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().AddAdmin(newAdmin));

                if (!mCommissionerInitialized)
                {
                    CHIP_ERROR err = JFAInitCommissioner(fb.GetFabricId(), fabricIndex);
                    if (err == CHIP_NO_ERROR)
                    {
                        mCommissionerInitialized = true;
                        ChipLogProgress(JointFabric, "Commissioner mode initialized on commissioned fabric index %u",
                                        static_cast<unsigned>(fabricIndex));
                    }
                    else
                    {
                        ChipLogError(JointFabric, "Failed to initialize commissioner mode: %" CHIP_ERROR_FORMAT, err.Format());
                    }
                }

                ChipLogProgress(JointFabric, "Joint Fabric Administrator commissioned on fabric index %d", fabricIndex);
            }
        }
    }
}

void JFAManager::OnNetworkReady()
{
    if (mCommissionerInitialized || jfFabricIndex == kUndefinedFabricIndex)
    {
        return;
    }

    // jfFabricIndex was restored in JFAManager::Init(). This is the single
    // commissioner-init point for the reboot case: it runs on kDnssdInitialized,
    // so AdvertiseOperational() inside JFAInitCommissioner has a working DNS-SD
    // service.
    for (const auto & fabricTable : mServer->GetFabricTable())
    {
        if (fabricTable.GetFabricIndex() != jfFabricIndex)
        {
            continue;
        }
        CHIP_ERROR err = JFAInitCommissioner(fabricTable.GetFabricId(), jfFabricIndex);
        if (err == CHIP_NO_ERROR)
        {
            mCommissionerInitialized = true;
            ChipLogProgress(JointFabric, "Commissioner re-initialized after network ready");

            // Re-fetch data for all nodes that were restored
            // from KVS (their node IDs are in the datastore but endpoints are not
            // persisted and must be re-fetched from the commissionee).
            auto & datastore = Server::GetInstance().GetJointFabricDatastore();
            for (const auto & entry : datastore.GetNodeInformationEntries())
            {
                ChipLogProgress(JointFabric, "Restoring RefreshNode for nodeId=0x" ChipLogFormatX64, ChipLogValueX64(entry.nodeID));
                LogErrorOnFailure(datastore.RefreshNode(entry.nodeID));
            }
            // Schedule the wildcard subscriptions once, after the delay:
            ChipLogProgress(JointFabric, "Scheduling subscription restore in %u s", kPostRebootSubscriptionDelaySeconds);
            DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(kPostRebootSubscriptionDelaySeconds),
                                                  OnRestoreSubscriptionsTimer, this);
        }
        else
        {
            ChipLogError(JointFabric, "OnNetworkReady: JFAInitCommissioner failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
        break;
    }
}

bool JFAManager::IsDeviceJFAdmin()
{
    if (jfFabricIndex == kUndefinedFabricId)
    {
        return false;
    }

    CATValues cats;

    if (mServer->GetFabricTable().FetchCATs(jfFabricIndex, cats) == CHIP_NO_ERROR)
    {
        if (cats.ContainsIdentifier(kAdminCATIdentifier))
        {
            return true;
        }
    }

    return false;
}

bool JFAManager::IsDeviceJFAnchor()
{
    if (jfFabricIndex == kUndefinedFabricId)
    {
        return false;
    }

    CATValues cats;

    if (mServer->GetFabricTable().FetchCATs(jfFabricIndex, cats) == CHIP_NO_ERROR)
    {
        if (cats.ContainsIdentifier(kAnchorCATIdentifier))
        {
            return true;
        }
    }

    return false;
}

void JFAManager::ReleaseSession()
{
    auto optionalSessionHandle = mSessionHolder.Get();

    if (optionalSessionHandle.HasValue())
    {
        if (optionalSessionHandle.Value()->IsActiveSession())
        {
            optionalSessionHandle.Value()->AsSecureSession()->MarkAsDefunct();
        }
    }
    mSessionHolder.Release();
}

void JFAManager::ConnectToNode(ScopedNodeId scopedNodeId, OnConnectedAction onConnectedAction)
{
    VerifyOrDie(mServer != nullptr);

    if ((scopedNodeId.GetFabricIndex() == kUndefinedFabricIndex) || (scopedNodeId.GetNodeId() == kUndefinedNodeId))
    {
        ChipLogError(JointFabric, "Invalid node location!");
        return;
    }

    // Set the action to take once connection is successfully established
    mNodeId            = scopedNodeId.GetNodeId();
    mOnConnectedAction = onConnectedAction;

    ChipLogDetail(JointFabric, "Establishing session to node ID 0x" ChipLogFormatX64 " on fabric index %d",
                  ChipLogValueX64(scopedNodeId.GetNodeId()), scopedNodeId.GetFabricIndex());

    mCASESessionManager->FindOrEstablishSession(scopedNodeId, &mOnConnectedCallback, &mOnConnectionFailureCallback);
}

void JFAManager::OnConnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);

    VerifyOrDie(jfaManager != nullptr);
    jfaManager->mSessionHolder.Grab(sessionHandle);
    jfaManager->mExchangeMgr = &exchangeMgr;

    ChipLogProgress(JointFabric, "Established CASE");

    switch (jfaManager->mOnConnectedAction)
    {
    case kStandardCommissioningComplete: {
        TEMPORARY_RETURN_IGNORED jfaManager->SendCommissioningComplete();
        break;
    }
    case kJCMCommissioning: {
        TEMPORARY_RETURN_IGNORED jfaManager->AnnounceJointFabricAdministrator();
        break;
    }
    case kOnOffOn: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendOnOff(true);
        break;
    }
    case kOnOffOff: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendOnOff(false);
        break;
    }
    case kOnOffToggle: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendOnOffToggle();
        break;
    }
    case kFanOn: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendFanMode(true);
        break;
    }
    case kFanOff: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendFanMode(false);
        break;
    }
    case kReadContact: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoReadContactState();
        break;
    }
    case kLevelControlMoveToLevel: {
        TEMPORARY_RETURN_IGNORED jfaManager->DoSendLevelControlMoveToLevel();
        break;
    }
    default:
        break;
    }
}

void JFAManager::OnConnectionFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogError(JointFabric, "Failed to establish connection to 0x" ChipLogFormatX64 " on fabric index %d",
                 ChipLogValueX64(peerId.GetNodeId()), peerId.GetFabricIndex());

    // Per spec 11.24.4.1 [11.1707], a failed finalize-commissioning step removes the
    // pending node from the Datastore. Only the standard flow added a pending entry.
    if (jfaManager->mOnConnectedAction == kStandardCommissioningComplete)
    {
        LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().RemoveNode(peerId.GetNodeId()));
    }

    jfaManager->ReleaseSession();
}

CHIP_ERROR JFAManager::AnnounceJointFabricAdministrator()
{
    Commands::AnnounceJointFabricAdministrator::Type request;

    if (!mExchangeMgr)
    {
        return CHIP_ERROR_UNINITIALIZED;
    }

    ChipLogProgress(JointFabric, "AnnounceJointFabricAdministrator: invoke cluster command.");
    request.endpointID = kJointFabricAdminEndpointId;

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), peerAdminJFAdminClusterEndpointId);
    return cluster.InvokeCommand(request, this, OnAnnounceJointFabricAdministratorResponse,
                                 OnAnnounceJointFabricAdministratorFailure);
}

void JFAManager::OnAnnounceJointFabricAdministratorResponse(void * context, const chip::app::DataModel::NullObjectType & data)
{
    JFAManager * jfaManagerCore = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManagerCore != nullptr);

    ChipLogProgress(JointFabric, "OnAnnounceJointFabricAdministratorResponse");

    /* TODO: https://github.com/project-chip/connectedhomeip/issues/38202 */
    TEMPORARY_RETURN_IGNORED jfaManagerCore->SendICACSRRequest();
}

void JFAManager::OnAnnounceJointFabricAdministratorFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManagerCore = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManagerCore != nullptr);
    jfaManagerCore->ReleaseSession();

    ChipLogError(JointFabric, "OnAnnounceJointFabricAdministratorFailure: %s\n", chip::ErrorStr(error));
}

CHIP_ERROR JFAManager::SendICACSRRequest()
{
    Commands::ICACCSRRequest::Type request;

    if (!mExchangeMgr)
    {
        return CHIP_ERROR_UNINITIALIZED;
    }

    ChipLogProgress(JointFabric, "SendICACSRRequest: invoke cluster command.");

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), peerAdminJFAdminClusterEndpointId);
    return cluster.InvokeCommand(request, this, OnSendICACSRRequestResponse, OnSendICACSRRequestFailure);
}

void JFAManager::OnSendICACSRRequestResponse(void * context, const Commands::ICACCSRResponse::DecodableType & icaccsr)
{
    JFAManager * jfaManagerCore = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManagerCore != nullptr);
    P256PublicKey pubKey;

    ChipLogProgress(JointFabric, "OnSendICACSRRequestResponse");

    if (icaccsr.icaccsr.HasValue() &&
        (CHIP_NO_ERROR ==
         VerifyCertificateSigningRequest(icaccsr.icaccsr.Value().data(), icaccsr.icaccsr.Value().size(), pubKey)) &&
        jfaManagerCore->peerAdminICACPubKey.Matches(pubKey))
    {
        ChipLogProgress(JointFabric, "OnSendICACSRRequestResponse: validated ICAC CSR");
        TEMPORARY_RETURN_IGNORED jfaManagerCore->SendCommissioningComplete();
    }
}

void JFAManager::OnSendICACSRRequestFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManagerCore = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManagerCore != nullptr);
    jfaManagerCore->ReleaseSession();

    ChipLogError(JointFabric, "OnSendICACSRRequestFailure: %s\n", chip::ErrorStr(error));
}

CHIP_ERROR JFAManager::SendCommissioningComplete()
{
    GeneralCommissioning::Commands::CommissioningComplete::Type request;

    if (!mExchangeMgr)
    {
        return CHIP_ERROR_UNINITIALIZED;
    }

    ChipLogProgress(JointFabric, "SendCommissioningComplete: invoke cluster command.");
    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), kRootEndpointId);
    return cluster.InvokeCommand(request, this, OnCommissioningCompleteResponse, OnCommissioningCompleteFailure);
}

void JFAManager::OnCommissioningCompleteResponse(
    void * context, const GeneralCommissioning::Commands::CommissioningCompleteResponse::DecodableType & data)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogProgress(JointFabric, "OnCommissioningCompleteResponse, Code=%u", to_underlying(data.errorCode));

    if (data.errorCode != GeneralCommissioning::CommissioningErrorEnum::kOk)
    {
        ChipLogProgress(JointFabric, "Commssioning Failed (nodeId=%ld, isJCM = %d), Code=%u", jfaManager->mNodeId,
                        jfaManager->mOnConnectedAction == kJCMCommissioning, to_underlying(data.errorCode));

        // Per spec 11.24.4.1 [11.1707], if the finalize-commissioning step fails the
        // Commissioner removes the pending node from the Datastore. Only the standard
        // flow added a pending entry in FinalizeCommissioning.
        if (jfaManager->mOnConnectedAction == kStandardCommissioningComplete)
        {
            LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().RemoveNode(jfaManager->mNodeId));
        }
        ChipLogProgress(JointFabric, "Commssioning Failed (nodeId=0x" ChipLogFormatX64 ", isJCM = %d), Code=%u",
                        ChipLogValueX64(jfaManager->mNodeId), jfaManager->mOnConnectedAction == kJCMCommissioning,
                        to_underlying(data.errorCode));
    }
    else
    {
        switch (jfaManager->mOnConnectedAction)
        {
        case kStandardCommissioningComplete: {
            ChipLogProgress(JointFabric, "Standard Commissioning (nodeId=%ld) success", jfaManager->mNodeId);
            auto & datastore = Server::GetInstance().GetJointFabricDatastore();

            // The pending node entry was added in FinalizeCommissioning before commissioning
            // completed (spec 11.24.4.1 [11.1705]). Now that CommissioningComplete has
            // succeeded, invoke RefreshNode to update the entry with state read from the
            // Node and mark it committed.
            LogErrorOnFailure(datastore.RefreshNode(jfaManager->mNodeId));
            ChipLogProgress(JointFabric, "Standard Commissioning (nodeId=0x" ChipLogFormatX64 ") success",
                            ChipLogValueX64(jfaManager->mNodeId));
            // Immediately after onboarding, open a wildcard subscription to the
            // commissionee over the CASE session just used to complete
            // commissioning. The JFA is the commissioner and already holds the
            // session. The subscription's ReadClient takes its own reference
            // to the session, so it stays resident once the JFA drops its
            // transient command-session holder below
            // (see the non-defunct release on the success path).
            jfaManager->OpenCommissioneeSubscription();
            break;
        }
        case kJCMCommissioning: {
            ChipLogProgress(JointFabric, "Joint Commissioning Method (nodeId=0x" ChipLogFormatX64 ") success",
                            ChipLogValueX64(jfaManager->mNodeId));
            break;
        }
        default:
            break;
        }
    }

    // On the standard-commissioning success path the wildcard subscription has
    // grabbed its own reference to the secure session, so only drop the JFA's
    // holder without marking the session defunct; otherwise tear the session
    // down as before.
    if (data.errorCode == GeneralCommissioning::CommissioningErrorEnum::kOk &&
        jfaManager->mOnConnectedAction == kStandardCommissioningComplete)
    {
        jfaManager->mSessionHolder.Release();
        jfaManager->mExchangeMgr = nullptr;
    }
    else
    {
        jfaManager->ReleaseSession();
    }
}

void JFAManager::OpenCommissioneeSubscription()
{
    auto optionalSessionHandle = mSessionHolder.Get();

    if (mExchangeMgr == nullptr || !optionalSessionHandle.HasValue())
    {
        ChipLogError(JointFabric, "Cannot open commissionee subscription: no active session");
        return;
    }

    NXP::App::JFA::OpenWildcardSubscription(*mExchangeMgr, optionalSessionHandle.Value(), ScopedNodeId(mNodeId, jfFabricIndex));
}

void JFAManager::OnCommissioningCompleteFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManagerCore = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManagerCore != nullptr);

    // Per spec 11.24.4.1 [11.1707], a failed finalize-commissioning step removes the
    // pending node from the Datastore. Only the standard flow added a pending entry.
    if (jfaManagerCore->mOnConnectedAction == kStandardCommissioningComplete)
    {
        LogErrorOnFailure(Server::GetInstance().GetJointFabricDatastore().RemoveNode(jfaManagerCore->mNodeId));
    }

    jfaManagerCore->ReleaseSession();

    ChipLogError(JointFabric, "Received failure response %s\n", chip::ErrorStr(error));
}

CHIP_ERROR JFAManager::GetIcacCsr(MutableByteSpan & icacCsr)
{
    JFARpc * jfaRpc = GetJFARpc();

    if (jfaRpc)
    {
        return jfaRpc->GetICACCSRForJF(icacCsr);
    }

    return CHIP_ERROR_UNINITIALIZED;
}

// ============================================================
// Client cluster commands
// ============================================================

CHIP_ERROR JFAManager::SendOnOff(NodeId nodeId, EndpointId endpointId, bool on)
{
    VerifyOrReturnError(IsDeviceCommissioned(), CHIP_ERROR_INCORRECT_STATE);

    mClientCmdEndpointId = endpointId;
    ScopedNodeId scopedNodeId(nodeId, jfFabricIndex);
    ConnectToNode(scopedNodeId, on ? kOnOffOn : kOnOffOff);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::SendFanOnOff(NodeId nodeId, EndpointId endpointId, bool on)
{
    VerifyOrReturnError(IsDeviceCommissioned(), CHIP_ERROR_INCORRECT_STATE);

    mClientCmdEndpointId = endpointId;
    ScopedNodeId scopedNodeId(nodeId, jfFabricIndex);
    ConnectToNode(scopedNodeId, on ? kFanOn : kFanOff);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::ReadContactState(NodeId nodeId, EndpointId endpointId)
{
    VerifyOrReturnError(IsDeviceCommissioned(), CHIP_ERROR_INCORRECT_STATE);

    mClientCmdEndpointId = endpointId;
    ScopedNodeId scopedNodeId(nodeId, jfFabricIndex);
    ConnectToNode(scopedNodeId, kReadContact);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::SendOnOffToggle(NodeId nodeId, EndpointId endpointId)
{
    VerifyOrReturnError(IsDeviceCommissioned(), CHIP_ERROR_INCORRECT_STATE);

    mClientCmdEndpointId = endpointId;
    ScopedNodeId scopedNodeId(nodeId, jfFabricIndex);
    ConnectToNode(scopedNodeId, kOnOffToggle);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::SendLevelControlMoveToLevel(NodeId nodeId, EndpointId endpointId, uint8_t level, uint16_t transitionTime)
{
    VerifyOrReturnError(IsDeviceCommissioned(), CHIP_ERROR_INCORRECT_STATE);

    mClientCmdEndpointId        = endpointId;
    mLevelControlLevel          = level;
    mLevelControlTransitionTime = transitionTime;
    ScopedNodeId scopedNodeId(nodeId, jfFabricIndex);
    ConnectToNode(scopedNodeId, kLevelControlMoveToLevel);
    return CHIP_NO_ERROR;
}

CHIP_ERROR JFAManager::DoSendOnOff(bool on)
{
    VerifyOrReturnError(mExchangeMgr != nullptr, CHIP_ERROR_UNINITIALIZED);

    ChipLogProgress(JointFabric, "DoSendOnOff: sending %s to node 0x" ChipLogFormatX64 " ep %u", on ? "On" : "Off",
                    ChipLogValueX64(mNodeId), mClientCmdEndpointId);

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), mClientCmdEndpointId);

    if (on)
    {
        OnOff::Commands::On::Type request;
        return cluster.InvokeCommand(request, this, OnOnOffSuccess, OnOnOffFailure);
    }

    OnOff::Commands::Off::Type request;
    return cluster.InvokeCommand(request, this, OnOnOffSuccess, OnOnOffFailure);
}

CHIP_ERROR JFAManager::DoSendOnOffToggle()
{
    VerifyOrReturnError(mExchangeMgr != nullptr, CHIP_ERROR_UNINITIALIZED);

    ChipLogProgress(JointFabric, "DoSendOnOffToggle: sending Toggle to node 0x" ChipLogFormatX64 " ep %u", ChipLogValueX64(mNodeId),
                    mClientCmdEndpointId);

    OnOff::Commands::Toggle::Type request;
    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), mClientCmdEndpointId);
    return cluster.InvokeCommand(request, this, OnOnOffSuccess, OnOnOffFailure);
}

void JFAManager::OnOnOffSuccess(void * context, const app::DataModel::NullObjectType & data)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogProgress(JointFabric, "OnOff command succeeded");
    jfaManager->ReleaseSession();
}

void JFAManager::OnOnOffFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogError(JointFabric, "OnOff command failed: %s", chip::ErrorStr(error));
    jfaManager->ReleaseSession();
}

CHIP_ERROR JFAManager::DoSendFanMode(bool on)
{
    VerifyOrReturnError(mExchangeMgr != nullptr, CHIP_ERROR_UNINITIALIZED);

    using FanModeEnum = app::Clusters::FanControl::FanModeEnum;
    FanModeEnum mode  = on ? FanModeEnum::kOn : FanModeEnum::kOff;

    ChipLogProgress(JointFabric, "DoSendFanMode: setting FanMode=%u on node 0x" ChipLogFormatX64 " ep %u", to_underlying(mode),
                    ChipLogValueX64(mNodeId), mClientCmdEndpointId);

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), mClientCmdEndpointId);
    // Use the explicit clusterId/attributeId overload to avoid template argument
    // disambiguation issues with nested type names on GCC.
    return cluster.WriteAttribute(mode, this, FanControl::Id, FanControl::Attributes::FanMode::Id, OnFanWriteSuccess,
                                  OnFanWriteFailure, chip::NullOptional);
}

void JFAManager::OnFanWriteSuccess(void * context)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogProgress(JointFabric, "FanControl FanMode write succeeded");
    jfaManager->ReleaseSession();
}

void JFAManager::OnFanWriteFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogError(JointFabric, "FanControl FanMode write failed: %s", chip::ErrorStr(error));
    jfaManager->ReleaseSession();
}

CHIP_ERROR JFAManager::DoReadContactState()
{
    VerifyOrReturnError(mExchangeMgr != nullptr, CHIP_ERROR_UNINITIALIZED);

    ChipLogProgress(JointFabric, "DoReadContactState: reading from node 0x" ChipLogFormatX64 " ep %u", ChipLogValueX64(mNodeId),
                    mClientCmdEndpointId);

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), mClientCmdEndpointId);
    // Use the explicit DecodableType/clusterId/attributeId overload to avoid template
    // argument disambiguation issues with nested type names on GCC.
    return cluster.ReadAttribute<bool, bool>(this, BooleanState::Id, BooleanState::Attributes::StateValue::Id, OnContactReadSuccess,
                                             OnContactReadFailure);
}

void JFAManager::OnContactReadSuccess(void * context, bool value)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogProgress(JointFabric, "BooleanState StateValue = %s", value ? "true (contact)" : "false (no contact)");
    jfaManager->ReleaseSession();
}

void JFAManager::OnContactReadFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogError(JointFabric, "BooleanState read failed: %s", chip::ErrorStr(error));
    jfaManager->ReleaseSession();
}

CHIP_ERROR JFAManager::DoSendLevelControlMoveToLevel()
{
    VerifyOrReturnError(mExchangeMgr != nullptr, CHIP_ERROR_UNINITIALIZED);

    ChipLogProgress(JointFabric, "DoSendLevelControlMoveToLevel: level=%u transitionTime=%u on node 0x" ChipLogFormatX64 " ep %u",
                    mLevelControlLevel, mLevelControlTransitionTime, ChipLogValueX64(mNodeId), mClientCmdEndpointId);

    LevelControl::Commands::MoveToLevel::Type request;
    request.level = mLevelControlLevel;
    request.transitionTime.SetNonNull(mLevelControlTransitionTime);
    // optionsMask and optionsOverride are left at their default zero values.

    Controller::ClusterBase cluster(*mExchangeMgr, mSessionHolder.Get().Value(), mClientCmdEndpointId);
    return cluster.InvokeCommand(request, this, OnLevelControlSuccess, OnLevelControlFailure);
}

void JFAManager::OnLevelControlSuccess(void * context, const app::DataModel::NullObjectType & data)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogProgress(JointFabric, "LevelControl MoveToLevel command succeeded");
    jfaManager->ReleaseSession();
}

void JFAManager::OnLevelControlFailure(void * context, CHIP_ERROR error)
{
    JFAManager * jfaManager = static_cast<JFAManager *>(context);
    VerifyOrDie(jfaManager != nullptr);

    ChipLogError(JointFabric, "LevelControl MoveToLevel command failed: %s", chip::ErrorStr(error));
    jfaManager->ReleaseSession();
}
