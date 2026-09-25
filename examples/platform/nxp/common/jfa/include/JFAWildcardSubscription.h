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

// OpenWildcardSubscription() opens a subscription for (endpoint=*, cluster=*,
// attribute=*) over an existing CASE session. It is meant to be called from the
// JFA onboarding-complete path (JFAManager::OnCommissioningCompleteResponse) so
// that the commissionee is subscribed to immediately after it is onboarded, and
// from the post-reboot restore path (JFAManager::OnRestoreSubscriptionsTimer).
//
// The subscription owns its own ReadClient and holds its own reference to the
// session, so it stays resident after the caller releases the transient command
// session used to finalize commissioning. If the peer later drops (for example
// when a commissionee reboots) the subscription re-establishes CASE through the
// JFA commissioner's own CASESessionManager and reopens itself, keeping all
// traffic on the commissioner's UDP endpoint.

#pragma once

#include <lib/core/DataModelTypes.h>
#include <lib/core/ScopedNodeId.h>
#include <messaging/ExchangeMgr.h>
#include <transport/Session.h>

namespace chip {
namespace NXP {
namespace App {
namespace JFA {

// Open a wildcard attribute subscription (all endpoints, all clusters, all
// attributes) toward peer over the supplied exchange manager and secure session.
//
// The peer is identified by a ScopedNodeId so the subscription can re-establish
// CASE through the JFA commissioner's own CASESessionManager if the peer later
// drops. If a subscription for peer already exists it is left untouched. If no
// subscription slot is free the request is dropped with an error log.
void OpenWildcardSubscription(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & session, const ScopedNodeId & peer);

} // namespace JFA
} // namespace App
} // namespace NXP
} // namespace chip
