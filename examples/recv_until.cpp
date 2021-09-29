#include <micronet/tcp.hpp>
#include <iostream>

int main()
{
    // this is our wrapper, could also use unet::basic_socket<socktype_tcp>
    unet::tcp_socket sock;

    // listen on port 8999, this will listen for both IPv4 and
    // IPv6 connections
    auto res = sock.listen(8999);

    // check if we have an value or an error, if error, print
    // it and bail out
    if (not res.has_value()) {
        std::cout << unet::explain(res.error()) << "\n";
        return -1;
    }

    // wait for the incoming connection and then accept it
    auto conn_maybe = sock.accept();

    if (not conn_maybe.has_value()) {
        std::cout << unet::explain(conn_maybe.error()) << "\n";
        return -1;
    }

    // our connection was legit, move the value from `conn_maybe`
    // to a new socket variable
    unet::tcp_socket conn = std::move(conn_maybe.value());

    while (true) {
        // waits to read as much data as possible from the socket
        auto received = conn.recv_until<std::string>('a');

        if (not received.has_value()) {
			// just discard any extra bytes after the last 'a'
			if (received.error() == unet::error_code::no_data_to_read) {
				continue;
			}

            std::cout << unet::explain(received.error()) << "\n";
            return -1;
        }

		std::cout << "received line: " << received.value() << "\n";
    }
}
