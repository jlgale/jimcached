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

// XXX - the entire commanline must fit here
constexpr int buffer_size = 4096;
// must be less than buffer_size
constexpr int max_key_size = 255;

using namespace std;

/* session state transitions:
 *
 * start -> write_prompt -> read_command -> execute_command -+--(quit)-> stop
 *               ^               |                 |         |
 *               |            (error)        (set/add/etc.)  |
 *               |               |                 |         |
 *               |               v                 v         |
 *               +--------- write_result <- execute_write    |
 *                               ^                           |
 *                               |                       (get/gets)
 *                               |                           |
 *                               |                           v
 *                               +--------------------- write_data
 *
 * IO errors cause the session to stop.
 */

enum session_state {
  session_write_prompt,                 // Sending a prompt (standalone mode only)
  session_read_command,                 // Reading the next command
  session_execute_command,              // Execute the command or reading set data
  session_execute_write,                // Execute the set/add/etc. command
  session_write_data,
  session_write_result,
  session_stopping,
};

ostream &
operator<<(ostream &o, session_state s)
{
  switch (s) {
  case session_write_prompt:    return o << "write_prompt";
  case session_read_command:    return o << "read_command";
  case session_execute_command: return o << "execute_command";
  case session_execute_write:   return o << "execute_write";
  case session_write_data:      return o << "write_data";
  case session_write_result:    return o << "write_result";
  case session_stopping:        return o << "stopping";
  }
  return o << "session_state(" << (int)s << ")";
}

class server_error_t {};
class client_error_t {};

class text_session : public session
{
  // session lifetime state
  boost::asio::io_service &io_service; // ASIO handle
  cache &money;                        // cache handle
  stream &in;                          // session input stream
  stream &out;                         // session output stream
  ostream &log;
  const char *prompt_;
  session_done done_;        // Callback when session stops

  // IO callbacks
  function<void (boost::system::error_code, size_t)> callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> void {
    callback(ec, bytes);
  };
  function<size_t (boost::system::error_code, size_t)> cmd_callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> size_t {
    return cmd_callback(ec, bytes);
  };

  buffer ibuf;               // Input buffer (current command)
  buffer obuf;               // Staged output for the current command

  // Current command state
  bool noreply_;             // When true, replys are suppressed
  session_state state_;
  buf args_;
  buf cmd_;
  unsigned long flags_, exptime_;
  uint64_t unique_;
  buf key_;
  mem *idata_ = NULL;
  const_rope odata_;

  // Input
  bool recv_command();
  bool recv_data(size_t bytes);

  // Output
  bool send_prompt();
  void send_async(const char *msg, size_t bytes);
  void send_n(const char *msg, size_t bytes);
  void send(const char *msg);
  void sendln(const char *msg);
  void sendf(const char *fmt, ...);
  void vsendf(const char *fmt, va_list ap);
  void send_stat(const char *name, const char *val);
  void send_stat(const char *name, uint64_t val);
  void send_cache_result(cache_error_t res);
  void client_error(const char *fmt, ...);
  void server_error(const char *fmt, ...);
  bool send_data();
  bool flush();

  // Command handlers
  bool flush_all();
  bool cas();
  bool get(bool cas_unique);
  bool incr_decr(bool incr);
  bool del();
  bool stats();
  bool version();
  bool touch();

  // Command helpers
  void parse_update(unsigned long *bytes);
  void parse_key();
  void stored_if(bool stored);
  void parse_noreply();
  void set_state(session_state next);

  // text_session helpers
  void callback(boost::system::error_code ec, size_t bytes);
  size_t cmd_callback(boost::system::error_code ec, size_t bytes);
  bool cmd_ready(size_t additional);
  void loop();

  bool dispatch();
  bool dispatch_write();

public:
  text_session(boost::asio::io_service &io_service,
          class cache &c, stream &in, stream &out,
          ostream &log, const char *prompt)
    : io_service(io_service), money(c), in(in), out(out), log(log),
      prompt_(prompt), ibuf(buffer_size), obuf(buffer_size) { }
  ~text_session() { }
  void interact(session_done done);
};

void
text_session::set_state(session_state next)
{
  log << DEBUG << state_ << " -> " << next << std::endl;
  state_ = next;
}

void
text_session::send(const char *msg)
{
  send_n(msg, strlen(msg));
}

void
text_session::sendln(const char *msg)
{
  send(msg);
  send(CRLF);
}

void
text_session::parse_noreply()
{
  buf nr = consume_token(args_);
  if (!nr.empty()) {
    if (nr.is("noreplay"))
      noreply_ = true;
    else
      client_error("expected noreply or end of command");
  }
}

bool
text_session::flush()
{
  if (obuf.empty()) {
    return false;
  } else {
    out.async_write(boost::asio::const_buffers_1(obuf.headp(), obuf.used()),
                    callback_);
    return true;
  }
}

void
text_session::send_n(const char *msg, size_t bytes)
{
  if (!noreply_)
    obuf.write(msg, bytes);
}

void
text_session::send_async(const char *msg, size_t bytes)
{
  assert(obuf.used() == 0);     // XXX - we could flush now
  // XXX - we could take a rope...
  out.async_write(boost::asio::const_buffers_1(msg, bytes), callback_);
}

void
text_session::vsendf(const char *fmt, va_list ap)
{
  if (not noreply_) {
    int n = vsnprintf(obuf.tailp(), obuf.available(), fmt, ap);
    obuf.notify_write(n);
    send(CRLF);
  }
}

void
text_session::sendf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
}

void
text_session::send_stat(const char *name, const char *val)
{
  sendf("STAT %s %s", name, val);
}

void
text_session::send_stat(const char *name, uint64_t val)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "%lu", val);
  send_stat(name, buf);
}

bool
text_session::send_data()
{
  const mem *m = odata_.pop();
  if (!m) {
    send(CRLF);
    set_state(session_execute_command);
    return false;
  } else {
    set_state(session_write_data);
    send_async(m->data, m->size);
    return true;
  }
}

bool
text_session::get(bool cas_unique)
{
  buf key = consume_token(args_);
  if (key.empty()) {
    send("END" CRLF);
    set_state(session_write_result);
    return false;
  }

  cache::ref e = money.get(key);
  if (e == nullptr) {
    sendln("NOT_FOUND");
    set_state(session_write_result);    // XXX - what about multiple results?
    return false;
  }

  odata_ = e->read();
  size_t size = odata_.size();
  if (cas_unique) {
    uint64_t version = odata_.hash(e->get_flags());
    sendf("VALUE %.*s %u %u %llu",
          key.used(), key.headp(), e->get_flags(), size, version);
  } else {
    sendf("VALUE %.*s %u %u",
          key.used(), key.headp(), e->get_flags(), size);
  }
  int margin = max_key_size + 64; // enough to send the next VALUE line
  if (obuf.available() > size + margin) {
    while (const mem *m = odata_.pop())
      send_n(m->data, m->size);
    send(CRLF);
    return false;
  } else {
    set_state(session_write_data);
    return flush();
  }
}

void
text_session::client_error(const char *fmt, ...)
{
  send("CLIENT ERROR ");
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
  send(CRLF);
  set_state(session_write_result);
  throw client_error_t();
}

void
text_session::server_error(const char *fmt, ...)
{
  send("SERVER ERROR ");
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
  send(CRLF);
  set_state(session_write_result);
  throw server_error_t();
}

void
text_session::parse_key()
{
  key_ = consume_token(args_);
  if (key_.empty())
    client_error("missing key");
}

void
text_session::parse_update(unsigned long *bytes)
{
  parse_key();
  if (!consume_int(args_, &flags_)) {
    client_error("missing flags");
  } else if (!consume_int(args_, &exptime_)) {
    client_error("missing exptime");
  } else if (!consume_int(args_, bytes)) {
    client_error("missing bytes");
  }
}

bool
text_session::del()
{
  parse_key();
  parse_noreply();
  cache_error_t res = money.del(key_);
  send_cache_result(res);
  return false;
}

void
text_session::send_cache_result(cache_error_t res)
{
  switch (res) {
  case cache_error_t::stored:
    sendln("STORED");
    break;
  case cache_error_t::deleted:
    sendln("DELETED");
    break;
  case cache_error_t::notfound:
    sendln("NOT_FOUND");
    break;
  case cache_error_t::set_error:
    sendln("NOT_STORED");
    break;
  case cache_error_t::cas_exists:
    sendln("EXISTS");
    break;
  }
  set_state(session_write_result);
}

bool
text_session::incr_decr(bool incr)
{
  parse_key();

  uint64_t v;
  if (!consume_u64(args_, &v)) {
    client_error("missing value");
    return false;
  }

  parse_noreply();

  cache_error_t res = incr ? money.incr(key_, v, &v)
    : money.decr(key_, v, &v);

  if (res == cache_error_t::stored) {
    sendf("%llu", v);
  } else {
    send_cache_result(res);
  }
  return false;
}

bool
text_session::cas()
{
  unsigned long bytes;
  parse_update(&bytes);
  if (!consume_u64(args_, &unique_))
    client_error("missing cas unique");
  parse_noreply();

  return recv_data(bytes);
}

bool
text_session::touch()
{
  unsigned long exptime;
  parse_key();
  if (!consume_int(args_, &exptime))
    client_error("missing exptime");
  parse_noreply();
  money.touch(key_, exptime);
  sendln("TOUCHED");
  return false;
}

bool
text_session::flush_all()
{
  unsigned long delay;
  if (!consume_int(args_, &delay))
    delay = 0;
  parse_noreply();
  money.flush_all(delay);
  return false;
}

bool
text_session::version()
{
  sendln("VERSION " PACKAGE_VERSION);
  set_state(session_write_result);
  return false;
}

bool
text_session::stats()
{
  send_stat("version", PACKAGE_VERSION);
  send_stat("pointer_size", sizeof(void*));
  send_stat("cmd_get", money.get_count());
  send_stat("cmd_set", money.set_count());
  send_stat("cmd_flush", money.flush_count());
  send_stat("cmd_touch", money.touch_count());
  send_stat("get_hits", money.get_hit_count());
  send_stat("get_misses", money.get_miss_count());
  send_stat("bytes", money.bytes());
  send_stat("buckets", money.buckets());
  send_stat("keys", money.keys());
  send("END" CRLF);
  set_state(session_write_result);
  return false;
}

bool                            // XXX - we never block
text_session::dispatch_write()
{
  rope data = rope(idata_, idata_); // XXX
  cache_error_t res;
  if (cmd_.is("set")) {
    res = money.set(key_, flags_, exptime_, data);
  } else if (cmd_.is("add")) {
    res = money.add(key_, flags_, exptime_, data);
  } else if (cmd_.is("replace")) {
    res = money.replace(key_, flags_, exptime_, data);
  } else if (cmd_.is("append")) {
    res = money.append(key_, data);
  } else if (cmd_.is("prepend")) {
    res = money.prepend(key_, data);
  } else if (cmd_.is("cas")) {
    res = money.cas(key_, flags_, exptime_, unique_, data);
  } else {
    server_error("confused by command: %.*s", cmd_.used(), cmd_.headp());
    return false;
  }

  send_cache_result(res);
  return false;
}

bool
text_session::dispatch()
{
  if (cmd_.empty()) {
    set_state(session_write_prompt);    // ignore empty commands
    return false;
  } else if (cmd_.is("get")) {
    return get(false);
  } else if (cmd_.is("gets")) {
    return get(true);
  } else if (cmd_.is("set") || cmd_.is("add") || cmd_.is("replace") ||
             cmd_.is("append") || cmd_.is("prepend")) {
    unsigned long bytes;
    parse_update(&bytes);
    parse_noreply();
    return recv_data(bytes);
  } else if (cmd_.is("incr")) {
    return incr_decr(true);
  } else if (cmd_.is("decr")) {
    return incr_decr(false);
  } else if (cmd_.is("delete")) {
    return del();
  } else if (cmd_.is("cas")) {
    return cas();
  } else if (cmd_.is("touch")) {
    return touch();
  } else if (cmd_.is("flush_all")) {
    return flush_all();
  } else if (cmd_.is("version")) {
    return version();
  } else if (cmd_.is("stats")) {
    return stats();
  } else if (cmd_.is("quit")) {
    set_state(session_stopping);
    return false;
  } else {
    log << INFO << "unknown command: " << cmd_ << endl;
    client_error("unknown command: '%.*s'", cmd_.used(), cmd_.headp());
    return false;
  }
}

// XXX - we leave the trailing CRLF, interpreted as an empty command
bool
text_session::recv_data(size_t bytes)
{
  idata_ = mem_alloc(bytes);       // XXX - memory chunk size
  size_t ready = min(bytes, (size_t)ibuf.used());
  memcpy(idata_->data, ibuf.headp(), ready);
  ibuf.notify_read(ready);
  set_state(session_execute_write);
  if (ready == bytes) {
    return false;
  } else {
    in.async_read(boost::asio::mutable_buffers_1(idata_->data, bytes),
                  boost::asio::transfer_exactly(bytes - ready),
                  callback_);
    return true;
  }
}

bool
text_session::send_prompt()
{
  set_state(session_read_command);
  if (prompt_) {
    send(prompt_);
    return flush();
  } else {
    return false;
  }
}

bool
text_session::recv_command()
{
  noreply_ = false;
  set_state(session_execute_command);
  if (cmd_ready(0)) {
    return false;
  } else {
    ibuf.compact();
    in.async_read(boost::asio::mutable_buffers_1(ibuf.tailp(), ibuf.available()),
                  cmd_callback_, callback_);
    return true;
  }
}

bool
text_session::cmd_ready(size_t additional)
{
  const char *end = find_end_of_command(ibuf.headp(), ibuf.used() + additional);
  if (end) {
    ibuf.notify_write(additional);
    args_ = ibuf.sub(end - ibuf.headp());
    cmd_ = consume_token(args_);
    log << INFO << "cmd> " << cmd_ << args_ << std::endl;
    return true;
  } else if (additional == ibuf.available()) {
    // We can't buffer the command, so hang up the phone.
    state_ = session_stopping;
    log << INFO << "command overflow" << std::endl;
    return true;
  }
  return false;
}

size_t
text_session::cmd_callback(boost::system::error_code ec, size_t bytes)
{
  if (ec)
    return 0;
  else
    return cmd_ready(bytes) ? 0 : ibuf.available() - bytes;
}

void
text_session::callback(boost::system::error_code ec, size_t bytes)
{
  if (ec) {
    log << ERROR << "IO error: " << ec.message() << std::endl;
    set_state(session_stopping);
  }
  loop();
}

void
text_session::loop()
{
  bool blocked = false;
  while (not blocked) {
    try {
      switch (state_) {
      case session_write_prompt:
        obuf.reset();
        blocked = send_prompt();
        continue;
      case session_read_command:
        obuf.reset();
        blocked = recv_command();
        continue;
      case session_execute_command:
        blocked = dispatch();
        continue;
      case session_execute_write:
        blocked = dispatch_write();
        continue;
      case session_write_data:
        obuf.reset();
        blocked = send_data();
        continue;
      case session_write_result:
        set_state(session_write_prompt);
        blocked = flush();
        continue;
      case session_stopping:
        io_service.dispatch(done_);
        return;
      }
    } catch (client_error_t) {
      continue;
    } catch (server_error_t) {
      continue;
    }
    assert(0);
  }
}

void
text_session::interact(session_done done)
{
  done_ = done;
  state_ = session_write_prompt;
  loop();
}

session *
text_session_new(boost::asio::io_service &io_service,
                 class cache &c, stream &in, stream &out,
                 std::ostream &log, const char *prompt)
{
  return new text_session(io_service, c, in, out, log, prompt);
}
