#include "nbd.hpp"

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

// Request types
extern const uint16_t NBD_CMD_READ = 0;
extern const uint16_t NBD_CMD_WRITE = 1;
extern const uint16_t NBD_CMD_DISC = 2; // A disconnect request. The server must handle outstanding requests, shut down the TLS session, and close the TCP session
// ect. 