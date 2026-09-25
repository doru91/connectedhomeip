/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "JFAWildcardSubscription.h"

#include "JFAPlatform.h"

#include <app/AttributePathParams.h>
#include <app/BufferedReadCallback.h>
#include <app/CASESessionManager.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadClient.h>
#include <app/ReadPrepareParams.h>
#include <controller/CHIPDeviceController.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLVReader.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <cstddef>
#include <cstdint>

using namespace chip;
using namespace chip::app;

namespace chip {
namespace NXP {
namespace App {
namespace JFA {

namespace {

// Subscription timing. A wide max ceiling keeps the subscription quiet after the
// priming report.
constexpr uint16_t kMinIntervalFloorSeconds   = 1;
constexpr uint16_t kMaxIntervalCeilingSeconds = 30;

// Resubscription backoff when a commissionee drops (e.g. it reboots). Capped so
// a persistently unreachable node is retried at a steady, low rate.
constexpr uint32_t kResubscribeInitialDelayMs = 2000;
constexpr uint32_t kResubscribeMaxDelayMs     = 60000;

// Maximum number of consecutive reconnect attempts before giving up and freeing
// the slot. A node that reconnects successfully resets this counter, so this
// only trips for a peer that stays unreachable (for example a commissionee that
// was removed from the fabric). With the exponential backoff above this spans
// roughly a couple of minutes before the slot is released.
constexpr uint32_t kMaxResubscribeAttempts = 5;

// Number of concurrent wildcard subscriptions this handler can hold alive at
// once. Each slot owns an independent ReadClient so subscriptions to multiple
// commissionees can stay resident simultaneously.
constexpr size_t kMaxConcurrentSubscriptions = 32;

// Owns one wildcard subscription's state and the ReadClient driving the
// interaction. Reported attribute data is reassembled by a BufferedReadCallback
// (correct list-chunk handling, as chip-tool does) and then dropped: nothing is
// retained beyond a single in-flight report.
//
// Resubscription is driven by this class rather than by the ReadClient's
// built-in SendAutoResubscribeRequest machinery. The built-in path re-establishes
// CASE through InteractionModelEngine::GetInstance() (the Server's
// CASESessionManager, bound to the operational port), whereas a JFA subscription
// must stay on the JFA commissioner's own CASESessionManager / UDP endpoint. On
// subscription loss this class re-establishes CASE through the commissioner and
// opens a fresh ReadClient over the resulting session.
class WildcardSubscription : public ReadClient::Callback
{
public:
    WildcardSubscription() :
        mBufferedAdapter(*this), mOnReconnectedCallback(OnReconnected, this), mOnReconnectFailureCallback(OnReconnectFailure, this)
    {}

    // The callback handed to the ReadClient.
    ReadClient::Callback & GetReadCallback() { return mBufferedAdapter; }

    void SetSlot(unsigned slot) { mSlot = slot; }
    unsigned GetSlot() const { return mSlot; }

    bool InUse() const { return mInUse; }
    NodeId GetPeer() const { return mPeer.GetNodeId(); }
    bool ReportComplete() const { return mReportComplete; }

    // Claims this slot for a given peer just before the ReadClient is created.
    // The ScopedNodeId is retained so the subscription can re-establish CASE
    // through the commissioner if the peer drops.
    void Claim(const ScopedNodeId & peer)
    {
        mInUse               = true;
        mPeer                = peer;
        mReportComplete      = false;
        mResubscribeDelayMs  = kResubscribeInitialDelayMs;
        mResubscribeAttempts = 0;
    }

    void SetReadClient(Platform::UniquePtr<ReadClient> readClient) { mReadClient = std::move(readClient); }

    // Opens the wildcard subscription over an existing session and installs the
    // resulting ReadClient into this slot. Returns the error from SendRequest.
    CHIP_ERROR Subscribe(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & session)
    {
        auto readClient = Platform::MakeUnique<ReadClient>(InteractionModelEngine::GetInstance(), &exchangeMgr, GetReadCallback(),
                                                           ReadClient::InteractionType::Subscribe);
        VerifyOrReturnError(readClient != nullptr, CHIP_ERROR_NO_MEMORY);

        // Wildcard attribute path: leaving endpoint/cluster/attribute unset means
        // "all endpoints, all clusters, all attributes".
        AttributePathParams wildcardPath;

        ReadPrepareParams readParams(session);
        readParams.mpAttributePathParamsList    = &wildcardPath;
        readParams.mAttributePathParamsListSize = 1;
        readParams.mMinIntervalFloorSeconds     = kMinIntervalFloorSeconds;
        readParams.mMaxIntervalCeilingSeconds   = kMaxIntervalCeilingSeconds;
        // Keep any already-established subscription (to another commissionee) alive.
        readParams.mKeepSubscriptions = true;

        ReturnErrorOnFailure(readClient->SendRequest(readParams));

        mReadClient = std::move(readClient);
        return CHIP_NO_ERROR;
    }

    // ReadClient::Callback: attribute data arrives here after list reassembly.
    // The TLV is dropped without storing it; only the path is logged so
    // concurrent subscriptions can be told apart.
    void OnAttributeData(const ConcreteDataAttributePath & path, TLV::TLVReader * apData, const StatusIB & status) override
    {
        ChipLogProgress(JointFabric,
                        "[jfsub%u] node=0x" ChipLogFormatX64 " subId=0x%" PRIx32 " Endpoint=0x%x Cluster=0x%04" PRIx32
                        " Attribute=0x%04" PRIx32,
                        mSlot, ChipLogValueX64(mPeer.GetNodeId()), mSubscriptionId, path.mEndpointId, path.mClusterId,
                        path.mAttributeId);
        (void) apData;
        (void) status;
    }

    void OnReportEnd() override { mReportComplete = true; }

    void OnError(CHIP_ERROR error) override
    {
        ChipLogError(JointFabric, "[jfsub%u] Subscription error: %" CHIP_ERROR_FORMAT, mSlot, error.Format());
    }

    void OnDone(ReadClient * apReadClient) override
    {
        (void) apReadClient;
        // Reached when the subscription terminates (liveness loss, e.g. the
        // commissionee rebooted). Tear down the dead ReadClient but keep the slot
        // claimed and schedule a reconnect: re-establish CASE through the JFA
        // commissioner and reopen the subscription. The slot is only freed if the
        // commissioner is gone (JFA shutting down).
        ChipLogProgress(JointFabric, "[jfsub%u] Subscription done, scheduling resubscribe to node=0x" ChipLogFormatX64, mSlot,
                        ChipLogValueX64(mPeer.GetNodeId()));
        mReadClient.reset();
        mReportComplete = false;

        ScheduleReconnect();
    }

    void OnSubscriptionEstablished(SubscriptionId subscriptionId) override
    {
        mSubscriptionId = subscriptionId;
        // A successful (re)subscription resets the backoff and the attempt budget.
        mResubscribeDelayMs  = kResubscribeInitialDelayMs;
        mResubscribeAttempts = 0;
        ChipLogProgress(JointFabric, "[jfsub%u] Subscription established, id=0x%" PRIx32, mSlot, subscriptionId);
    }

    // Frees the slot. Used when there is no commissioner to reconnect through.
    void Release()
    {
        mReadClient.reset();
        mInUse          = false;
        mReportComplete = false;
        mPeer           = ScopedNodeId();
    }

private:
    void ScheduleReconnect()
    {
        Controller::DeviceCommissioner * commissioner = JFAGetDeviceCommissioner();
        if (commissioner == nullptr)
        {
            ChipLogError(JointFabric, "[jfsub%u] No commissioner available; releasing subscription slot", mSlot);
            Release();
            return;
        }

        // Give up after a bounded number of consecutive attempts so a peer that is
        // gone for good (e.g. removed from the fabric) does not get retried
        // forever. OnSubscriptionEstablished resets the counter, so a node that
        // reconnects successfully gets a fresh budget on its next drop.
        if (mResubscribeAttempts >= kMaxResubscribeAttempts)
        {
            ChipLogError(JointFabric, "[jfsub%u] Giving up after %u resubscribe attempts; releasing slot", mSlot,
                         static_cast<unsigned>(mResubscribeAttempts));
            Release();
            return;
        }
        mResubscribeAttempts++;

        ChipLogProgress(JointFabric, "[jfsub%u] Resubscribe to node=0x" ChipLogFormatX64 " in %u ms", mSlot,
                        ChipLogValueX64(mPeer.GetNodeId()), static_cast<unsigned>(mResubscribeDelayMs));

        CHIP_ERROR err =
            DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(mResubscribeDelayMs), OnReconnectTimer, this);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(JointFabric, "[jfsub%u] Failed to arm resubscribe timer: %" CHIP_ERROR_FORMAT, mSlot, err.Format());
            Release();
        }
    }

    static void OnReconnectTimer(System::Layer * /*layer*/, void * appState)
    {
        auto * self = static_cast<WildcardSubscription *>(appState);

        // Exponential backoff, capped. Applied before the next attempt so repeated
        // failures space out.
        self->mResubscribeDelayMs =
            (self->mResubscribeDelayMs >= kResubscribeMaxDelayMs / 2) ? kResubscribeMaxDelayMs : self->mResubscribeDelayMs * 2;

        Controller::DeviceCommissioner * commissioner = JFAGetDeviceCommissioner();
        if (commissioner == nullptr)
        {
            ChipLogError(JointFabric, "[jfsub%u] No commissioner at reconnect; releasing slot", self->mSlot);
            self->Release();
            return;
        }

        CASESessionManager * caseSessionManager = commissioner->CASESessionMgr();
        if (caseSessionManager == nullptr)
        {
            ChipLogError(JointFabric, "[jfsub%u] Commissioner CASESessionManager unavailable; releasing slot", self->mSlot);
            self->Release();
            return;
        }

        ChipLogProgress(JointFabric, "[jfsub%u] Re-establishing CASE to node=0x" ChipLogFormatX64, self->mSlot,
                        ChipLogValueX64(self->mPeer.GetNodeId()));

        // FindOrEstablishSession runs on the commissioner's stack, so the
        // resulting session and its UDP endpoint belong to the commissioner, not
        // the Server's Interaction Model engine.
        caseSessionManager->FindOrEstablishSession(self->mPeer, &self->mOnReconnectedCallback, &self->mOnReconnectFailureCallback);
    }

    static void OnReconnected(void * context, Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
    {
        auto * self = static_cast<WildcardSubscription *>(context);

        ChipLogProgress(JointFabric, "[jfsub%u] CASE re-established to node=0x" ChipLogFormatX64 ", reopening subscription",
                        self->mSlot, ChipLogValueX64(self->mPeer.GetNodeId()));

        CHIP_ERROR err = self->Subscribe(exchangeMgr, sessionHandle);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(JointFabric, "[jfsub%u] Resubscribe SendRequest failed: %" CHIP_ERROR_FORMAT, self->mSlot, err.Format());
            // ReadClient was destroyed inside Subscribe() on failure; back off and
            // retry via the timer.
            self->ScheduleReconnect();
        }
    }

    static void OnReconnectFailure(void * context, const ScopedNodeId & peerId, CHIP_ERROR error)
    {
        auto * self = static_cast<WildcardSubscription *>(context);
        ChipLogError(JointFabric, "[jfsub%u] CASE re-establish to 0x" ChipLogFormatX64 " failed: %" CHIP_ERROR_FORMAT, self->mSlot,
                     ChipLogValueX64(peerId.GetNodeId()), error.Format());
        self->ScheduleReconnect();
    }

    BufferedReadCallback mBufferedAdapter;
    Platform::UniquePtr<ReadClient> mReadClient;
    chip::Callback::Callback<chip::OnDeviceConnected> mOnReconnectedCallback;
    chip::Callback::Callback<chip::OnDeviceConnectionFailure> mOnReconnectFailureCallback;
    unsigned mSlot = 0;
    ScopedNodeId mPeer;
    SubscriptionId mSubscriptionId = 0;
    uint32_t mResubscribeDelayMs   = kResubscribeInitialDelayMs;
    uint32_t mResubscribeAttempts  = 0;
    bool mInUse                    = false;
    bool mReportComplete           = false;
};

// Fixed pool of concurrent subscription contexts, one per subscribed peer.
WildcardSubscription gSubscriptions[kMaxConcurrentSubscriptions];
bool gSlotsInitialized = false;

void EnsureSlotsInitialized()
{
    if (!gSlotsInitialized)
    {
        for (size_t i = 0; i < kMaxConcurrentSubscriptions; i++)
        {
            gSubscriptions[i].SetSlot(static_cast<unsigned>(i));
        }
        gSlotsInitialized = true;
    }
}

// Returns the slot already owning peer, or a free slot, or nullptr if full.
WildcardSubscription * AcquireSlotForPeer(NodeId peer)
{
    for (auto & sub : gSubscriptions)
    {
        if (sub.InUse() && sub.GetPeer() == peer)
        {
            return &sub;
        }
    }
    for (auto & sub : gSubscriptions)
    {
        if (!sub.InUse())
        {
            return &sub;
        }
    }
    return nullptr;
}

} // namespace

void OpenWildcardSubscription(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & session, const ScopedNodeId & peer)
{
    EnsureSlotsInitialized();

    WildcardSubscription * sub = AcquireSlotForPeer(peer.GetNodeId());
    VerifyOrReturn(
        sub != nullptr,
        ChipLogError(JointFabric, "No free subscription slot (max=%u)", static_cast<unsigned>(kMaxConcurrentSubscriptions)));

    // A slot already established for this peer needs no action.
    if (sub->InUse() && sub->GetPeer() == peer.GetNodeId() && sub->ReportComplete())
    {
        ChipLogProgress(JointFabric, "[jfsub%u] Subscription already active for node=0x" ChipLogFormatX64, sub->GetSlot(),
                        ChipLogValueX64(peer.GetNodeId()));
        return;
    }

    sub->Claim(peer);

    CHIP_ERROR err = sub->Subscribe(exchangeMgr, session);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(JointFabric, "[jfsub%u] Failed to send subscribe request: %" CHIP_ERROR_FORMAT, sub->GetSlot(), err.Format());
        // The initial subscribe failed outright; free the slot (no resubscribe is
        // scheduled since nothing was ever established).
        sub->Release();
        return;
    }

    ChipLogProgress(JointFabric, "[jfsub%u] Wildcard subscribe request sent to node=0x" ChipLogFormatX64, sub->GetSlot(),
                    ChipLogValueX64(peer.GetNodeId()));
}

} // namespace JFA
} // namespace App
} // namespace NXP
} // namespace chip
