#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace slash {
struct Range { std::size_t offset; std::size_t size; };
// Operates on an already mapped PE image. Both string and target are bounded.
std::vector<std::size_t> locate(std::span<const std::uint8_t> image,
                               std::span<const Range> executable);
std::vector<std::size_t> locateEventDispatch(std::span<const std::uint8_t> image,
                                            std::span<const Range> executable);
}
