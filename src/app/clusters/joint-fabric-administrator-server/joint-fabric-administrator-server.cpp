/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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
 * @brief Implementation for the Joint Fabric Administrator Cluster
 ***************************************************************************/

#include <access/AccessControl.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/CommandHandler.h>
#include <app/ConcreteCommandPath.h>
#include <app/EventLogging.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/reporting.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <credentials/CHIPCert.h>
#include <credentials/CertificationDeclaration.h>
#include <credentials/DeviceAttestationConstructor.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPSafeCasts.h>
#include <lib/core/PeerId.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedBuffer.h>
#include <lib/support/TestGroupData.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <string.h>
#include <tracing/macros.h>

#include "joint-fabric-administrator-server.h"

using namespace chip;
using namespace chip::Transport;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::JointFabricAdministrator;
using namespace chip::Controller;
using namespace chip::Credentials;
using namespace chip::Crypto;
using namespace chip::Protocols::InteractionModel;

void MatterJointFabricAdministratorPluginServerInitCallback() {}

bool emberAfJointFabricAdministratorClusterICACCSRRequestCallback(chip::app::CommandHandler* a, chip::app::ConcreteCommandPath const& b, chip::app::Clusters::JointFabricAdministrator::Commands::ICACCSRRequest::DecodableType const& c)
{
    return TRUE;
}

bool emberAfJointFabricAdministratorClusterAddICACCallback(chip::app::CommandHandler* a, chip::app::ConcreteCommandPath const& b, chip::app::Clusters::JointFabricAdministrator::Commands::AddICAC::DecodableType const& c)
{
    return TRUE;
}

bool emberAfJointFabricAdministratorClusterOpenJointCommissioningWindowCallback(chip::app::CommandHandler* a, chip::app::ConcreteCommandPath const& b, chip::app::Clusters::JointFabricAdministrator::Commands::OpenJointCommissioningWindow::DecodableType const& c)
{
    return TRUE;
}



