#include "buffer.h"
#include "murmur2.h"
#include "cache.h"
#include "error.h"
#include "session.h"
#include "utils.h"

#include "config.h"

#include <cassert>
#include <cstdarg>
#include <cstring>

#include <boost/asio.hpp>

using namespace std;

enum session_state {
  write_prompt,
  read_command,
  execute_command,
  read_data,
  execute_write,
  write_data,
  write_result,
  stopping,
};

/*
static const char *
sss(session_state s)
{
  switch (s) {
  case write_prompt: return "write_prompt";
  case read_command: return "read_command";
  case execute_command: return "execute_command";
  case read_data: return "read_data";
  case execute_write: return "execute_write";
  case write_data: return "write_data";
  case write_result: return "write_result";
  case stopping: return "stopping";
  }
  return "unknown";
}
*/

class Session
{
  boost::asio::io_service &io_service;
  cache &money;
  stream &in;
  stream &out;
  std::ostream &log;
  const char *prompt_;
  bool noreply_;
  buffer ibuf;
  buffer obuf;
  session_state state_ = write_prompt;
  session_done done_;
  buffer args_;
  buffer cmd_;
  unsigned long flags_, exptime_;
  buffer key_;
  mem *idata_ = NULL;
  const_rope odata_;
  std::function<void (boost::system::error_code, size_t)> callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> void {
    callback(ec, bytes);
  };
  std::function<size_t (boost::system::error_code, size_t)> cmd_callback_ =
    [this](boost::system::error_code ec, size_t bytes) -> size_t {
    return cmd_callback(ec, bytes);
  };

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
  /*
  void append(buffer &args);
  void prepend(buffer &args);
  void cas(buffer &args);
  void flush_all(buffer &args);
  */
  bool get(bool cas_unique);
  bool set_add_replace();
  bool incr_decr(bool incr);
  bool del();
  bool stats();
  bool version();
  bool touch();

  // Command helpers
  bool parse_update(unsigned long *bytes);
  bool parse_key();
  void stored_if(bool stored);
  bool parse_noreply();

  // Session helpers
  void callback(boost::system::error_code ec, size_t bytes);
  size_t cmd_callback(boost::system::error_code ec, size_t bytes);
  void loop();

  bool dispatch();
  bool dispatch_write();

public:
  class closed { };

  Session(boost::asio::io_service &io_service,
          class cache &c, stream &in, stream &out,
          std::ostream &log, const char *prompt)
    : io_service(io_service), money(c), in(in), out(out), log(log),
      prompt_(prompt), noreply_(false), ibuf(2048), obuf(2048) { }
  ~Session() { }
  void interact(session_done done);
};

class stream_buffer : public std::streambuf
{
  buffer &buf_;
public:
  stream_buffer(buffer &buf) : buf_(buf) { }
  int overflow(int c = EOF)
  {
    if (buf_.available() > 0) {
      char a = c;
      buf_.write(&a, 1);
      return c;
    } else {
      return EOF;
    }
  }
};


class session_error_category : public std::error_category
{
  session_error_category() { }
  const char *name() const noexcept { return "session_error"; }
  string message (int val) const { return "some_error!?"; } // XXX

public:
  static session_error_category& instance() {
    static session_error_category instance_;
    return instance_;
  }
};

class cache_error_category : public std::error_category
{
  cache_error_category() { }
  const char *name() const noexcept { return "cache_error"; }
  string message (int val) const { return "some_error"; } // XXX

public:
  static cache_error_category& instance() {
    static cache_error_category instance_;
    return instance_;
  }
};

void
Session::send(const char *msg)
{
  send_n(msg, strlen(msg));
}

void
Session::sendln(const char *msg)
{
  send(msg);
  send(CRLF);
}

bool
Session::parse_noreply()
{
  const buffer nr = consume_token(args_);
  if (nr.empty()) {
    return false;
  } else if (!nr.is("noreplay")) {
    client_error("expected noreply or end of command");
    return true;
  } else {
    noreply_ = true;
    return false;
  }
}

static boost::system::error_code
success()
{
  return boost::system::error_code();
}

bool
Session::flush()
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
Session::send_n(const char *msg, size_t bytes)
{
  if (!noreply_)
    obuf.write(msg, bytes);
}

void
Session::send_async(const char *msg, size_t bytes)
{
  assert(obuf.used() == 0);     // XXX - we could flush now
  // XXX - we could take a rope...
  out.async_write(boost::asio::const_buffers_1(msg, bytes), callback_);
}

void
Session::vsendf(const char *fmt, va_list ap)
{
  if (not noreply_) {
    int n = vsnprintf(obuf.tailp(), obuf.available(), fmt, ap);
    obuf.notify_write(n);
    send(CRLF);
  }
}

void
Session::sendf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
}

void
Session::send_stat(const char *name, const char *val)
{
  sendf("STAT %s %s", name, val);
}

void
Session::send_stat(const char *name, uint64_t val)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "%lu", val);
  send_stat(name, buf);
}

bool
Session::send_data()
{
  const mem *m = odata_.pop();
  if (!m) {
    send(CRLF);
    state_ = execute_command;
    return false;
  }
  state_ = write_data;
  send_async(m->data, m->size);
  return true;
}

bool
Session::get(bool cas_unique)
{
  buffer key = consume_token(args_);
  if (key.empty()) {
    send("END" CRLF);
    state_ = write_result;
    return false;
  }

  cache::ref e = money.get(key);
  if (e == nullptr) {
    sendln("NOT_FOUND");
    state_ = write_result;
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
  state_ = write_data;
  return flush();
}

void
Session::client_error(const char *fmt, ...)
{
  send("CLIENT ERROR ");
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
  send(CRLF);
  state_ = write_result;
}

void
Session::server_error(const char *fmt, ...)
{
  send("SERVER ERROR ");
  va_list ap;
  va_start(ap, fmt);
  vsendf(fmt, ap);
  va_end(ap);
  send(CRLF);
  state_ = write_result;
}

bool
Session::parse_key()
{
  key_ = consume_token(args_);
  if (key_.empty()) {
    client_error("missing key");
    return true;
  } else {
    return false;
  }
}

bool
Session::parse_update(unsigned long *bytes)
{
  if (parse_key())
    return true;
  if (!consume_int(args_, &flags_)) {
    client_error("missing flags");
    return true;
  } else if (!consume_int(args_, &exptime_)) {
    client_error("missing exptime");
    return true;
  } else if (!consume_int(args_, bytes)) {
    client_error("missing bytes");
    return true;
  }
  return false;
}

bool
Session::set_add_replace()
{
  unsigned long bytes;
  if (parse_update(&bytes) || parse_noreply())
    return false;
  return recv_data(bytes);
}

bool
Session::del()
{
  if (parse_key() && parse_noreply()) {
    cache_error_t res = money.del(key_);
    send_cache_result(res);
  }
  return false;
}

/*
void
Session::append(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  parse_noreply(args);
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.append(key, data);
              result(done, cache_error_code(res));
            });
}

void
Session::prepend(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  parse_noreply(args);
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.prepend(key, data);
              result(done, cache_error_code(res));
            });
}
*/

void
Session::send_cache_result(cache_error_t res)
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
  state_ = write_result;
}

bool
Session::incr_decr(bool incr)
{
  if (parse_key())
    return false;

  uint64_t v;
  if (!consume_u64(args_, &v)) {
    client_error("missing value");
    return false;
  }

  if (parse_noreply())
    return false;

  cache_error_t res = incr ? money.incr(key_, v, &v)
    : money.decr(key_, v, &v);
    
  if (res == cache_error_t::stored) {
    sendf("%llu", v);
  } else {
    send_cache_result(res);
  }
  return false;
}

/*
void
Session::cas(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  uint64_t unique;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  if (!consume_u64(args, &unique))
    client_error("missing cas unique");
  parse_noreply(args);
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.cas(key, flags, exptime, unique, data);
              result(done, cache_error_code(res));
            });
}
*/

bool
Session::touch()
{
  unsigned long exptime;
  if (parse_key())
    return false;
  if (!consume_int(args_, &exptime)) {
    client_error("missing exptime");
    return false;
  }
  if (parse_noreply())
    return false;
  money.touch(key_, exptime);
  sendln("TOUCHED");
  return false;
}

/*
void
Session::flush_all(buffer &args, session_result done)
{
  unsigned long delay;
  if (!consume_int(args, &delay))
    delay = 0;
  parse_noreply(args);
  money.flush_all(delay);
  result(done, success());
}
*/

bool
Session::version()
{
  sendln("VERSION " PACKAGE_VERSION);
  state_ = write_result;
  return false;
}

bool
Session::stats()
{
  stream_buffer buf(obuf);
  std::ostream os(&buf);
  send_stat("version", PACKAGE_VERSION);
  send_stat("pointer_size", sizeof(void*));
  send_stat("bytes", money.bytes());
  send_stat("buckets", money.buckets());
  send_stat("keys", money.keys());
  send("END" CRLF);
  state_ = write_result;
  return false;
}

bool                            // XXX - we never block
Session::dispatch_write()
{
  rope data = rope(idata_, idata_); // XXX
  cache_error_t res;
  if (cmd_.is("set")) {
    res = money.set(key_, flags_, exptime_, data);
  } else if (cmd_.is("add")) {
    res = money.add(key_, flags_, exptime_, data);
  } else if (cmd_.is("replace")) {
    res = money.replace(key_, flags_, exptime_, data);
  } else {
    server_error("confused by command: %.*s", cmd_.used(), cmd_.headp());
    return false;
  }

  send_cache_result(res);
  return false;
}

bool
Session::dispatch()
{
  //log << "cmd> " << cmd_ << " " << args_ << std::endl;
  if (cmd_.empty()) {
    state_ = write_prompt;      // ignore empty commands
    return false;
  } else if (cmd_.is("get")) {
    return get(false);
  } else if (cmd_.is("gets")) {
    return get(true);
  } else if (cmd_.is("set") || cmd_.is("add") || cmd_.is("replace")) {
    return set_add_replace();
  } else if (cmd_.is("incr")) {
    return incr_decr(true);
  } else if (cmd_.is("decr")) {
    return incr_decr(false);
  } else if (cmd_.is("delete")) {
    return del();
    /*
  } else if (cmd.is("cas")) {
    cas(cmdline, done);
  } else if (cmd.is("append")) {
    append(cmdline, done);
  } else if (cmd.is("prepend")) {
    prepend(cmdline, done);
    */
  } else if (cmd_.is("touch")) {
    return touch();
    /*
  } else if (cmd.is("flush_all")) {
    flush_all(cmdline, done);
    */
  } else if (cmd_.is("version")) {
    return version();
  } else if (cmd_.is("stats")) {
    return stats();
  } else if (cmd_.is("quit")) {
    state_ = stopping;
    return false;
  } else {
    log << "unknown command: " << cmd_ << endl;
    client_error("unknown command: '%.*s'", cmd_.used(), cmd_.headp());
    return false;
  }
}

// XXX - we leave the trailing CRLF, interpreted as an empty command
bool
Session::recv_data(size_t bytes)
{
  state_ = read_data;
  idata_ = mem_alloc(bytes);       // XXX - memory chunk size
  size_t ready = min(bytes, (size_t)ibuf.used());
  memcpy(idata_->data, ibuf.headp(), ready);
  ibuf.notify_read(ready);
  state_ = execute_write;
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
Session::send_prompt()
{
  state_ = read_command;
  if (prompt_) {
    send(prompt_);
    return flush();
  } else {
    return false;
  }
}

bool
Session::recv_command()
{
  noreply_ = false;
  state_ = execute_command;
  if (cmd_callback(success(), 0) == 0) {
    return false;
  } else {
    ibuf.compact();
    in.async_read(boost::asio::mutable_buffers_1(ibuf.tailp(), ibuf.available()),
                  cmd_callback_, callback_);
    return true;
  }
}

size_t
Session::cmd_callback(boost::system::error_code ec, size_t bytes)
{
    if (ec)
      return 0;

    const char *end = (char *)memchr(ibuf.headp(), '\n', ibuf.used() + bytes);
    if (end) {
      ibuf.notify_write(bytes);
      args_ = ibuf.sub(end - ibuf.headp() + 1);
      cmd_ = consume_token(args_);
      return 0;
    }

    return ibuf.available() - bytes;
}

void
Session::callback(boost::system::error_code ec, size_t bytes)
{
  if (ec) {
    switch (state_) {
    case write_prompt:
    case write_data:
    case write_result:
      log << "write error: " << ec << std::endl;
      state_ = stopping;
      break;
    case read_command:
      log << "read error: " << ec << std::endl;
      state_ = stopping;
      break;
    case execute_command:
    case execute_write:
      break;
    case read_data:
      mem_free(idata_);
      log << "read error: " << ec << std::endl;
      state_ = stopping;
      break;
    case stopping:
      break;
    }
  }
  loop();
}

void
Session::loop()
{
  bool blocked = false;
  while (not blocked) {
    switch (state_) {
    case write_prompt:
      obuf.reset();
      blocked = send_prompt();
      continue;
    case read_command:
      obuf.reset();
      blocked = recv_command();
      continue;
    case execute_command:
      blocked = dispatch();
      continue;
    case execute_write:
      blocked = dispatch_write();
      continue;
    case read_data:
      assert(0);                // XXX
      continue;
    case write_data:
      obuf.reset();
      blocked = send_data();
      continue;
    case write_result:
      state_ = write_prompt;
      blocked = flush();
      continue;
    case stopping:
      io_service.dispatch(done_);
      return;
    }
    assert(0);
    //log << sss(cur) << " -> " << sss(state_) << (blocked ? " (blocked)" : "") << std::endl;
  }
}

void
Session::interact(session_done done)
{
  done_ = done;
  state_ = write_prompt;
  loop();
}

Session *
session_new(boost::asio::io_service &io_service,
            class cache &c, stream &in, stream &out,
            std::ostream &log, const char *prompt)
{
  return new Session(io_service, c, in, out, log, prompt);
}

void
session_interact(Session &session, session_done done)
{
  session.interact(done);
}

void
session_delete(Session *session)
{
  delete session;
}
