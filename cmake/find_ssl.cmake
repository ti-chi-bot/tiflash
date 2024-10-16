option (USE_INTERNAL_SSL_LIBRARY "Set to FALSE to use system *ssl library instead of bundled" ${MSVC})

# Using dynamic lib is convenient for developing.
# In release-build environment, we only need to install static libs of openssl and curl
#set (OPENSSL_USE_STATIC_LIBS ${USE_STATIC_LIBRARIES})

if (NOT USE_INTERNAL_SSL_LIBRARY)
    if (APPLE)
        set (OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        # https://rt.openssl.org/Ticket/Display.html?user=guest&pass=guest&id=2232
        if (USE_STATIC_LIBRARIES)
            message (WARNING "Disable USE_STATIC_LIBRARIES if you have linking problems with OpenSSL on MacOS")
        endif ()
    endif ()

    find_package (OpenSSL)

    if (NOT OPENSSL_FOUND)
        set (OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        find_package (OpenSSL)
    endif ()
endif ()

if (NOT OPENSSL_FOUND)
    message (WARNING "Try to use internal ssl library")
    set (USE_INTERNAL_SSL_LIBRARY 1)
    set (OPENSSL_ROOT_DIR "${ClickHouse_SOURCE_DIR}/contrib/ssl")
    set (OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
    if (NOT USE_STATIC_LIBRARIES AND TARGET crypto-shared AND TARGET ssl-shared)
        set (OPENSSL_CRYPTO_LIBRARY crypto-shared)
        set (OPENSSL_SSL_LIBRARY ssl-shared)
    else ()
        set (OPENSSL_CRYPTO_LIBRARY crypto)
        set (OPENSSL_SSL_LIBRARY ssl)
    endif ()
    set (OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    set (OPENSSL_FOUND 1)
endif ()

message (STATUS "Using ssl=${OPENSSL_FOUND}: ${OPENSSL_INCLUDE_DIR} : ${OPENSSL_LIBRARIES}")
