#ifndef UNET_INTERNAL_UTILITY_HPP
#define UNET_INTERNAL_UTILITY_HPP

#include <tl/expected.hpp>

#if defined(__linux__) || defined(__linux)
# include <sys/socket.h>
#elif defined(_WIN32)
# include <winsock2.h>
# include <ws2def.h>
# include <ws2tcpip.h>
#endif

namespace unet
{
    enum class error_code {
        socket_already_open,
        cannot_open_socket,
        multiplexing_error,
        cannot_listen,
        no_active_socket,
        failed_to_accept,
        failed_to_send,
        recv_failed,
        connection_reset_by_peer,
        cannot_connect,
        no_data_to_read,

        unimplemented,
    };

    const char* explain(error_code err) {
        switch (err) {
            case error_code::socket_already_open:
                return "socket already open";
            case error_code::cannot_open_socket:
                return "cannot open socket";
            case error_code::multiplexing_error:
                return "multiplexing failure";
            case error_code::cannot_listen:
                return "cannot_listen";
            case error_code::no_active_socket:
                return "no active socket";
            case error_code::failed_to_accept:
                return "failed to accept()";
            case error_code::failed_to_send:
                return "failed to send()";
            case error_code::recv_failed:
                return "recv failed";
            case error_code::connection_reset_by_peer:
                return "connection reset by peer";
            case error_code::cannot_connect:
                return "unable to connect";
            case error_code::unimplemented:
                return "unimplemented";
            case error_code::no_data_to_read:
                return "no data to read";
       }
       __builtin_unreachable();
    }
}

namespace unet::detail
{
    inline int deduce_protocol_from_address(const char* s)
    {
        addrinfo* result;
        getaddrinfo(s, nullptr, nullptr, &result);
        int rval = result->ai_family;
        freeaddrinfo(result);
        return rval;
    }

    inline void* get_in_addr(struct sockaddr* sa)
    {
        if (sa->sa_family == AF_INET)
            return &((reinterpret_cast<struct sockaddr_in*>(sa))->sin_addr);
        else
            return &((reinterpret_cast<struct sockaddr_in6*>(sa))->sin6_addr);
    }
}

#endif
