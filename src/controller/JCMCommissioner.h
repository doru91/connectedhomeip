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
 
 #include <app/ClusterStateCache.h>
 #include <app/DeviceProxy.h>
 #include <app/ReadClient.h>
 #include <lib/core/CHIPCallback.h>
 #include <lib/core/CHIPError.h>
 #if CONFIG_DEVICE_LAYER
 #include <platform/CHIPDeviceLayer.h>
 #endif
 namespace chip {
 
 namespace Controller {
  
 struct JCMCommissionerInfo {
     EndpointId endPointId;
 };
 
 enum class JCMCommissionerResult : uint16_t
 {
     kSuccess = 0,
 
     kNotAnAdministrator  = 100,
     // TODO: Add more JCM trust verification errors
 
     kNoMemory = 700,
 
     kInvalidArgument = 800,
 
     kInternalError = 900,
 
     kNotImplemented = 0xFFFFU,
 };
 
 struct JCMCommissionerError
 {
     JCMCommissionerError(JCMCommissionerResult result) : mResult(result) {}
     JCMCommissionerResult mResult;
 };
 
 enum JCMCommissionerStage : uint8_t
 {
     kIdle,
     kStarted,
     kDiscoveringAdministratorEndpoint,
     kReadingAdministratorFabricIndex,
     kPerformingVendorIDVerificationProcedure,
     kVerifyingNOCContainsAdministratorCAT,
 };
 
 typedef void (*JCMCommissionerCompleteCallback)(void * context, JCMCommissionerInfo * info, JCMCommissionerResult result);
 
 class JCMCommissioner : public app::ClusterStateCache::Callback
 {
 public:
     JCMCommissioner();
     ~JCMCommissioner(){};
     CHIP_ERROR Start(
         DeviceProxy * device,
         chip::Callback::Callback<JCMCommissionerCompleteCallback> *callback);
 
     // ClusterStateCache::Callback impl
     void OnDone(app::ReadClient *) override;
 
 private:
     CHIP_ERROR SendCommissioningReadRequest(Optional<System::Clock::Timeout> timeout, app::AttributePathParams * readPaths, size_t readPathsSize);
     
     void FindAdministratorEndpoint();
 
     // Move to next JCM commissioning step
     void AdvanceStage(JCMCommissionerResult result);
 
     // JCM commissioning steps
     void DiscoverAdministratorEndpoint();
     void ReadAdministratorFabricIndex();
     void PerformVendorIDVerificationProcedure();
     void VerifyNOCContainsAdministratorCAT();
 
     chip::Callback::Callback<JCMCommissionerCompleteCallback> *mJCMCommissionerCompleteCallback;
     Platform::UniquePtr<app::ClusterStateCache> mAttributeCache;
     Platform::UniquePtr<app::ReadClient> mReadClient;
     JCMCommissionerStage mNextStage = JCMCommissionerStage::kIdle;
     DeviceProxy * mDeviceProxy = nullptr;
 };
 
 } // namespace Controller
 } // namespace chip
