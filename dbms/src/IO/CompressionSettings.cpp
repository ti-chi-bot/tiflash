#include "CompressionSettings.h"

#include <Interpreters/Settings.h>
#include <lz4hc.h>


namespace DB
{
CompressionSettings::CompressionSettings(const Settings & settings)
{
    method = settings.network_compression_method;
    switch (method)
    {
    case CompressionMethod::ZSTD:
        level = settings.network_zstd_compression_level;
        break;
    default:
        level = getDefaultLevel(method);
    }
}

int CompressionSettings::getDefaultLevel(CompressionMethod method)
{
    switch (method)
    {
    case CompressionMethod::LZ4:
        return 1;
    case CompressionMethod::LZ4HC:
        return LZ4HC_CLEVEL_DEFAULT;
    case CompressionMethod::ZSTD:
        return 1;
    default:
        return -1;
    }
}

} // namespace DB
