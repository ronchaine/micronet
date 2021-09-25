#ifndef UNET_SOCKETS_UDP_HPP
#define UNET_SOCKETS_UDP_HPP

#include "basic_socket.hpp"

namespace unet
{
    struct socktype_udp
    {
        constexpr static int    domain          = PF_INET;
        constexpr static int    type            = SOCK_DGRAM;
        constexpr static bool   secure          = false;
    };

    using udp_socket = basic_socket<socktype_udp>;
}

#endif
