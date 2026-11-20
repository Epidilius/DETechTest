#pragma once

namespace tracking
{

    // One way FlakyTransport can treat a send.
    enum class Fault
    {
        None,           // pass the send through
        FailBeforeSend, // the request never arrives: a plain retry
        LoseReply,      // the request arrives but the reply is lost: a retry delivers twice
    };

} // namespace tracking
