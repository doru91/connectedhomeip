#!/bin/bash

SOURCE=${BASH_SOURCE[0]}
SOURCE_DIR=$(cd "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)

sed -i 's/"CPU_K32W061HN"/"CPU_K32W041AMZ",\n      "K32WMCM_APP_BUILD"/g' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i 's/"CPU_K32W061HN"/"CPU_K32W041AMZ",\n      "K32WMCM_APP_BUILD"/g' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i 's/k32w061dk6/k32w041amdk6/g' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i '24,$s/K32W061/K32W041AM/g' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i 's/k32w061/k32w041am/g' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i '/ntag_i2c_plus/d' ${SOURCE_DIR}/k32w0_sdk.gni
sed -i '/fsl_ntag/d' ${SOURCE_DIR}/k32w0_sdk.gni

##line=`grep -n "middleware/wireless/ieee-802.15.4/uMac/Include" ${SOURCE_DIR}/k32w0_sdk.gni | awk -F: '{print $1}'`

##echo $line
sed -i '/ieee-802.15.4\/uMac\/Include/a\      "${k32w0_sdk_root}/middleware/wireless/ieee-802.15.4/Include",\n      "${k32w0_sdk_root}/middleware/wireless/ieee-802.15.4/mMac/Include",' ${SOURCE_DIR}/k32w0_sdk.gni

sed -i '/\"JENNIC_CHIP_FAMILY_NAME=_JN518x\"/a\      "JENNIC_CHIP_FAMILY_JN518x",' ${SOURCE_DIR}/k32w0_sdk.gni


sed -i 's/k32w061dk6/k32w041amdk6/g' ${SOURCE_DIR}/sdk_fixes/patch_k32w_sdk.sh
