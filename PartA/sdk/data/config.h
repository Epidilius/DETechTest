#pragma once

#include "sdk/data/dispatcher_config.h"
#include "sdk/interfaces/identity_provider.h"
#include "sdk/interfaces/transport.h"

namespace tracking
{
    struct Config
    {
        Config(IIdentityProvider& identity_provider, ITransport& event_transport) : identity(identity_provider), transport(event_transport)
        {
        }

        IIdentityProvider& identity;
        ITransport& transport;
        DispatcherConfig dispatcher;
    };
} // namespace tracking
