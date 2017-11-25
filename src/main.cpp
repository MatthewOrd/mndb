#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>

#include <iostream>
#include <array>
#include <memory>
#include <thread>

#include "nbd.hpp"

using namespace boost;

unsigned short nbd_port = 10809;


class tcp_connection
    : public std::enable_shared_from_this<tcp_connection>
{
public:
    typedef std::shared_ptr<tcp_connection> pointer;

  static pointer create(std::shared_ptr<asio::io_service> io_service)
    {
        return pointer(new tcp_connection(io_service));
    }

    asio::ip::tcp::socket& socket()
    {
        return socket_;
    }

    void start()
    {       
        std::cout << "Kicking off the negotiation" << std::endl;

        initial_message initial;
        initial.nbdmagic = boost::endian::native_to_big(nbdmagic);
        initial.optmagic = boost::endian::native_to_big(optmagic);
        initial.handshake_flags = boost::endian::native_to_big(NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROS);

        boost::system::error_code error;
        boost::asio::write(socket_, asio::buffer(&initial, sizeof(initial)), error);
        std::cout << "Sent initial message" << std::endl;

        uint32_t client_flags;
        boost::asio::read(socket_, asio::buffer(&client_flags, sizeof(client_flags)), error);

        client_option c_option;
        boost::asio::read(socket_, asio::buffer(&c_option, sizeof(c_option)), error);
    
        uint32_t option = boost::endian::big_to_native(c_option.option);
        uint32_t data_len = boost::endian::big_to_native(c_option.length_of_data);
        asio::read(socket_, asio::buffer(data_, data_len), error); // TODO: We're not checking bounds

        // Server response format:
        // S: 64 bits, 0x3e889045565a9 (magic number for replies)
        // S: 32 bits, the option as sent by the client to which this is a reply
        // S: 32 bits, reply type (e.g., NBD_REP_ACK for successful completion,
        // or NBD_REP_ERR_UNSUP to mark use of an option not known by this
        // server
        // S: 32 bits, length of the reply. This MAY be zero for some replies, in
        // which case the next field is not sent
        // S: any data as required by the reply (e.g., an export name in the case
        // of NBD_REP_SERVER) 

        if (option == NBD_OPT_GO)
        {
            // 32 bits, length of name (unsigned); MUST be no larger than the  option data length - 6
            // String: name of the export
            // 16 bits, number of information requests
            // 16 bits x n - list of NBD_INFO information requests
            uint32_t name_length = boost::endian::big_to_native(*(uint32_t*)data_);
            uint16_t num_info_requests = boost::endian::big_to_native(*(uint16_t*)(data_ + 4 + name_length));
            std::cout << "Name length " << name_length << ", Num info requests " << num_info_requests << std::endl;

            // If no name is specified, this specifies the default export

            // Respond with an NBD_REP_ACK if the server accepts the chosen export.
            // The server must send at least one NBD_REP_INFO with an NBD_INFO_EXPORT

            // NBD_REP_ERR_UNKNOWN if the chosen export does not exist on the server (no NBD_REP_IN
            // NBD_REP_ERR_TLS_REQD: The server requires the client to initiate TLS
            // NBD_REP_ERR_BLOCK_SIZE_REQD: The server requires the client to request block size constraints using
            // NBD_INFO_BLOCK_SIZE because the server will be using non-default block size. 

            server_negotiation_response info_response;
            info_response.reply_magic = boost::endian::native_to_big(negotiation_replymagic);
            info_response.option_sent_by_client = boost::endian::native_to_big(option);
            info_response.reply_type = boost::endian::native_to_big(NBD_REP_INFO);
            info_response.reply_length = boost::endian::native_to_big(12);

            nbd_info_export info_export;
            info_export.information_type = boost::endian::native_to_big(NBD_INFO_EXPORT);
            info_export.size_of_export_in_bytes = boost::endian::native_to_big(disk_size_); // 1MB for now. Change this later
            info_export.transmission_flags = boost::endian::native_to_big(NBD_FLAG_HAS_FLAGS);

            server_negotiation_response ack_response;
            ack_response.reply_magic = boost::endian::native_to_big(negotiation_replymagic);
            ack_response.option_sent_by_client = boost::endian::native_to_big(option);
            ack_response.reply_type = boost::endian::native_to_big(NBD_REP_ACK);
            ack_response.reply_length = 0;

            // Copy the data into the buffer so we can send it all at once
            size_t bytes_copied = 0;
            memcpy(data_ + bytes_copied, &info_response, sizeof(info_response));
            bytes_copied += sizeof(info_response);
            memcpy(data_ + bytes_copied, &info_export, sizeof(info_export));
            bytes_copied += sizeof(info_export);
            memcpy(data_ + bytes_copied, &ack_response, sizeof(ack_response));
            bytes_copied += sizeof(ack_response);

            asio::write(socket_, asio::buffer(data_, bytes_copied), error);

            io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::begin_request_read, shared_from_this())));
        }
        else
        {
            // TODO: ??
        }
    }
    void begin_request_read()
    {
      //std::cout << "Beginning an asynchronous command read" << std::endl;

        asio::async_read(socket_, asio::buffer(data_, sizeof(request_message)), 
            socket_strand_.wrap(boost::bind(&tcp_connection::handle_request, shared_from_this(),
                asio::placeholders::error, 
                asio::placeholders::bytes_transferred)));
    }

    void handle_request(const boost::system::error_code& error, size_t bytes_transferred)
    {   
        request_message* request = (request_message* ) data_;
        uint32_t magic = boost::endian::big_to_native(request->nbd_request_magic);

        if (magic == NBD_REQUEST_MAGIC)
        {
            uint32_t type = boost::endian::big_to_native(request->type);
            uint64_t offset = boost::endian::big_to_native(request->offset);
            uint32_t length = boost::endian::big_to_native(request->length);
            std::cout << "Request type " << type << " (offset, len): (" 
                << offset << "," << length << ")" << std::endl;   

            auto buffer = std::make_shared<std::vector<char>>(length);

            if (type == NBD_CMD_READ)
            {
	      io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::read_data_from_backing, shared_from_this(), buffer, offset, request->handle)));
	      
	    }
	    else if (type == NBD_CMD_WRITE)
            {
                std::cout << "Initiating read for the rest of the data for the write request." << std::endl;

                asio::async_read(socket_, asio::buffer(buffer->data(), length),
				 socket_strand_.wrap(boost::bind(&tcp_connection::on_read_data_for_write_request, shared_from_this(), buffer, offset, request->handle,
								 asio::placeholders::error,
								 asio::placeholders::bytes_transferred)));
            }
        }
        else
        {
            std::cout << "I got something else. " << bytes_transferred << " bytes" << std::endl;
        }
    }

    void on_read_data_for_write_request(std::shared_ptr<std::vector<char>> buffer, uint64_t offset, uint64_t handle, const boost::system::error_code& error, size_t bytes_transferred)
    {
        std::cout << "Received " << bytes_transferred << " bytes of data for the write request." << std::endl;

	io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::write_data_to_backing, shared_from_this(), buffer, offset, handle)));
        
	begin_request_read();
    }

  void on_read_request_complete(std::shared_ptr<std::vector<char>> buffer, uint64_t offset, const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (!error) 
        {
	  std::cout << "Read handled successfully. (" << offset << "," << bytes_transferred << ")" << std::endl;

	  // TODO: We should be able to read a new command as soon as we read the previous one, but for some reason it's not working. 
	  begin_request_read();
        }
        else 
        {
            std::cout << "Read failed: " << error << std::endl;
            // TODO: What do we do when the read fails?
        }
    }

  void finish_read_request(std::shared_ptr<std::vector<char>> buffer, uint64_t offset, uint64_t handle)
  {
    std::cout << "Finishing read request (" << offset << "," << buffer->size() << ")" << std::endl;
    
     reply_message reply;
     reply.nbd_reply_magic = boost::endian::big_to_native(NBD_REPLY_MAGIC);
     reply.error = 0;
     reply.handle = handle;

     // TODO: Perhaps we want this asynchronous, although it's small?
     std::cout << "Writing read request response header (" << offset << "," << buffer->size() << ")" << std::endl;
     asio::write(socket_, asio::buffer(&reply, sizeof(reply)));

     std::cout << "Writing read request response data (" << offset << "," << buffer->size() << ")" << std::endl;     
     asio::async_write(socket_, asio::buffer(buffer->data(), buffer->size()),
		       socket_strand_.wrap(boost::bind(&tcp_connection::on_read_request_complete, shared_from_this(), buffer, offset, 
						       asio::placeholders::error,
						       asio::placeholders::bytes_transferred)));
  }

  void finish_write_request(uint64_t handle)
  {

    reply_message reply;
    reply.nbd_reply_magic = boost::endian::native_to_big(NBD_REPLY_MAGIC);
    reply.error = 0;
    reply.handle = handle; // We aren't using this so we didn't switch the endianness.

    // TODO: for simplicity's sake we'll make this synchronous for now since it's small
    asio::write(socket_, asio::buffer(&reply, sizeof(reply)));
    
    std::cout << "Write handled successfully" << std::endl;
    
  }

  void read_data_from_backing(std::shared_ptr<std::vector<char>> buffer, uint64_t offset, uint64_t handle)
  {
    std::cout << "Reading data from backing (" << offset << "," << buffer->size() << ")" << std::endl; 

    // TODO: Real file IO
    memcpy(buffer.get()->data(), device_+offset, buffer->size());
    
    io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::finish_read_request, shared_from_this(), buffer, offset, handle)));

    std::cout << "Queued post to finish_read_request (" << offset << "," << buffer->size() << ")" << std::endl;
  }

  void write_data_to_backing(std::shared_ptr<std::vector<char>> buffer, uint64_t offset, uint64_t handle)
  {

    // TODO: As before, we need this to be real file io
    memcpy(device_+offset, buffer.get()->data(), buffer.get()->size());

    io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::finish_write_request, shared_from_this(), handle)));

  }
  
  
private:

  tcp_connection(std::shared_ptr<asio::io_service> io_service)
    : io_service_(io_service)
    , socket_(*io_service)
    , socket_strand_(*io_service)
    , file_io_strand_(*io_service)
    {
      //backing_file_ = open(backing_file_path_, O_RDWR);
      //if (backing_file_ == -1)
      // {
      //      throw std::runtime_error("Unable to open the backing file");
      //  }

    }

    std::shared_ptr<asio::io_service> io_service_;
    asio::ip::tcp::socket socket_;
    asio::io_service::strand socket_strand_;
    asio::io_service::strand file_io_strand_;

    const size_t max_length_ = 1024;
    char data_[1024];
    
    // TODO: Replace this with actual file IO
    static const uint64_t disk_size_ = 1024*1024*1024;
    char device_[disk_size_];

    const char* backing_file_path_ = "/home/matthew/backing.img";
    int backing_file_;
};

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
      //tcp_connection::pointer new_connection = tcp_connection::create(acceptor_.get_io_service());
      tcp_connection::pointer new_connection = tcp_connection::create(io_service_);
    
        acceptor_.async_accept(new_connection->socket(), 
            boost::bind(&tcp_server::handle_accept, this, new_connection, asio::placeholders::error));
    }

    void handle_accept(tcp_connection::pointer new_connection, const boost::system::error_code& error)
    {
        if (!error)
        {
            new_connection->start();
        }
        start_accept();
    }

  std::shared_ptr<asio::io_service> io_service_;
    asio::ip::tcp::acceptor acceptor_;
};

int main(int argc, char** argv)
{
    try 
    {
        // One strand for network IO and one for reading from the file.
        // If we enable multiple conections we might need more.
        size_t thread_pool_size = 2;
	auto io_service = std::make_shared<asio::io_service>();
        std::vector<boost::shared_ptr<std::thread>> threads;
        tcp_server s(io_service);
        for (std::size_t i = 0; i < thread_pool_size; i++)
        {
            boost::shared_ptr<std::thread> thread(new std::thread(
	        boost::bind(&asio::io_service::run, io_service.get())));
            threads.push_back(thread);
        }

        for (auto& thread : threads)
        {
            thread->join();
        }
    }
    catch(std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
