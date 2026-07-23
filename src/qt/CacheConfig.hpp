#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <system_error>

namespace amrvis::qt {

inline constexpr std::uint64_t defaultInitialCacheBudget
    = 1ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::string_view cacheSizeEnvironmentVariable
    = "AMRVIS_CACHE_SIZE_MB";
inline constexpr std::uint64_t bytesPerMegabyte = 1024ULL * 1024ULL;

inline std::uint64_t initialCacheBudget(
    const char* configuredValue
    = std::getenv(cacheSizeEnvironmentVariable.data())) noexcept
{
    if (configuredValue == nullptr) {
        return defaultInitialCacheBudget;
    }

    const std::string_view text(configuredValue);
    std::uint64_t megabytes = 0;
    const auto [end, error]
        = std::from_chars(text.data(), text.data() + text.size(), megabytes);
    if (error != std::errc{} || end != text.data() + text.size()
        || megabytes == 0
        || megabytes > std::numeric_limits<std::uint64_t>::max() / bytesPerMegabyte) {
        return defaultInitialCacheBudget;
    }
    return megabytes * bytesPerMegabyte;
}

} // namespace amrvis::qt
