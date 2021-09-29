// Windows support was hacked together pretty fast without really
// being too familiar with windows stuff, so here be dragons.

#ifndef UNET_OS_WINDOWS_HPP
#define UNET_OS_WINDOWS_HPP

#include <winsock2.h>
#include <ws2def.h>
#include <ws2tcpip.h>

#include "utility.hpp"

#include <chrono>
#include <cassert>
#include <unordered_set>
#include <iostream>

namespace unet::detail::os
{
    enum class socket_state
    {
        uninitialised,
        disabled,
        error,
        active
    };

    struct win32_socket_wrapper
    {
        constexpr win32_socket_wrapper() = default;
        constexpr win32_socket_wrapper(SOCKET s) : sock(s) {
            state = socket_state::active;
        }
        constexpr win32_socket_wrapper(socket_state ss, SOCKET s) : sock(s), state(ss) {}

        constexpr win32_socket_wrapper& operator=(const SOCKET& other) {
            sock = other;
            state = socket_state::active;
            return *this;
        }

        bool operator<(int other) const noexcept {
            assert(other == 0 && "comparison of win32_socket_wrapper with nonzero integer");
            if (state != socket_state::active)
                return true;
            return false;
        }
        
        bool operator>(int other) const noexcept {
            assert(other == 0 && "comparison of win32_socket_wrapper with nonzero integer");
            if (state != socket_state::active)
                return false;
            return true;
        }
        
        bool operator>=(int other) {
            assert(other == 0 && "comparison of win32_socket_wrapper with nonzero integer");
            if (state != socket_state::active)
                return false;
            return true;
        }

        bool operator==(int other) = delete;
        bool operator==(const win32_socket_wrapper& rhs) const noexcept {
            return (sock == rhs.sock) && (state == rhs.state);
        }
        bool operator==(const SOCKET& rhs) const noexcept {
            return (sock == rhs) && (state == socket_state::active);
        }

        operator int() = delete;

        explicit operator SOCKET&() {
            return sock;
        }
        explicit constexpr operator const SOCKET&() const {
            return sock;
        }

        SOCKET sock = INVALID_SOCKET;
        socket_state state = socket_state::uninitialised;
    };
}

namespace unet
{
    // MSG_DONTWAIT is not supported on windows
    int MSG_DONTWAIT = 0;

    using os_socket_type = detail::os::win32_socket_wrapper;
    using native_socket_type = SOCKET;
}

// FIXME: this is a hack, polluting the global namespace like a boss...
inline void close(unet::os_socket_type s) {
    closesocket(s.sock);
}

namespace unet::detail::os
{
    using platform_event_type = SOCKET;

    constexpr static os_socket_type uninitialised_socket = detail::os::win32_socket_wrapper {
        socket_state::uninitialised,
        INVALID_SOCKET,
    };
    constexpr static os_socket_type disabled_socket = detail::os::win32_socket_wrapper {
        socket_state::disabled,
        INVALID_SOCKET,
    };
    constexpr static os_socket_type socket_error = detail::os::win32_socket_wrapper {
        socket_state::error,
        INVALID_SOCKET,
    };

    class socket
    {
        protected:
            socket() {
                if (instance_count == 0) {
                    if (WSAStartup(MAKEWORD(2,2), &wsaData)) {
                        assert(0 && "failed WSAstartup");
                    }
                }
                instance_count++;
            }
            ~socket() {
                instance_count--;
                if (instance_count == 0) {
                    WSACleanup();
                }
            }
            socket(socket&&) = default;
            socket(const socket&) = delete;

            tl::expected<void, error_code> listen_on_os_socket(os_socket_type& sock, int backlog_size, int socktype) noexcept;
            void stop_listening() noexcept {}

            int wait_listen(platform_event_type* output, uint32_t max_events, std::chrono::milliseconds timeout) noexcept;

            SOCKET native_socket_from_event(platform_event_type& event) const noexcept {
                return event;
            }

            inline char* os_ptr_cast(void* ptr) {
                return reinterpret_cast<char*>(ptr);
            }

        private:
            inline static WSADATA wsaData;
            inline static size_t instance_count;

            fd_set listen_fd_set;
            std::unordered_set<SOCKET> listening_sockets;
    };

    tl::expected<void, error_code> socket::listen_on_os_socket(os_socket_type& sock, int backlog_size, int socktype) noexcept
    {
        if (listening_sockets.size() == 0) {
            FD_ZERO(&listen_fd_set);
        }

        listening_sockets.insert(sock.sock);
        FD_SET(sock.sock, &listen_fd_set);

        if (::listen(sock.sock, backlog_size) == -1)
            return tl::unexpected(error_code::cannot_listen);

        return {};
    }
    
    int socket::wait_listen(platform_event_type* output, uint32_t max_events, std::chrono::milliseconds timeout) noexcept
    {
        if (timeout.count() <= 0) {
            if (select(listening_sockets.size(), &listen_fd_set, NULL, NULL, NULL) == -1) {
                return -1;
            }
        } else {
            timeval t; 
            t.tv_sec = 0;
            t.tv_usec = 1000 * timeout.count();

            if (select(listening_sockets.size(), &listen_fd_set, NULL, NULL, &t) == -1) {
                return -1;
            }
        }

        for (const SOCKET& sock : listening_sockets) {
            if (FD_ISSET(sock, &listen_fd_set)) {
                *output = sock;
                return 0;
            }
        }
        return -1;
    }
}

#endif
