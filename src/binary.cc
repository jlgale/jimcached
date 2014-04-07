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

constexpr int buffer_size = 4096;

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

enum binary_state {
  binary_read_command,                 // Reading the next command
  binary_execute_command,              // Execute the command or reading set data
  binary_execute_write,                // Execute the set/add/etc. command
  binary_write_data,
  binary_write_result,
  binary_stopping,
};

ostream &
operator<<(ostream &o, binary_state s)
{
  switch (s) {
  case binary_read_command:    return o << "read_command";
  case binary_execute_command: return o << "execute_command";
  case binary_execute_write:   return o << "execute_write";
  case binary_write_data:      return o << "write_data";
  case binary_write_result:    return o << "write_result";
  case binary_stopping:        return o << "stopping";
  }
  return o << "binary_state(" << (int)s << ")";
}

class binary_session : public session
{
  // session lifetime state
  boost::asio::io_service &io_service_; // ASIO handle
  //cache &money;                        // cache handle
  //stream &in;                          // session input stream
  //stream &out;                         // session output stream
  ostream &log_;
  session_done done_;           // Callback when session stops

  // IO callbacks
  function<void (boost::system::error_code, size_t)> callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> void {
    callback(ec, bytes);
  };
  function<size_t (boost::system::error_code, size_t)> cmd_callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> size_t {
    return cmd_callback(ec, bytes);
  };

  buffer ibuf_;                 // Input buffer (current command)
  request_header *current_;
  binary_state state_;

  void set_state(binary_state next);
  
  void callback(boost::system::error_code ec, size_t bytes);
  size_t cmd_callback(boost::system::error_code ec, size_t bytes);
  bool cmd_ready(size_t additional);
  void loop();

public:
  /*
  binary_session(boost::asio::io_service &io_service, class cache &c,
           stream &in, stream &out, ostream &log)
    : io_service_(io_service), money(c), in(in), out(out), log(log) { }
  */
  binary_session(boost::asio::io_service &io_service, class cache &,
                 stream &, stream &, ostream &log)
    : io_service_(io_service), log_(log), ibuf_(buffer_size) { }
  ~binary_session() { }
  void interact(session_done done);
};

void
binary_session::set_state(binary_state next)
{
  log_ << DEBUG << state_ << " -> " << next << std::endl;
  state_ = next;
}

bool
binary_session::cmd_ready(size_t additional)
{
  if (ibuf_.used() + additional < sizeof(request_header))
    return false;
  current_ = (request_header*) ibuf_.headp();
  ibuf_.notify_write(additional);
  return true;
}

size_t
binary_session::cmd_callback(boost::system::error_code ec, size_t bytes)
{
  if (ec)
    return 0;
  else
    return cmd_ready(bytes) ? 0 : ibuf_.available() - bytes;
}

void
binary_session::callback(boost::system::error_code ec, size_t bytes)
{
  if (ec) {
    log_ << ERROR << "IO error: " << ec.message() << std::endl;
    set_state(binary_stopping);
  }
  loop();
}

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
      io_service_.dispatch(done_);
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
