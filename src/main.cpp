#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/filesystem.hpp>

#include <iostream>
#include <array>
#include <memory>
#include <thread>

#include "nbd.hpp"
#include "connection.hpp"
#include "connection_manager.hpp"

using namespace boost;

unsigned short nbd_port = 10809;

class tcp_connection;

class tcp_server
{
public:

    tcp_server(std::shared_ptr<asio::io_service> io_service) 
        : io_service_(io_service)
        , acceptor_(*io_service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), nbd_port))
    {
        acceptor_.listen(asio::socket_base::max_connections); // TODO: I don't know if this is required. It's not in the example
        start_accept();
    }

private:
    void start_accept()
    {
        tcp_connection::pointer new_connection = std::make_shared<tcp_connection>(io_service_, connection_manager_);
        
        acceptor_.async_accept(new_connection->socket(), 
            boost::bind(&tcp_server::handle_accept, this, new_connection, asio::placeholders::error));
    }

    void handle_accept(tcp_connection::pointer new_connection, const boost::system::error_code& error)
    {
        if (!error)
        {
            connection_manager_.start(new_connection);
        }
        start_accept();
    }
  
    std::shared_ptr<asio::io_service> io_service_;
    asio::ip::tcp::acceptor acceptor_;
    connection_manager connection_manager_;
};

int main(int argc, char** argv)
{
    try 
    {
        // One strand for network IO and one for reading from the file.
        // If we enable multiple connections we might need more.
        size_t thread_pool_size = 2;
        auto io_service = std::make_shared<asio::io_service>();
        std::vector<boost::shared_ptr<std::thread>> threads;
        tcp_server s(io_service);
        for (std::size_t i = 0; i < thread_pool_size; i++)
        {
            boost::shared_ptr<std::thread> thread(new std::thread(boost::bind(&asio::io_service::run, io_service.get())));
            threads.push_back(thread);
        }
        
        for (auto& thread : threads)
        {
            thread->join();
            std::cout << "Thread is joined" << std::endl;
        }
    }
    catch(std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
