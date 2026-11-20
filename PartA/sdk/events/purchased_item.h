#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sdk/schema/required.h"
#include "sdk/schema/trackable_event.h"

namespace tracking
{
    struct PurchasedItem
    {
        static constexpr std::string_view kName = "PURCHASED_ITEM";
        static constexpr std::uint32_t kVersion = 1;

        Required<std::string> item;
        Required<std::int32_t> quantity;

        static void Describe(auto& visit)
        {
            visit("item", &PurchasedItem::item);
            visit("quantity", &PurchasedItem::quantity);
        }
    };

    static_assert(TrackableEvent<PurchasedItem>);
} // namespace tracking
