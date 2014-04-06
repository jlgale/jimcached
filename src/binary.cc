#include "buffer.h"
#include "cache.h"
#include "binary.h"
#include "utils.h"
#include "log.h"

#include "config.h"

#include <cassert>
#include <cstdarg>
#include <cstring>

#include <boost/asio.hpp>

using namespace std;

struct request_header
{
  uint8_t magic;
  uint8_t opcode;
  uint16_t key_length;
  uint8_t extras_length;
  uint8_t data_type;
  uint16_t vbucket_id;
  uint32_t total_body_length;
  uint32_t opaque;
} __attribute__((packed));

struct response_header
{
  uint8_t magic;
  uint8_t opcode;
  uint16_t key_length;
  uint8_t entras_length;
  uint8_t data_type;
  uint16_t status;
  uint32_t total_body_length;
  uint32_t opaque;
} __attribute__((packed));

enum session_state {
  binary_read_command,                 // Reading the next command
  binary_execute_command,              // Execute the command or reading set data
  binary_execute_write,                // Execute the set/add/etc. command
  binary_write_data,
  binary_write_result,
  binary_stopping,
};

class Binary
{
  // session lifetime state
  boost::asio::io_service &io_service; // ASIO handle
  //cache &money;                        // cache handle
  //stream &in;                          // session input stream
  //stream &out;                         // session output stream
  //ostream &log;
  binary_done done_;            // Callback when session stops

  session_state state_;

  void loop();

public:
  /*
  Binary(boost::asio::io_service &io_service, class cache &c,
           stream &in, stream &out, ostream &log)
    : io_service(io_service), money(c), in(in), out(out), log(log) { }
  */
  Binary(boost::asio::io_service &io_service, class cache &,
           stream &, stream &, ostream &)
    : io_service(io_service) { }
  ~Binary() { }
  void interact(binary_done done);
};

void
Binary::loop()
{
  bool blocked = false;
  while (not blocked) {
    switch (state_) {
    case binary_read_command:
      assert(0);
      continue;
    case binary_execute_command:
      assert(0);
      continue;
    case binary_execute_write:
      assert(0);
      continue;
    case binary_write_data:
      assert(0);
      continue;
    case binary_write_result:
      assert(0);
      continue;
    case binary_stopping:
      io_service.dispatch(done_);
      return;
    }
  }
}

void
Binary::interact(binary_done done)
{
  done_ = done;
  state_ = binary_read_command;
  loop();
}

Binary *
binary_new(boost::asio::io_service &io_service,
           class cache &c, stream &in, stream &out,
           std::ostream &log)
{
  return new Binary(io_service, c, in, out, log);
}

void
binary_interact(Binary &session, binary_done done)
{
  session.interact(done);
}

void
binary_delete(Binary *session)
{
  delete session;
}
