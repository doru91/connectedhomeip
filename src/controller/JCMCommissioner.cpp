/*
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
 
 #include "JCMCommissioner.h"
 #include <app-common/zap-generated/ids/Attributes.h>
 #include <app-common/zap-generated/ids/Clusters.h>
 #include <app/InteractionModelEngine.h>
 #include <lib/core/CHIPCore.h>
 #include <lib/core/CHIPError.h>
 #include <controller/JCMCommissioner.h>
 
 using namespace ::chip;
 using namespace ::chip::app;
 using namespace chip::app::Clusters;
 using chip::app::ReadClient;
 
 namespace chip {
 namespace Controller {
 
 JCMCommissioner::JCMCommissioner()
 {
     mJCMCommissionerCompleteCallback = nullptr;
     mAttributeCache = nullptr;
 }
 
 CHIP_ERROR JCMCommissioner::Start(
     DeviceProxy * proxy,
     chip::Callback::Callback<JCMCommissionerCompleteCallback> *callback)
 {
     VerifyOrReturnError(proxy != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
     VerifyOrReturnError(callback != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
 
     mNextStage = JCMCommissionerStage::kStarted;
     mJCMCommissionerCompleteCallback = callback;
     mDeviceProxy = proxy;
     mAttributeCache = Platform::MakeUnique<chip::app::ClusterStateCache>(*this);
 
     AdvanceStage(JCMCommissionerResult::kSuccess);
     
     return CHIP_NO_ERROR;  
 }
 
 void JCMCommissioner::DiscoverAdministratorEndpoint()
 {
     ChipLogProgress(Controller, "Discovering Joint Fabric Administrator endpoint");
 
 #if 1
     Optional<System::Clock::Timeout> mTimeout;
 
     // Create attribute path for ServerList on all endpoints
     AttributePathParams attributePath;
     attributePath.mEndpointId = 0xFFFFU;
     attributePath.mClusterId = chip::app::Clusters::Descriptor::Id;
     attributePath.mAttributeId = chip::app::Clusters::Descriptor::Attributes::DeviceTypeList::Id;
     
     auto error = SendCommissioningReadRequest(mTimeout, &attributePath, 1);
     if (error != CHIP_NO_ERROR)
     {
         AdvanceStage(JCMCommissionerResult::kInternalError);
     }
 #else
     AdvanceStage(JCMCommissionerResult::kSuccess);
 #endif
 }
 
 void JCMCommissioner::ReadAdministratorFabricIndex()
 {
     ChipLogProgress(Controller, "Reading Administrator Fabric Index");
 
     // TODO: Implement the read request for Administrator Fabric Index
 
     AdvanceStage(JCMCommissionerResult::kSuccess);
 }
 
 void JCMCommissioner::PerformVendorIDVerificationProcedure()
 {
     ChipLogProgress(Controller, "Performing Vendor ID Verification Procedure");
 
     // TODO: Implement the Vendor ID verification procedure
 
     AdvanceStage(JCMCommissionerResult::kSuccess);
 }
 
 void JCMCommissioner::VerifyNOCContainsAdministratorCAT()
 {
     ChipLogProgress(Controller, "Verifying NOC contains Administrator CAT");
 
     // TODO: Implement the verification of NOC containing Administrator CAT
 
     AdvanceStage(JCMCommissionerResult::kSuccess);
 }
 
 void JCMCommissioner::FindAdministratorEndpoint() {
     ChipLogProgress(Controller, "Searching for the Administrator Endpoint in the ServerList");
 
     // TODO: Parse the mAttributeCache device-type-list here
    
     AdvanceStage(JCMCommissionerResult::kSuccess);
 }
 
 void JCMCommissioner::OnDone(chip::app::ReadClient * readClient)
 {
     ChipLogProgress(Controller, "JCMCommissioner::OnDone called for read client");
     // Check if the read client is valid
     VerifyOrDie(readClient != nullptr && readClient == mReadClient.get());
     mReadClient.reset();
 
     switch (mNextStage)
     {
         case JCMCommissionerStage::kDiscoveringAdministratorEndpoint:
             FindAdministratorEndpoint();
             break;
         default:
             ChipLogError(Controller, "Invalid state in OnDone: %d", static_cast<int>(mNextStage));
             mJCMCommissionerCompleteCallback->mCall(mJCMCommissionerCompleteCallback->mContext, nullptr,
                                                          JCMCommissionerResult::kInternalError);
     }
 
 }
 
 CHIP_ERROR JCMCommissioner::SendCommissioningReadRequest(Optional<System::Clock::Timeout> timeout,
     app::AttributePathParams * readPaths, size_t readPathsSize)
 {
     app::InteractionModelEngine * engine = app::InteractionModelEngine::GetInstance();
     app::ReadPrepareParams readParams(mDeviceProxy->GetSecureSession().Value());
     readParams.mIsFabricFiltered = true;
     if (timeout.HasValue())
     {
         readParams.mTimeout = timeout.Value();
     }
     readParams.mpAttributePathParamsList    = readPaths;
     readParams.mAttributePathParamsListSize = readPathsSize;
     auto attributeCache = std::move(mAttributeCache);
     auto readClient = chip::Platform::MakeUnique<app::ReadClient>(
         engine, mDeviceProxy->GetExchangeManager(), attributeCache->GetBufferedCallback(), app::ReadClient::InteractionType::Read);
     CHIP_ERROR err = readClient->SendRequest(readParams);
     if (err != CHIP_NO_ERROR)
     {
         ChipLogError(Controller, "Failed to send read request: %" CHIP_ERROR_FORMAT, err.Format());
         return err;
     }
     mAttributeCache = std::move(attributeCache);
     mReadClient     = std::move(readClient);
 
     return CHIP_NO_ERROR;
 }
 
 void JCMCommissioner::AdvanceStage(JCMCommissionerResult result)
 {
     if (result != JCMCommissionerResult::kSuccess)
     {
         // Handle error
         ChipLogError(Controller, "Error in Joint Commissioning Trust Verification: %d", static_cast<int>(result));
         mJCMCommissionerCompleteCallback->mCall(mJCMCommissionerCompleteCallback->mContext, nullptr, result);
         return;
     }
 
     switch (mNextStage)
     {
         case chip::Controller::JCMCommissionerStage::kStarted:
             mNextStage = JCMCommissionerStage::kDiscoveringAdministratorEndpoint;
             DiscoverAdministratorEndpoint();
             break;
         case JCMCommissionerStage::kDiscoveringAdministratorEndpoint:
             mNextStage = JCMCommissionerStage::kReadingAdministratorFabricIndex;
             ReadAdministratorFabricIndex();
             break;
         case JCMCommissionerStage::kReadingAdministratorFabricIndex:
             mNextStage = JCMCommissionerStage::kPerformingVendorIDVerificationProcedure;
             PerformVendorIDVerificationProcedure();
             break;
         case JCMCommissionerStage::kPerformingVendorIDVerificationProcedure:
             mNextStage = JCMCommissionerStage::kVerifyingNOCContainsAdministratorCAT;
             VerifyNOCContainsAdministratorCAT();
             break;
         case JCMCommissionerStage::kVerifyingNOCContainsAdministratorCAT:
             // Handle the response for verifying NOC contains administrator CAT
             ChipLogProgress(Controller, "Joint Commissioning Trust Verification completed successfully");
             mNextStage = JCMCommissionerStage::kIdle;
             JCMCommissionerInfo info;
             mJCMCommissionerCompleteCallback->mCall(mJCMCommissionerCompleteCallback->mContext, &info, result);
             break;
         default:
             ChipLogError(Controller, "Invalid state in OnDone: %d", static_cast<int>(mNextStage));
             mJCMCommissionerCompleteCallback->mCall(mJCMCommissionerCompleteCallback->mContext, nullptr, JCMCommissionerResult::kInternalError);
             break;
     }
 }
 
 } // namespace Controller
 } // namespace chip
