#!/bin/bash

if [[ ! -d $PW_PROJECT_ROOT ]]; then
    echo "PW_PROJECT_ROOT is not set, run activate.sh script"
    exit 1
fi

SOURCE=${BASH_SOURCE[0]}
SOURCE_DIR=$(cd "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)

# Mbedtls header files

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_check_config_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_config_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_oid_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_pk_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_pk_internal_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_platform_h.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_platform_util_h.patch"

# Mbedtls source files

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_oid_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_pk_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_pkparse_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_pkwrite_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_platform_util_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_x509_crt_c.patch"

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo -p1 <"$SOURCE_DIR/fixes/mbedtls_x509write_csr_c.patch"

# Openthead

patch -N --binary -d "$PW_PROJECT_ROOT"/third_party/openthread/repo -p1 <"$SOURCE_DIR/fixes/openthread_ecdsa_cpp.patch"

# Tinycrypt changes

if [ ! -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo/include/tinycrypt ]; then
	cp -r "$SOURCE_DIR"/new_files/tinycrypt_h "$PW_PROJECT_ROOT"/third_party/mbedtls/repo/include/tinycrypt
else
	echo "$PW_PROJECT_ROOT/third_party/mbedtls/repo/include/tinycrypt directory already exists."
fi

if [ ! -d "$PW_PROJECT_ROOT"/third_party/mbedtls/repo/tinycrypt ]; then
	cp -r "$SOURCE_DIR"/new_files/tinycrypt_c "$PW_PROJECT_ROOT"/third_party/mbedtls/repo/tinycrypt
else
	echo "$PW_PROJECT_ROOT/third_party/mbedtls/repo/tinycrypt directory already exists."
fi

echo "Tinycrypt patching done."
exit 0
