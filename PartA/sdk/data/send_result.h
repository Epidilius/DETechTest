#pragma once

namespace tracking
{
    // What a transport reports for one send. It decides whether the dispatcher sends the batch again.
    enum class SendResult
    {
        Ok,
        Retry,
        Rejected,
    };
} // namespace tracking
