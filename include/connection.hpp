#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/filesystem.hpp>

#include <deque>
#include <set>

#include "nbd.hpp"

using namespace boost; // TODO: Don't do this in a header

class connection_manager;

class tcp_connection
    : public std::enable_shared_from_this<tcp_connection>
    , private boost::noncopyable
{
public:

    struct command
    {
        uint16_t type;
        uint64_t handle;
        uint64_t offset;
        uint64_t length;

        std::vector<char> buffer;
    };
  
    typedef std::shared_ptr<tcp_connection> pointer;

    tcp_connection(std::shared_ptr<asio::io_service> io_service, connection_manager& manager);

    //static pointer create(std::shared_ptr<asio::io_service> io_service)
    //{
    //    return pointer(new tcp_connection(io_service));
    //}

    asio::ip::tcp::socket& socket()
    {
        return socket_;
    }

    void start();

    void read_request();
    
    void on_read_data_for_write_request(std::shared_ptr<command> c, const boost::system::error_code& error, size_t bytes_transferred);

    void handle_disconnect_request(std::shared_ptr<command> c);
    
    void on_response_complete(const boost::system::error_code& error, size_t bytes_transferred);
    
    void write_response();
    
    void finish_request(std::shared_ptr<command> c);
    
    void read_data_from_backing(std::shared_ptr<command> c)
    {
        off_t off = lseek(backing_file_, c->offset, SEEK_SET);
        if (off == -1)
        {
            throw std::runtime_error("Failed to seek to the given offset");
        }
        size_t remaining = c->length;
        while (remaining > 0)
        {
            ssize_t res = read(backing_file_, c->buffer.data() + (c->length - remaining), remaining);
            if (res == -1)
            {
                throw std::runtime_error("Read failed");
            }
            remaining -= res;
        }
        
        io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::finish_request, shared_from_this(), c)));
    }

    void write_data_to_backing(std::shared_ptr<command> c)
    {
        ssize_t off = lseek(backing_file_, c->offset, SEEK_SET);
        if (off == -1)
        {
            throw std::runtime_error("Failed to seek to the given offset");	
        }
        size_t remaining = c->length;
        while (remaining > 0)
        {
            ssize_t res = write(backing_file_, c->buffer.data() + (c->length - remaining), c->length);
            if (res == -1)
            {
            throw std::runtime_error("Write failed");
            }
            remaining -= res;
        }
        io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::finish_request, shared_from_this(), c)));
    }
    
private:

    std::shared_ptr<asio::io_service> io_service_;
    asio::ip::tcp::socket socket_;
    connection_manager& connection_manager_;

    // We'll do network IO in one strand and file io in the other strand. 
    asio::io_service::strand socket_strand_;
    asio::io_service::strand file_io_strand_;

    const size_t max_length_ = 1024;
    char data_[1024];

    uint64_t disk_size_;

    // TODO: We need to put this on the command-line
    const char* backing_file_path_ = "/home/matthew/backing.img";
    int backing_file_;

    // Synchronized by socket_strand_, so long as outbox_ is only accessed from that strand. 
    std::deque<std::shared_ptr<command>> outbox_;
    std::set<std::shared_ptr<command>> commands_; // Contains all currently running commands
};

#endif