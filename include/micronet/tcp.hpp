#ifndef UNET_SOCKETS_TCP_HPP
#define UNET_SOCKETS_TCP_HPP

#include "basic_socket.hpp"

namespace unet
{
    struct socktype_tcp
    {
        constexpr static int    domain          = PF_INET;
        constexpr static int    type            = SOCK_STREAM;
        constexpr static bool   secure          = false;
    };

    using tcp_socket = basic_socket<socktype_tcp>;
}

#endif
