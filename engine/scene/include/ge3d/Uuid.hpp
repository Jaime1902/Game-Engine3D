#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ge3d {

// Identificador persistente UUID v4 (RFC 4122). Se guarda en texto canónico:
// xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
struct Uuid {
    std::uint64_t high{};
    std::uint64_t low{};

    [[nodiscard]] bool isNil() const noexcept { return high == 0 && low == 0; }
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] static Uuid generate();
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text);

    friend bool operator==(const Uuid&, const Uuid&) = default;
    friend auto operator<=>(const Uuid&, const Uuid&) = default;
};

}
