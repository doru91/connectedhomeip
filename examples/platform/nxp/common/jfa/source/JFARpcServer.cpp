/*
 *
 *    Copyright (c) 2025-2026 Project CHIP Authors
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

#include "pigweed/rpc_services/JointFabric.h"

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/support/logging/CHIPLogging.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

using namespace chip;

namespace joint_fabric_service {

constexpr uint32_t kRpcTimeoutMs                = 1000;
constexpr uint32_t kCrossSignedICACRpcTimeoutMs = 10000;

// Binary semaphore used to signal that ResponseStream() has delivered the ICAC
// CSR bytes requested by GetICACCSRForJF(). Created on first use.
SemaphoreHandle_t responseSem = nullptr;
bool responseReceived         = false;

uint8_t icacCSRBuf[Crypto::kMIN_CSR_Buffer_Size] = { 0 };
MutableByteSpan icacCSRSpan{ icacCSRBuf };

// Binary semaphore used to signal that ResponseStream() has delivered the
// cross-signed ICAC bytes requested by GetCrossSignedICAC(). Created on first use.
SemaphoreHandle_t crossSignedICACSem = nullptr;
bool crossSignedICACReceived         = false;
uint8_t crossSignedICACBuf[Credentials::kMaxDERCertLength] = { 0 };
MutableByteSpan crossSignedICACSpan{ crossSignedICACBuf };

// Binary semaphore used to signal that ResponseStream() has delivered the device
// NOC (plus JF materials) requested by GetNOCForDevice(). Created on first use.
SemaphoreHandle_t deviceNOCSem = nullptr;
bool deviceNOCReceived         = false;
uint8_t deviceNOCBuf[Credentials::kMaxDERCertLength] = { 0 };
MutableByteSpan deviceNOCSpan{ deviceNOCBuf };
uint8_t deviceICACBuf[Credentials::kMaxDERCertLength] = { 0 };
MutableByteSpan deviceICACSpan{ deviceICACBuf };
uint8_t deviceRCACBuf[Credentials::kMaxDERCertLength] = { 0 };
MutableByteSpan deviceRCACSpan{ deviceRCACBuf };
uint8_t deviceIPKBuf[Crypto::CHIP_CRYPTO_SYMMETRIC_KEY_LENGTH_BYTES] = { 0 };
MutableByteSpan deviceIPKSpan{ deviceIPKBuf };


::pw::Status JointFabric::TransferOwnership(const ::OwnershipContext & request, ::pw_protobuf_Empty & response)
{
    ChipLogProgress(JointFabric, "RPC Ownership Transfer for NodeId: 0x" ChipLogFormatX64 ", jcm=%d",
                    ChipLogValueX64(request.node_id), request.jcm);

    if (request.jcm && (Crypto::kP256_PublicKey_Length != request.trustedIcacPublicKeyB.size))
    {
        ChipLogError(JointFabric, "Invalid ICAC Public Key Size");
        return pw::Status::OutOfRange();
    }

    if (request.jcm && request.peerAdminJFAdminClusterEndpointId == kInvalidEndpointId)
    {
        ChipLogError(JointFabric, "Invalid Peer Admin Endpoint ID for the JF Administrator Cluster");
        return pw::Status::OutOfRange();
    }

    OwnershipTransferContext * data = Platform::New<OwnershipTransferContext>(
        request.node_id, request.jcm, ByteSpan(request.trustedIcacPublicKeyB.bytes, request.trustedIcacPublicKeyB.size),
        request.peerAdminJFAdminClusterEndpointId);
    VerifyOrReturnValue(data, pw::Status::Internal());
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(FinalizeCommissioningWork, reinterpret_cast<intptr_t>(data));

    return pw::OkStatus();
}

void JointFabric::GetStream(const ::pw_protobuf_Empty & request, ServerWriter<::RequestOptions> & writer)
{
    ChipLogProgress(JointFabric, "GetStream Opened");
    rpcGetStream = std::move(writer);

    return;
}

::pw::Status JointFabric::ResponseStream(const ::Response & responseBytes, ::pw_protobuf_Empty & response)
{
    ChipLogProgress(JointFabric, "RPC ResponseStream, transaction_type=%d", responseBytes.transaction_type);

    switch (responseBytes.transaction_type)
    {
    case TransactionType::TransactionType_CROSS_SIGNED_ICAC: {
        // Reset the span to full buffer capacity before copying.
        crossSignedICACSpan = MutableByteSpan(crossSignedICACBuf, sizeof(crossSignedICACBuf));
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.response_bytes.bytes, responseBytes.response_bytes.size), crossSignedICACSpan);
        crossSignedICACReceived = true;
        if (crossSignedICACSem != nullptr)
        {
            xSemaphoreGive(crossSignedICACSem);
        }
        break;
    }
    case TransactionType::TransactionType_DEVICE_NOC: {
        // Reset the spans to full buffer capacity before copying.
        deviceNOCSpan = MutableByteSpan(deviceNOCBuf, sizeof(deviceNOCBuf));
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.response_bytes.bytes, responseBytes.response_bytes.size), deviceNOCSpan);
        // JFC-B also returns the JF materials it holds (cross-signed ICAC, Fabric-A RCAC, IPK).
        deviceICACSpan = MutableByteSpan(deviceICACBuf, sizeof(deviceICACBuf));
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.icac_bytes.bytes, responseBytes.icac_bytes.size), deviceICACSpan);
        deviceRCACSpan = MutableByteSpan(deviceRCACBuf, sizeof(deviceRCACBuf));
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.rcac_bytes.bytes, responseBytes.rcac_bytes.size), deviceRCACSpan);
        deviceIPKSpan = MutableByteSpan(deviceIPKBuf, sizeof(deviceIPKBuf));
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.ipk_bytes.bytes, responseBytes.ipk_bytes.size), deviceIPKSpan);
        deviceNOCReceived = true;
        if (deviceNOCSem != nullptr)
        {
            xSemaphoreGive(deviceNOCSem);
        }
        break;
    }
    case TransactionType::TransactionType_ICAC_CSR:
    default: {
        TEMPORARY_RETURN_IGNORED CopySpanToMutableSpan(
            ByteSpan(responseBytes.response_bytes.bytes, responseBytes.response_bytes.size), icacCSRSpan);
        responseReceived = true;
        if (responseSem != nullptr)
        {
            xSemaphoreGive(responseSem);
        }
        break;
    }
    }

    return pw::OkStatus();
}

CHIP_ERROR JointFabric::GetICACCSRForJF(MutableByteSpan & icacCSR)
{
    if (responseSem == nullptr)
    {
        responseSem = xSemaphoreCreateBinary();
        VerifyOrReturnError(responseSem != nullptr, CHIP_ERROR_NO_MEMORY);
    }

    ::pw::Status status;

    // Drain any stale signal/response left over from a previous request that
    // timed out here but was answered late by ResponseStream(). Without this,
    // the xSemaphoreTake() below could return immediately with a CSR that
    // belongs to the earlier request (potentially a different key).
    responseReceived = false;
    while (xSemaphoreTake(responseSem, 0) == pdTRUE)
    {
        // Discard pending gives from late responses.
    }

    // JFA requests an ICAC CSR from JFC
    RequestOptions requestOptions{ TransactionType::TransactionType_ICAC_CSR };
    status = rpcGetStream.Write(requestOptions);

    if (pw::OkStatus() != status)
    {
        ChipLogError(JointFabric, "Writing to GetStream failed");

        return CHIP_ERROR_SHUT_DOWN;
    }

    // wait for the ICAC CSR from JFC
    if (xSemaphoreTake(responseSem, pdMS_TO_TICKS(kRpcTimeoutMs)) == pdTRUE && responseReceived)
    {
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(icacCSRSpan.data(), icacCSRSpan.size()), icacCSR));
        responseReceived = false;

        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_TIMEOUT;
}

CHIP_ERROR JointFabric::GetCrossSignedICAC(chip::FabricId anchorFabricId, chip::ByteSpan csr, chip::MutableByteSpan & icac)
{
    if (crossSignedICACSem == nullptr)
    {
        crossSignedICACSem = xSemaphoreCreateBinary();
        VerifyOrReturnError(crossSignedICACSem != nullptr, CHIP_ERROR_NO_MEMORY);
    }

    // Drain any stale signal left over from a late response to a previous request.
    crossSignedICACReceived = false;
    while (xSemaphoreTake(crossSignedICACSem, 0) == pdTRUE)
    {
        // Discard pending gives from late responses.
    }

    // Send the commissionee CSR to the JFC for cross-signing.
    RequestOptions requestOptions{};
    requestOptions.transaction_type = TransactionType::TransactionType_CROSS_SIGNED_ICAC;
    requestOptions.anchor_fabric_id = anchorFabricId;
    requestOptions.csr_bytes.size   = static_cast<uint16_t>(csr.size());
    memcpy(requestOptions.csr_bytes.bytes, csr.data(), csr.size());

    ::pw::Status status = rpcGetStream.Write(requestOptions);
    if (pw::OkStatus() != status)
    {
        ChipLogError(JointFabric, "GetCrossSignedICAC: Writing to GetStream failed");
        return CHIP_ERROR_SHUT_DOWN;
    }

    // Wait for the cross-signed ICAC from JFC.
    if (xSemaphoreTake(crossSignedICACSem, pdMS_TO_TICKS(kCrossSignedICACRpcTimeoutMs)) == pdTRUE && crossSignedICACReceived)
    {
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(crossSignedICACSpan.data(), crossSignedICACSpan.size()), icac));
        crossSignedICACReceived = false;
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_TIMEOUT;
}

CHIP_ERROR JointFabric::PushJFMaterials(chip::ByteSpan crossSignedICAC, chip::ByteSpan anchorRCAC, chip::ByteSpan anchorIPK)
{
    VerifyOrReturnError(crossSignedICAC.size() <= sizeof(RequestOptions{}.icac_bytes.bytes), CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(anchorRCAC.size() <= sizeof(RequestOptions{}.rcac_bytes.bytes), CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(anchorIPK.size() <= sizeof(RequestOptions{}.ipk_bytes.bytes), CHIP_ERROR_BUFFER_TOO_SMALL);

    RequestOptions requestOptions{};
    requestOptions.transaction_type = TransactionType::TransactionType_JF_MATERIALS;
    requestOptions.icac_bytes.size  = static_cast<uint16_t>(crossSignedICAC.size());
    memcpy(requestOptions.icac_bytes.bytes, crossSignedICAC.data(), crossSignedICAC.size());
    requestOptions.rcac_bytes.size = static_cast<uint16_t>(anchorRCAC.size());
    memcpy(requestOptions.rcac_bytes.bytes, anchorRCAC.data(), anchorRCAC.size());
    requestOptions.ipk_bytes.size = static_cast<uint16_t>(anchorIPK.size());
    memcpy(requestOptions.ipk_bytes.bytes, anchorIPK.data(), anchorIPK.size());

    ::pw::Status status = rpcGetStream.Write(requestOptions);
    if (pw::OkStatus() != status)
    {
        ChipLogError(JointFabric, "PushJFMaterials: Writing to GetStream failed");
        return CHIP_ERROR_SHUT_DOWN;
    }

    ChipLogProgress(JointFabric, "PushJFMaterials: sent ICAC (%u bytes) + RCAC (%u bytes) + IPK (%u bytes) to JFC-B",
                    static_cast<unsigned>(crossSignedICAC.size()), static_cast<unsigned>(anchorRCAC.size()),
                    static_cast<unsigned>(anchorIPK.size()));
    return CHIP_NO_ERROR;
}

CHIP_ERROR JointFabric::ClearJFMaterials()
{
    RequestOptions requestOptions{};
    requestOptions.transaction_type = TransactionType::TransactionType_CLEAR_JF_MATERIALS;

    ::pw::Status status = rpcGetStream.Write(requestOptions);
    if (pw::OkStatus() != status)
    {
        ChipLogError(JointFabric, "ClearJFMaterials: Writing to GetStream failed");
        return CHIP_ERROR_SHUT_DOWN;
    }
    ChipLogProgress(JointFabric, "ClearJFMaterials: sent CLEAR_JF_MATERIALS to JFC-B");
    return CHIP_NO_ERROR;
}

CHIP_ERROR JointFabric::GetNOCForDevice(chip::NodeId deviceNodeId, chip::ByteSpan csr, chip::MutableByteSpan & noc,
                                        chip::MutableByteSpan & icac, chip::MutableByteSpan & rcac, chip::MutableByteSpan & ipk)
{
    if (deviceNOCSem == nullptr)
    {
        deviceNOCSem = xSemaphoreCreateBinary();
        VerifyOrReturnError(deviceNOCSem != nullptr, CHIP_ERROR_NO_MEMORY);
    }

    // Drain any stale signal left over from a late response to a previous request.
    deviceNOCReceived = false;
    while (xSemaphoreTake(deviceNOCSem, 0) == pdTRUE)
    {
        // Discard pending gives from late responses.
    }

    // Send the device CSR to JFC-B so it can issue a Fabric-A NOC for migrating the device into Fabric A.
    // No fabric id is transmitted; JFC-B derives it internally from the cross-signed ICAC it holds.
    RequestOptions requestOptions{};
    requestOptions.transaction_type = TransactionType::TransactionType_DEVICE_NOC;
    requestOptions.node_id          = deviceNodeId;
    requestOptions.csr_bytes.size   = static_cast<uint16_t>(csr.size());
    memcpy(requestOptions.csr_bytes.bytes, csr.data(), csr.size());

    ::pw::Status status = rpcGetStream.Write(requestOptions);
    if (pw::OkStatus() != status)
    {
        ChipLogError(JointFabric, "GetNOCForDevice: Writing to GetStream failed");
        return CHIP_ERROR_SHUT_DOWN;
    }

    // Wait for the device NOC (plus the JF materials) from JFC-B.
    if (xSemaphoreTake(deviceNOCSem, pdMS_TO_TICKS(kCrossSignedICACRpcTimeoutMs)) == pdTRUE && deviceNOCReceived)
    {
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(deviceNOCSpan.data(), deviceNOCSpan.size()), noc));
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(deviceICACSpan.data(), deviceICACSpan.size()), icac));
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(deviceRCACSpan.data(), deviceRCACSpan.size()), rcac));
        ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan(deviceIPKSpan.data(), deviceIPKSpan.size()), ipk));
        deviceNOCReceived = false;
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_TIMEOUT;
}

void JointFabric::CloseStreams()
{
    rpcGetStream.Finish();
}

} // namespace joint_fabric_service
