#pragma once

#include <cstddef>

#include "sdk/data/send_result.h"

namespace tracking
{
    struct IngestResult
    {
        bool authorized = true;
        std::size_t stored = 0;
        std::size_t duplicates = 0;
        std::size_t rejected = 0;
    };

    inline SendResult ToSendResult(const IngestResult& result)
    {
        return result.authorized ? SendResult::Ok : SendResult::Rejected;
    }
} // namespace tracking
