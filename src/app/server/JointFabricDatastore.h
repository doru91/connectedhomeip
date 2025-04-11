/*
  *
  *    Copyright (c) 2024 Project CHIP Authors
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
 
 #include <app-common/zap-generated/cluster-objects.h>
 #include <lib/core/CHIPPersistentStorageDelegate.h>
 
 namespace chip {
 namespace app {
 
 class JointFabricDatastore
 {
 public:
     static JointFabricDatastore & GetInstance()
     {
         static JointFabricDatastore sInstance;
         return sInstance;
     }
 
     CHIP_ERROR Init(PersistentStorageDelegate & persistentStorage);
 
     CHIP_ERROR AddPendingNode(FabricIndex fabricId, NodeId nodeId, const CharSpan & friendlyName);
 
 private:
     static constexpr size_t kMaxNodes = 32;
 
     Clusters::JointFabricDatastore::Structs::DatastoreNodeInformationEntry::Type mNodeInformationEntries[kMaxNodes] = {};
     size_t mNodeInformationEntriesCount                                                                             = 0;
 };
 
 } // namespace app
 } // namespace chip
