#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <boost/core/noncopyable.hpp>

#include "connection.hpp"

class connection_manager 
    : private boost::noncopyable
{
public:

    void start(std::shared_ptr<tcp_connection> connection)
    {
        connections_.insert(connection);
        connection->start();
    }

    void stop(std::shared_ptr<tcp_connection> connection)
    {
        connections_.erase(connection);
        // TODO: 
        // connection->stop();
    }
    // TODO: stop_all


private:
    std::set<std::shared_ptr<tcp_connection>> connections_;

};

#endif