#ifndef UNET_SOCKETS_HPP
#define UNET_SOCKETS_HPP

#include <type_traits>
#include <algorithm>

#if defined(__linux__) || defined(__linux)
# include <sys/socket.h>
# include "detail/sockets_os_posix.hpp"
#elif defined(_WIN32)
# include "detail/sockets_os_winsock2.hpp"
#endif

#include "detail/utility.hpp"
#include <string>
#include <chrono>
#include <cstring>
#include <span>

namespace unet
{
    using namespace std::chrono_literals;

    template <typename T, typename = int>
    struct has_init_hook : std::false_type {};

    template <typename T> requires std::is_invocable_v<decltype(T::init_hook)>
    struct has_init_hook<T, decltype((void) T::init_hook, 0)> : std::true_type {};


    template <typename T, typename = int>
    struct has_close_hook : std::false_type {};

    template <typename T> requires std::is_invocable_v<decltype(T::close_hook)>
    struct has_close_hook<T, decltype((void) T::close_hook, 0)> : std::true_type {};


    template <typename T>
    concept suitable_socket_type = requires(T t) {
        t.domain;
        t.type;
        t.secure;
    };

    template <typename T>
    concept suitable_container_type = (sizeof(typename T::value_type) == 1) && requires(T t) {
        { t.size() };
        { t.resize(0) };
        { t.operator[](0) };
    };

    struct ip_socket_pair
    {
        native_socket_type ipv4;
        native_socket_type ipv6;
    };

    struct recv_opts {
        bool disable_wait : 1 = false;
        bool allow_partial : 1 = false;
        bool append : 1 = false;

        // to ::recv flags, probably POSIX-only, TODO: figure out how to handle this in windows
        operator int() {
            return MSG_DONTWAIT & disable_wait;
        }
    };

    template <suitable_socket_type SocketType>
    class basic_socket : public SocketType, detail::os::socket
    {
        public:
            constexpr static native_socket_type uninitialised  = detail::os::uninitialised_socket;
            constexpr static native_socket_type disabled       = detail::os::disabled_socket;

            constexpr static bool is_secure = SocketType::secure;

            constexpr static ssize_t recv_buffer_size = 1024;

            size_t mtu_size = 1200;

            basic_socket() noexcept;
            basic_socket(native_socket_type in_socket_fd, int protocol) noexcept;
            basic_socket(basic_socket&&) noexcept(std::is_nothrow_move_assignable<basic_socket>::value);
            basic_socket& operator=(basic_socket&&) noexcept;

            basic_socket(const socket&) = delete;
            ~basic_socket();

            constexpr bool operator==(const basic_socket& other) const {
                return (socket_ipv4 == other.socket_ipv4) && (socket_ipv6 == other.socket_ipv6);
            }

            // connecting
            tl::expected<void, error_code> open(const std::string& host, uint16_t port) noexcept;
            tl::expected<void, error_code> open(uint16_t port) noexcept { return open({}, port); }
            tl::expected<void, error_code> connect(const std::string& host, uint16_t port) noexcept;

            // for listening/accepting socket streams
            tl::expected<void, error_code> listen(uint16_t port, int backlog_size = 20) noexcept requires (SocketType::type == SOCK_STREAM);
            tl::expected<basic_socket, error_code> accept(std::chrono::milliseconds = 0ms) noexcept requires (SocketType::type == SOCK_STREAM);

            // cleanup
            void close() noexcept;

            // state query
            bool is_active() const noexcept { return (socket_ipv4 > 0) || (socket_ipv6 > 0); }

            // sending data
            template <typename T>
            tl::expected<size_t, error_code> send(std::span<T> data) const noexcept;

            template <typename T>
            tl::expected<size_t, error_code> send(const T& data) const noexcept;

            tl::expected<size_t, error_code> send(const std::string& data) const noexcept;

            // receiving data
            template <typename T>
            tl::expected<T, error_code> recv(recv_opts = {}) noexcept;

            template <suitable_container_type T>
            tl::expected<T, error_code> recv_until(std::span<uint8_t> pattern, recv_opts = {}) noexcept;

            template <suitable_container_type T>
            tl::expected<T, error_code> recv_all(recv_opts = {}) noexcept;

            // for integration
            ip_socket_pair native_sockets() const noexcept {
                return {
                    socket_ipv4,
                    socket_ipv6
                };
            }

        private:
            tl::expected<size_t, error_code> send_raw(const char* dataptr, size_t size) const noexcept;

            native_socket_type get_os_socket(const std::string& host, uint16_t port, int family) noexcept;

            // TODO/FIXME: this probably needs to be changed later on to be dependant on SocketType,
            // e.g. we don't need IP sockets for UNIX domain sockets.
            native_socket_type socket_ipv6 = uninitialised;
            native_socket_type socket_ipv4 = uninitialised;
    };
}

namespace unet
{
    template <suitable_socket_type SockType>
    basic_socket<SockType>::basic_socket() noexcept
    {
        if constexpr(has_init_hook<SockType>::value) {
            this->init_hook();
        }
    }

    template <suitable_socket_type SockType>
    void basic_socket<SockType>::close() noexcept
    {
        if constexpr(has_close_hook<SockType>::value) {
            this->close_hook();
        }

        if (socket_ipv6 > 0)
            ::close(socket_ipv6);
        if (socket_ipv4 > 0)
            ::close(socket_ipv4);
    }

    template <suitable_socket_type SockType>
    basic_socket<SockType>::~basic_socket()
    {
        close();
    }

    template <suitable_socket_type SockType>
    basic_socket<SockType>::basic_socket(basic_socket&& other) 
    noexcept(std::is_nothrow_move_assignable<basic_socket<SockType>>::value) 
        : detail::os::socket(std::move(other))
    {
        *this = std::move(other);
    }

    template <suitable_socket_type SockType>
    basic_socket<SockType>& basic_socket<SockType>::operator=(basic_socket<SockType>&& other) noexcept
    {
        if (this == &other)
            return *this;

        close();

        socket_ipv6 = other.socket_ipv6;
        socket_ipv4 = other.socket_ipv4;

        other.socket_ipv6 = other.socket_ipv6 == disabled ? disabled : uninitialised;
        other.socket_ipv4 = other.socket_ipv4 == disabled ? disabled : uninitialised;

        return *this;
    }

    template <suitable_socket_type SockType>
    native_socket_type basic_socket<SockType>::get_os_socket(const std::string& host, uint16_t port, int family) noexcept
    {
        addrinfo    hints{};
        addrinfo*   server_info = nullptr;
        addrinfo*   info = nullptr;

        const char yes = 1;

        char portstr[6]; sprintf(portstr, "%d", port);

        hints.ai_family = family;
        hints.ai_socktype = SockType::type;

        if (host.empty()) {
            hints.ai_flags = AI_PASSIVE;
        }

        if (getaddrinfo(host.empty() ? nullptr : host.c_str(), portstr, &hints, &server_info) != 0)
            return disabled;

        native_socket_type socket_fd;

        for (info = server_info; info != nullptr; info = info->ai_next)
        {
            if ((socket_fd = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == detail::os::socket_error)
                continue;

            if (family == AF_INET6)
            {
                if (setsockopt(socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(int)) == detail::os::socket_error)
                {
                    freeaddrinfo(server_info);
                    return disabled;
                }
            }
            if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == detail::os::socket_error)
            {
                freeaddrinfo(server_info);
                return disabled;
            }

            if (host.empty())
            {
                if (::bind(socket_fd, info->ai_addr, info->ai_addrlen) == detail::os::socket_error)
                {
                    ::close(socket_fd);
                    continue;
                }
            } else {
                if (::connect(socket_fd, info->ai_addr, info->ai_addrlen) == detail::os::socket_error)
                {
                    ::close(socket_fd);
                    continue;
                }
            }
            break;
        }

        freeaddrinfo(server_info);
        if (info == nullptr)
            return disabled;

        return socket_fd;
    }

    template <suitable_socket_type SockType>
    basic_socket<SockType>::basic_socket(native_socket_type in_socket_fd, int protocol) noexcept
    {
        socket_ipv4 = protocol == AF_INET ? in_socket_fd : disabled;
        socket_ipv6 = protocol == AF_INET6 ? in_socket_fd : disabled;

        // TODO: do we need to call hooks here?
    }

    template <suitable_socket_type SockType>
    tl::expected<void, error_code> basic_socket<SockType>::open(const std::string& host, uint16_t port) noexcept
    {
        if ((socket_ipv6 > 0) || (socket_ipv4 > 0))
            return tl::unexpected(error_code::socket_already_open);

        if (host.empty())
        {
            socket_ipv6 = get_os_socket(host, port, AF_INET6);
            socket_ipv4 = get_os_socket(host, port, AF_INET);
        } else {
            socket_ipv6 = get_os_socket(host, port, AF_INET6);
            if (socket_ipv6 > 0)
                socket_ipv4 = disabled;
            else
                socket_ipv4 = get_os_socket(host, port, AF_INET);
        }

        if ((socket_ipv6 >= 0) || (socket_ipv4 >= 0))
            return {};

        return tl::unexpected(host.empty() ? error_code::cannot_open_socket : error_code::cannot_connect);
    }

    template <suitable_socket_type SockType>
    tl::expected<void, error_code> basic_socket<SockType>::connect(const std::string& host, uint16_t port) noexcept
    {
        return open(host, port);
    }

    template <suitable_socket_type SockType>
    tl::expected<void, error_code> basic_socket<SockType>::listen(uint16_t port, int backlog_size) noexcept
    requires (SockType::type == SOCK_STREAM)
    {
        auto listen_sock = open(port);
        if (not listen_sock.has_value())
            return listen_sock;

        if (socket_ipv4 > 0)
        {
            auto status = listen_on_os_socket(socket_ipv4, backlog_size, AF_INET);
            if (not status.has_value()) {
                ::close(socket_ipv4);
                socket_ipv4 = disabled;
                return status;
            }
        }

        if (socket_ipv4 > 0)
        {
            auto status = listen_on_os_socket(socket_ipv6, backlog_size, AF_INET6);
            if (not status.has_value()) {
                ::close(socket_ipv6);
                socket_ipv6 = disabled;
                return status;
            }
        }
        return {};
    }

    template <suitable_socket_type SockType>
    tl::expected<basic_socket<SockType>, error_code> basic_socket<SockType>::accept(std::chrono::milliseconds timeout) noexcept
    requires (SockType::type == SOCK_STREAM)
    {
        if ((socket_ipv6 < 0) && (socket_ipv4 < 0))
            return tl::unexpected(error_code::no_active_socket);

        sockaddr_storage their_addr;
        socklen_t addr_size = sizeof(their_addr);

        detail::os::platform_event_type event;
        wait_listen(&event, 1, timeout);

        native_socket_type new_socket = ::accept(os_socket_from_event(event),
                                                      reinterpret_cast<sockaddr*>(&their_addr),
                                                      &addr_size);

        if (new_socket == detail::os::socket_error)
            return tl::unexpected(error_code::failed_to_accept);

        char s[INET6_ADDRSTRLEN];
        inet_ntop(their_addr.ss_family,
                  detail::get_in_addr(reinterpret_cast<sockaddr*>(&their_addr)),
                  s, sizeof(s));

        int af_type = os_socket_from_event(event) == socket_ipv4 ? AF_INET : AF_INET6;

        return basic_socket(new_socket, af_type);
    }

    template <suitable_socket_type SockType> template <typename T>
    tl::expected<size_t, error_code> basic_socket<SockType>::send(std::span<T> data) const noexcept
    {
        return send_raw(data.data(), data.size() * sizeof(T));
    }

    template <suitable_socket_type SockType>
    tl::expected<size_t, error_code> basic_socket<SockType>::send(const std::string& data) const noexcept
    {
        return send_raw(data.data(), data.size());
    }

    template <suitable_socket_type SockType> template <typename T>
    tl::expected<size_t, error_code> basic_socket<SockType>::send(const T& data) const noexcept
    {
        constexpr static int extent = std::extent_v<T>;
        if constexpr (extent == 0) {
            char* dataptr = &data;
            size_t data_size = extent == 0 ? sizeof(T) : extent * sizeof(decltype(data[0]));
            return send_raw(dataptr, data_size);
        } else {
            const char* dataptr = &data[0];
            size_t data_size = extent == 0 ? sizeof(T) : extent * sizeof(decltype(data[0]));
            return send_raw(dataptr, data_size);
        }
    }

    template <suitable_socket_type SockType>
    tl::expected<size_t, error_code> basic_socket<SockType>::send_raw(const char* dataptr, size_t total_size) const noexcept
    {
        if (not is_active())
            return tl::unexpected(error_code::no_active_socket);
        
        const int socket_fd = socket_ipv4 == disabled ? socket_ipv6 : socket_ipv4;
        
        size_t sent = 0;
        size_t left = total_size;
        int n = 0;

        while (sent < total_size) {
            n = ::send(socket_fd, dataptr + sent, left, 0);
            if (n == -1)
                return tl::unexpected(error_code::failed_to_send);

            sent += n;
            left -= n;
        }
        
        return sent;
    }

    // TODO/FIXME: write straight into rval if sizeof(RecvType) < recv_buffer_size
    template <suitable_socket_type SockType>
    template <typename RecvType>
    tl::expected<RecvType, error_code> basic_socket<SockType>::recv(recv_opts opts) noexcept
    {
        // FIXME:
        (void)opts;

        if (not is_active())
            return tl::unexpected(error_code::no_active_socket);

        RecvType rval{};

        const native_socket_type raw_sockfd = socket_ipv4 == disabled ? socket_ipv6 : socket_ipv4;
        std::array<std::byte, recv_buffer_size> chunk;

        ssize_t bytes_remaining = sizeof(RecvType);
        size_t bytes_received = 0;

        while(true) {
            chunk.fill(std::byte(0));
            ssize_t bytes = ::recv(raw_sockfd,
                                   chunk.data(),
                                   std::min(bytes_remaining, recv_buffer_size),
                                   opts);

            if (bytes == 0) {
                close();
                return tl::unexpected(error_code::connection_reset_by_peer);
            }
            else if (bytes < 0) {
                return tl::unexpected(error_code::recv_failed);
            }

            std::memmove(reinterpret_cast<char*>(&rval) + bytes_received, chunk.data(), bytes);
            bytes_received += bytes;
            bytes_remaining -= bytes;

            if (bytes_remaining <= 0)
                break;

        }

        return rval;
    };

    // FIXME: this needs to use some sort of buffering, ::recving a byte at a time is horrible
    template <suitable_socket_type SockType>
    template <suitable_container_type T>
    tl::expected<T, error_code> basic_socket<SockType>::recv_until(std::span<uint8_t> pattern, recv_opts flags) noexcept
    {
        if (not is_active())
            return tl::unexpected(error_code::no_active_socket);

        T rval{};

        const int socket_fd = socket_ipv4 == disabled ? socket_ipv6 : socket_ipv4;
        std::array<std::byte, recv_buffer_size> chunk;

        size_t match_size = 0;

        uint8_t recv;

        bool multiple_chunks = false;
        while(true) {
            ssize_t bytes = ::recv(socket_fd, &recv, 1, multiple_chunks ? MSG_DONTWAIT : flags);

            if (bytes == 0) {
                close();
                return tl::unexpected(error_code::connection_reset_by_peer);
            } else if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (flags.allow_partial)
                        return rval;
                    return tl::unexpected(error_code::no_data_to_read);
                }

                return tl::unexpected(error_code::recv_failed);
            }

            rval.push_back(static_cast<typename T::value_type>(recv));

            if (bytes < recv_buffer_size)
                break;

            if (recv == pattern[match_size]) {
                match_size++;
                if (match_size == pattern.size())
                    break;
            }

            multiple_chunks = true;
        }

        return rval;
    }

    template <suitable_socket_type SockType>
    template <suitable_container_type T>
    tl::expected<T, error_code> basic_socket<SockType>::recv_all(recv_opts opts) noexcept
    {
        // FIXME:
        (void)opts;

        if (not is_active())
            return tl::unexpected(error_code::no_active_socket);

        constexpr static int no_flags = 0;

        T rval{};

        const int socket_fd = socket_ipv4 == disabled ? socket_ipv6 : socket_ipv4;
        std::array<std::byte, recv_buffer_size> chunk;

        size_t total_recv = 0;

        bool multiple_chunks = false;
        while(true)
        {
            chunk.fill(std::byte(0));
            ssize_t bytes = ::recv(socket_fd, chunk.data(), recv_buffer_size, multiple_chunks ? MSG_DONTWAIT : no_flags);
            if (bytes == 0) {
                close();
                return tl::unexpected(error_code::connection_reset_by_peer);
            } else if (bytes < 0) {
                return tl::unexpected(error_code::recv_failed);
            }

            rval.resize(total_recv + bytes);
            std::memmove(rval.data() + total_recv, chunk.data(), bytes);
            total_recv += bytes;

            if (bytes < recv_buffer_size)
                break;

            multiple_chunks = true;
        }

        return rval;
    }
}

namespace std
{
    template <unet::suitable_socket_type SockType> struct hash<unet::basic_socket<SockType>>
    {
        size_t operator()(const unet::basic_socket<SockType>& sock) const
        {
            // this is assumed, so check it
            static_assert(sizeof(size_t) >= sizeof(unet::native_socket_type) * 2, 
                    "FIXME/BUG: Implementation assumes sizeof(size_t) is at least twice sizeof(os::socket_type)\n");

            unet::ip_socket_pair socks = sock.native_sockets();
            return (size_t(socks.ipv6) << (sizeof(unet::native_socket_type) * 8)) | socks.ipv4;
        }
    };
}

#endif
