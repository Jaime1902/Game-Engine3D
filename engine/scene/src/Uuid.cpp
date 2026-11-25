#include "ge3d/Uuid.hpp"

#include <random>

namespace ge3d {

Uuid Uuid::generate() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();
    Uuid id{engine(), engine()};
    // Versión 4 en los bits 12-15 de "high" y variante RFC 4122 (10xx) en los bits altos de "low".
    id.high = (id.high & ~std::uint64_t{0xF000}) | std::uint64_t{0x4000};
    id.low = (id.low & ~(std::uint64_t{0xC} << 60)) | (std::uint64_t{0x8} << 60);
    return id;
}

std::string Uuid::toString() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) out.push_back('-');
        const std::uint64_t part = i < 16 ? high : low;
        const int shift = 60 - 4 * (i % 16);
        out.push_back(digits[(part >> shift) & 0xF]);
    }
    return out;
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    Uuid id;
    int nibble = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return std::nullopt;
            continue;
        }
        int value = 0;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else return std::nullopt;
        auto& part = nibble < 16 ? id.high : id.low;
        part = (part << 4) | static_cast<std::uint64_t>(value);
        ++nibble;
    }
    return id;
}

}
