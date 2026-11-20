#pragma once

#include <cstddef>

namespace tracking
{
    // What one IEventStore::Insert call did.
    struct InsertResult
    {
        std::size_t stored = 0;
        std::size_t duplicates = 0;
    };
} // namespace tracking
