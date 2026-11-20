#pragma once

#include <utility>

namespace tracking
{
    // A field that must be sent.
    template <class T>
    struct Required
    {
        T value;

        template <class U>
        requires requires(U&& source) { T{std::forward<U>(source)}; }
        Required(U&& source) : value{std::forward<U>(source)}
        {
        }
    };
} // namespace tracking
