µnet sockets
============
Simple C++20 wrapper around BSD sockets with some QoL functionality.
Focuses on simple use first, uses `tl::expected` for error handling.

Works currently only on Linux.


There's ton of performance improvements to be made in the source
code, it currently makes stupid copies, doesn't buffer nearly
enough data to avoid extra syscalls etc.


Quick example use
-----------------

### Simple telnet echo server

```
#include <micronet/tcp.hpp>

int main()
{
    unet::tcp_socket sock;

    auto res = sock.listen(8999);
    if (not res.has_value())
        std::cout << unet::explain(res.error()) << "\n";

    auto conn_maybe = sock.accept();

    if (not conn_maybe.has_value()) {
        std::cout << unet::explain(conn_maybe.error()) << "\n";
        return -1;
    }

    auto conn = std::move(conn_maybe.value());

    while (true) {
        auto received = conn.recv_all<std::string>();
        if (not received.has_value()) {
            std::cout << unet::explain(received.error()) << "\n";
            return -1;
        }

        conn.send(received.value());
    }
}
```
