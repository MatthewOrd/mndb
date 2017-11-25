#include "stdint.h"

#pragma pack(1)

// Magic numbers
const uint64_t nbdmagic = 0x4e42444d41474943; // 'NBDMAGIC' Generally called INIT_PASSWD
const uint64_t optmagic = 0x49484156454F5054; // 'IHAVEOPT';
const uint64_t negotiation_replymagic = 0x3e889045565a9; 
const uint32_t NBD_REQUEST_MAGIC = 0x25609513;
const uint32_t NBD_REPLY_MAGIC = 0x67446698;

// handshake_flags 
const uint16_t NBD_FLAG_FIXED_NEWSTYLE = 0x1; // Must be set by servers that support the fixed newstyle protocol
const uint16_t NBD_FLAG_NO_ZEROS = 0x2;

//client flags
const uint32_t NBD_FLAG_C_FIXED_NEWSTYLE = 1<<0; // Should be set by clients that support the fixed newstyle protocol
const uint32_t NBD_FLAG_C_NO_ZEROES = 1<<1;

// option types
const uint32_t NBD_OPT_INFO = 6;
const uint32_t NBD_OPT_GO = 7;

// Reply types (Used in the "reply type field sent by the server during option haggling")
const uint32_t NBD_REP_ACK = 1;
const uint32_t NBD_REP_INFO = 3;

// For use with NBD_REP_INFO
const uint16_t NBD_INFO_EXPORT = 0;

// Transmission flags
const uint16_t NBD_FLAG_HAS_FLAGS = 0x01; // Must always be 1
const uint16_t NBD_FLAG_READ_ONLY = 1<<1;
const uint16_t NBD_FLAG_SEND_FLUSH = 1<<2; 
// etc.

// Request types
const uint16_t NBD_CMD_READ = 0;
const uint16_t NBD_CMD_WRITE = 1;
const uint16_t NBD_CMD_DISC = 2; // A disconnect request. The server must handle outstanding requests, shut down the TLS session, and close the TCP session
// ect. 

struct initial_message
{
    uint64_t nbdmagic;
    uint64_t optmagic;
    uint16_t handshake_flags;        
};

struct client_option
{
    uint64_t optmagic;
    uint32_t option;
    uint32_t length_of_data;
    // Followed by any needed data from the length above
};

struct server_negotiation_response
{
    uint64_t reply_magic;
    uint32_t option_sent_by_client;
    uint32_t reply_type; // e.g. NBD_REP_ACK
    uint32_t reply_length; // May be zero fro some replies.
    // Any data as required by the reply
};

struct nbd_info_export
{
    uint16_t information_type; // NBD_INFO_EXPORT
    uint64_t size_of_export_in_bytes;
    uint16_t transmission_flags;
};

struct reply_magic
{
    uint32_t nbd_reply_magic;
    uint32_t error; // May be zero
    uint64_t handle;
    // length bytes of data if the type is of NBD_CMD_READ
};

struct request_message
{
    uint32_t nbd_request_magic;
    uint16_t command_flags;
    uint16_t type;
    uint64_t handle; 
    uint64_t offset;
    uint32_t length;
    // Length bytes of data if the type is NBD_CMD_WRITE
};

struct reply_message
{
    uint32_t nbd_reply_magic;
    uint32_t error; // May be zero
    uint64_t handle; 
    // length bytes of data if the request is type NBD_CMD_READ
};

#pragma pop()