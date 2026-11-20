#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include "sdk/schema/record_builder.h"
#include "sdk/schema/schema_builder.h"

namespace tracking
{
    // What Track() accepts: a name, a version, and a Describe that works with both builders.
    template <class E>
    concept TrackableEvent = requires(RecordBuilder<E>& record_builder, SchemaBuilder<E>& schema_builder)
    {
        { E::kName } -> std::convertible_to<std::string_view>;
        { E::kVersion } -> std::convertible_to<std::uint32_t>;
        E::Describe(record_builder);
        E::Describe(schema_builder);
    };
} // namespace tracking
