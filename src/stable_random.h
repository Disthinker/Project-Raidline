#pragma once

#include <cstdint>
#include <limits>

class Pcg32
{
public:
    explicit Pcg32(
        std::uint64_t seed,
        std::uint64_t stream = 0xda3e39cb94b95bdbULL) noexcept
    {
        state_ = 0;
        increment_ = (stream << 1U) | 1U;
        static_cast<void>(next());
        state_ += seed;
        static_cast<void>(next());
    }

    [[nodiscard]] std::uint32_t next() noexcept
    {
        const std::uint64_t oldState = state_;
        state_ = oldState * 6364136223846793005ULL + increment_;
        const std::uint32_t xorShifted = static_cast<std::uint32_t>(
            ((oldState >> 18U) ^ oldState) >> 27U);
        const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59U);
        return (xorShifted >> rotation) |
               (xorShifted << ((0U - rotation) & 31U));
    }

    [[nodiscard]] std::uint32_t bounded(std::uint32_t bound) noexcept
    {
        if (bound == 0)
        {
            return 0;
        }
        const std::uint32_t threshold = (0U - bound) % bound;
        while (true)
        {
            const std::uint32_t value = next();
            if (value >= threshold)
            {
                return value % bound;
            }
        }
    }

private:
    std::uint64_t state_{};
    std::uint64_t increment_{};
};

