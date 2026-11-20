#pragma once

#include <optional>
#include <type_traits>

#include "sdk/schema/required.h"

namespace tracking
{
    // Every member is Required<T> or std::optional<T>.
    // Anything else has no Unwrap specialisation and fails to compile, so each field states whether it must be sent.
    // `Unwrap<M>::type` is the type inside the wrapper.

    template <class M>
    struct Unwrap;

    template <class T>
    struct Unwrap<Required<T>> : std::type_identity<T>
    {
        static constexpr bool required = true;
    };

    template <class T>
    struct Unwrap<std::optional<T>> : std::type_identity<T>
    {
        static constexpr bool required = false;
    };
} // namespace tracking
