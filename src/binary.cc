#include "buffer.h"
#include "cache.h"
#include "session.h"
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

class binary_session : public session
{
  // session lifetime state
  boost::asio::io_service &io_service; // ASIO handle
  //cache &money;                        // cache handle
  //stream &in;                          // session input stream
  //stream &out;                         // session output stream
  //ostream &log;
  session_done done_;           // Callback when session stops

  session_state state_;

  void loop();

public:
  /*
  binary_session(boost::asio::io_service &io_service, class cache &c,
           stream &in, stream &out, ostream &log)
    : io_service(io_service), money(c), in(in), out(out), log(log) { }
  */
  binary_session(boost::asio::io_service &io_service, class cache &,
                 stream &, stream &, ostream &) : io_service(io_service) { }
  ~binary_session() { }
  void interact(session_done done);
};

void
binary_session::loop()
{
  bool blocked = false;
  while (not blocked) {
    switch (state_) {
    case binary_read_command:
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
binary_session::interact(session_done done)
{
  done_ = done;
  state_ = binary_read_command;
  loop();
}

session *
binary_session_new(boost::asio::io_service &io_service,
                   class cache &c, stream &in, stream &out,
                   std::ostream &log)
{
  return new binary_session(io_service, c, in, out, log);
}
