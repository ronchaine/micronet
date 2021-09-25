#ifndef UNET_OS_POSIX_HPP
#define UNET_OS_POSIX_HPP

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <chrono>

#include "utility.hpp"

#if defined(UNET_EPOLL)
# include <sys/epoll.h>
namespace unet::detail::os {
    using platform_event_type = epoll_event;
}
#elif defined(UNET_KQUEUE)
# include <sys/event.h>
namespace unet::detail::os {
    using platform_event_type = kevent;
}
#else
# error No multiplexer backend chosen, define UNET_EPOLL or UNET_KQUEUE
#endif

namespace unet::detail::os
{
    using socket_type = int;

    constexpr static socket_type uninitialised_socket    = 0;
    constexpr static socket_type disabled_socket         = -2;
    constexpr static socket_type socket_error            = -1;

    class socket
    {
        protected:
            socket() = default;
            socket(socket&&) = default;
            socket(const socket&) = delete;

            tl::expected<void, error_code> listen_on_os_socket(os::socket_type& sock, int backlog_size, int socktype) noexcept;
            void stop_listening() noexcept { ::close(listen_fd); listen_fd = uninitialised_socket; }

            int wait_listen(platform_event_type* output, uint32_t max_events, std::chrono::milliseconds timeout) noexcept;

            socket_type os_socket_from_event(platform_event_type& event) const noexcept {
                #if defined(UNET_EPOLL)
                return event.data.fd;
                #endif
            }

        private:
            socket_type listen_fd = uninitialised_socket;
    };

    tl::expected<void, error_code> socket::listen_on_os_socket(os::socket_type& sock, int backlog_size, int socktype) noexcept
    {
        (void)socktype;

        if (listen_fd == uninitialised_socket)
        {
            #if defined(UNET_EPOLL)
            listen_fd = epoll_create1(0);
            #elif defined(UNET_KQUEUE)
            listen_fd = kqueue();
            #endif
        }

        if (listen_fd == -1)
        {
            stop_listening();
            return tl::unexpected(error_code::multiplexing_error);
        }

        if (::listen(sock, backlog_size) == -1)
            return tl::unexpected(error_code::cannot_listen);

        #if defined(UNET_EPOLL)
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = sock;
        if (epoll_ctl(listen_fd, EPOLL_CTL_ADD, sock, &event))
            return tl::unexpected(error_code::multiplexing_error);

        #elif defined(UNET_KQUEUE)
        #error unimplemented
        #endif
        return {};
    }

    int socket::wait_listen(platform_event_type* output, uint32_t max_events, std::chrono::milliseconds timeout) noexcept
    {
        #if defined(UNET_EPOLL)
        if (timeout.count() <= 0)
            return epoll_wait(listen_fd, output, max_events, -1);
        else
            return epoll_wait(listen_fd, output, max_events, timeout.count());
        #elif defined (UNET_KQUEUE)
        #error unimplemented
        #endif
    }
}

#endif
