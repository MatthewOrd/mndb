#include <iostream>

#include "connection.hpp"
#include "connection_manager.hpp"

tcp_connection::tcp_connection(std::shared_ptr<asio::io_service> io_service, connection_manager& manager)
    : io_service_(io_service)
    , socket_(*io_service)
    , connection_manager_(manager)
    , socket_strand_(*io_service)
    , file_io_strand_(*io_service)
{
    backing_file_ = open(backing_file_path_, O_RDWR);
    if (backing_file_ == -1)
    {
        throw std::runtime_error("Unable to open the backing file");
    }

    disk_size_ = boost::filesystem::file_size(backing_file_path_);
}

void tcp_connection::start()
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

void tcp_connection::begin_request_read()
{
    //std::cout << "Beginning an asynchronous command read" << std::endl;

    asio::async_read(socket_, asio::buffer(data_, sizeof(request_message)), 
        socket_strand_.wrap(boost::bind(&tcp_connection::handle_request, shared_from_this(),
            asio::placeholders::error, 
            asio::placeholders::bytes_transferred)));
}

void tcp_connection::handle_request(const boost::system::error_code& error, size_t bytes_transferred)
{   
    request_message* request = (request_message* ) data_;
    uint32_t magic = boost::endian::big_to_native(request->nbd_request_magic);
    
    if (magic == NBD_REQUEST_MAGIC)
    {
        auto c = std::make_shared<command>();
        commands_.insert(c);
        
        c->type = boost::endian::big_to_native(request->type);
        c->handle = request->handle;
        c->offset = boost::endian::big_to_native(request->offset);
        c->length = boost::endian::big_to_native(request->length);
        c->buffer = std::vector<char>(c->length);
    
        std::cout << "Request type " << c->type << " ("  << c->offset << "," << c->length << ")" << std::endl;   
    
        if (c->type == NBD_CMD_READ)
        {
            io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::read_data_from_backing, shared_from_this(), c)));
            begin_request_read();    
        }
        else if (c->type == NBD_CMD_WRITE)
        {
            asio::async_read(socket_, asio::buffer(c->buffer.data(), c->length),
                socket_strand_.wrap(boost::bind(&tcp_connection::on_read_data_for_write_request, shared_from_this(), c,
                    asio::placeholders::error,
                    asio::placeholders::bytes_transferred)));
        }
        else if (c->type == NBD_CMD_DISC)
        {
            // Disconnect request. 
            // The server must handle all outstanding requests, shut down 
            // the TLS session, and close the TCP session. 
            // There is no reply to an NBD_CMD_DISC. 

            // We won't start a new read since the client can't send anymore requests,
            // but we do need to handle any outstanding write requests, I think.
            io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::handle_disconnect_request, shared_from_this(), c)));
        }
    }
    else
    {
        std::cout << "Unexpected request. Terminating the connection." << std::endl; // << bytes_transferred << " bytes" << std::endl;
        socket_.close();
        connection_manager_.stop(shared_from_this());
        
    }
}

void tcp_connection::on_read_data_for_write_request(std::shared_ptr<command> c, const boost::system::error_code& error, size_t bytes_transferred)
{
    std::cout << "Received " << bytes_transferred << " bytes of data for the write request." << std::endl;
    io_service_->post(file_io_strand_.wrap(boost::bind(&tcp_connection::write_data_to_backing, shared_from_this(), c)));

    begin_request_read();
}


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

void tcp_connection::on_response_complete(const boost::system::error_code& error, size_t bytes_transferred)
{
    std::shared_ptr<command> c = outbox_.front();
    outbox_.pop_front();
    commands_.erase(c);
    if (outbox_.size() > 0)
    {
        // There's another response waiting to be written so we'll queue it.
        io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::write_response, shared_from_this()))); 
    }
}
  
void tcp_connection::write_response()
{
    std::shared_ptr<command> c = outbox_.front();

    if (c->type == NBD_CMD_READ)
    {
    
        reply_message reply;
        reply.nbd_reply_magic = boost::endian::big_to_native(NBD_REPLY_MAGIC);
        reply.error = 0;
        reply.handle = c->handle;

        // TODO: Perhaps we want this asynchronous, although it's small?
        boost::system::error_code error;
        asio::write(socket_, asio::buffer(&reply, sizeof(reply)));
        
        std::cout << "Writing read request response data (" << c->offset << "," << c->buffer.size() << ")" << std::endl;
        
        asio::async_write(socket_, asio::buffer(c->buffer.data(), c->buffer.size()),
                socket_strand_.wrap(boost::bind(&tcp_connection::on_response_complete, shared_from_this(),
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred)));
    }
    else if (c->type == NBD_CMD_WRITE)
    {
        reply_message reply;
        reply.nbd_reply_magic = boost::endian::native_to_big(NBD_REPLY_MAGIC);
        reply.error = 0;
        reply.handle = c->handle; // We aren't using this so we didn't switch the endianness.

        // TODO: for simplicity's sake we'll make this synchronous for now since it's small
        boost::system::error_code error;
        asio::write(socket_, asio::buffer(&reply, sizeof(reply)), error);
    
        std::cout << "Write handled successfully" << std::endl;

        // It's convenient to have a single function where we do the queue popping. 
        io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::on_response_complete, shared_from_this(), error, sizeof(reply))));
    }
}

void tcp_connection::finish_request(std::shared_ptr<command> c)
{
    std::cout << "Finishing request type " << c->type << " (" << c->offset << "," << c->buffer.size() << ")" << std::endl;

    outbox_.push_back(c);
    if (outbox_.size() > 1)
    {
        // There's already a write in progress so we don't want to start another one, 
        return;
    }
    else
    {
        // There is no write in progress right now so we can start one
        io_service_->post(socket_strand_.wrap(boost::bind(&tcp_connection::write_response, shared_from_this())));
    }
}
