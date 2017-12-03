#include <iostream>

#include "connection.hpp"
#include "connection_manager.hpp"

void tcp_connection::handle_disconnect_request(std::shared_ptr<command> c)
{
    if (commands_.size() > 1)
    {
        // There are still outstanding commands other than this one that 
        // we're supposed to handle before we close the socket. So, we'll go around 
        // again. 
        // TODO: We should probably wait some time before calling this again so we're not just spinning
        io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::handle_disconnect_request, shared_from_this(), c)));
    }
    else
    {
        // All commands are done except that disconnect, which has no response, so 
        // we'll just close the socket.

        //TODO: We also need to clean up the "this" object.
        socket_.close();
        connection_manager_.stop(shared_from_this());
        std::cout << "Closed the socket for the disconnnect request" << std::endl;
    }
}