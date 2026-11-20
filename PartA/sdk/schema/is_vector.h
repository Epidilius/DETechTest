#pragma once

#include <type_traits>
#include <vector>

namespace tracking
{
    // True for a std::vector, which is how an event member becomes an array.
    template <class T>
    struct IsVector : std::false_type
    {
    };

    template <class T>
    struct IsVector<std::vector<T>> : std::true_type
    {
    };
} // namespace tracking
