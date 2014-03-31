#include "buffer.h"
#include "murmur2.h"
#include "cache.h"
#include "error.h"
#include "session.h"
#include "utils.h"
#include "log.h"

#include "config.h"

#include <cassert>
#include <cstdarg>
#include <cstring>

#include <boost/asio.hpp>

using namespace std;
using namespace std::placeholders;

enum class session_error_t {
  closed=1,
  client_error,
};

typedef std::function<void (buffer)> session_cmdline;
typedef std::function<void (rope)> session_recv;
typedef std::function<void (std::error_code err)> session_result;

class Session
{
  boost::asio::io_service &io_service;
  cache &money;
  stream &in;
  stream &out;
  std::ostream &log;
  const char *prompt;
  bool noreply;
  buffer ibuf;
  buffer obuf;

  // Input
  void recv_command_done(session_result err, session_cmdline done,
                         boost::system::error_code ec, size_t bytes);
  void recv_command_read(session_result err, session_cmdline done);
  void recv_command(session_result err, session_cmdline done);
  void recv_data(size_t bytes, session_result err, session_recv done);

  // Output
  void send_async(const char *msg, size_t bytes, session_result done);
  void send_n(const char *msg, size_t bytes);
  void send(const char *msg);
  void sendln(const char *msg);
  void sendf(const char *fmt, ...);
  void vsendf(const char *fmt, va_list ap);
  void send_stat(const char *name, const char *val);
  void send_stat(const char *name, uint64_t val);
  void client_error(const char *fmt, ...);
  void flush(session_result done);

  // Command handlers
  void get(buffer &args, session_result done);
  void gets(buffer &args, session_result done);
  void set(buffer &args, session_result done);
  void add(buffer &args, session_result done);
  void replace(buffer &args, session_result done);
  void version(buffer &args, session_result done);
  void del(buffer &args, session_result done);
  void append(buffer &args, session_result done);
  void prepend(buffer &args, session_result done);
  void incr(buffer &args, session_result done);
  void decr(buffer &args, session_result done);
  void cas(buffer &args, session_result done);
  void touch(buffer &args, session_result done);
  void stats(buffer &args, session_result done);
  void flush_all(buffer &args, session_result done);

  // Command helpers
  void send_rope(const_rope data, session_result done, std::error_code ec);
  void get_one(const buffer &key, bool cas_unique, session_result done);
  void get_iter(buffer &args, session_result done,
                bool cas_unique, std::error_code ec);

  const buffer parse_update(buffer &args, unsigned long *flags,
                            unsigned long *exptime, unsigned long *bytes);
  const buffer parse_key(buffer &args);
  void stored_if(bool stored);
  void parse_noreply(buffer &args);

  // Session helpers
  void execute_one_done(session_result done, std::error_code ec);
  void execute_one_read(session_result done, buffer cmdline);
  void execute_one(session_result done);
  void execute_common(buffer &cmdline, session_result done);
  void dispatch(buffer &cmdline, session_result done);
  void interact_iter(session_done done, std::error_code ec);
  void result(session_result done, std::error_code ec);

public:
  class closed { };

  Session(boost::asio::io_service &io_service,
          class cache &c, stream &in, stream &out,
          std::ostream &log, const char *prompt)
    : io_service(io_service), money(c), in(in), out(out), log(log),
      prompt(prompt), noreply(false), ibuf(2048), obuf(2048) { }
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

static error_code
cache_error_code(cache_error_t err)
{
  return error_code((int)err, cache_error_category::instance());
}

static error_code
session_error_code(session_error_t err)
{
  return error_code((int)err, session_error_category::instance());
}

void
Session::result(session_result done, error_code ec)
{
  io_service.dispatch(bind(done, ec));
}

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

void
Session::parse_noreply(buffer &args)
{
  const buffer nr = consume_token(args);
  if (nr.empty())
    return;
  if (!nr.is("noreplay"))
    client_error("expected noreply or end of command");
  noreply = true;
}

static error_code
success()
{
  return error_code();
}

static error_code
convert_error(boost::system::error_code ec)
{
  return make_error_code(static_cast<errc>(ec.value()));
}

void
Session::flush(session_result done)
{
  if (noreply) {
    result(done, success());
    return;
  }
  
  out.async_write(boost::asio::const_buffers_1(obuf.headp(), obuf.used()),
                  [=](const boost::system::error_code &err, size_t bytes) {
                    obuf.reset();
                    result(done, convert_error(err));
                  });
}

void
Session::send_n(const char *msg, size_t bytes)
{
  if (!noreply)
    obuf.write(msg, bytes);
}

void
Session::send_async(const char *msg, size_t bytes, session_result done)
{
  assert(obuf.used() == 0);     // XXX - we could flush now
  // XXX - we could take a rope...
  out.async_write(boost::asio::const_buffers_1(msg, bytes),
                  [=](const boost::system::error_code &err, size_t bytes) {
                    result(done, convert_error(err));
                  });
}

void
Session::vsendf(const char *fmt, va_list ap)
{
  if (not noreply) {
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

void
Session::send_rope(const_rope data, session_result done, error_code ec)
{
  if (ec) {
    result(done, ec);
    return;
  }
  const mem *m = data.pop();
  if (!m) {
    send(CRLF);
    result(done, success());
    return;
  }
  send_async(m->data, m->size,
             bind(&Session::send_rope, this, data, done, _1));
}

void
Session::get_one(const buffer &key, bool cas_unique, session_result done)
{
  cache::ref e = money.get(key);
  if (e == nullptr) {
    result(done, cache_error_code(cache_error_t::notfound));
    return;
  }

  const_rope data = e->read();
  size_t size = data.size();
  if (cas_unique) {
    uint64_t version = data.hash(e->get_flags());
    sendf("VALUE %.*s %u %u %llu",
          key.used(), key.headp(), e->get_flags(), size, version);
  } else {
    sendf("VALUE %.*s %u %u",
          key.used(), key.headp(), e->get_flags(), size);
  }
  flush([=](error_code ec) {
      if (ec) {
        result(done, ec);
      } else {
        send_rope(data, done, success());
      }
    });
}

void
Session::get_iter(buffer &args, session_result done,
                  bool cas_unique, error_code ec)
{
  if (ec) {
    result(done, ec);
    return;
  }
  const buffer key = consume_token(args);
  if (key.empty()) {
    send("END" CRLF);
    result(done, success());
    return;
  }
  
  get_one(key, cas_unique,
          bind(&Session::get_iter, this, args, done, cas_unique, _1));
}

void
Session::get(buffer &args, session_result done)
{
  // XXX - args on stack?
  get_iter(args, done, false, success());
}

void
Session::gets(buffer &args, session_result done)
{
  // XXX - args on stack?
  get_iter(args, done, true, success());
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
  throw ::client_error();
}

const buffer
Session::parse_key(buffer &args)
{
  const buffer key = consume_token(args);
  if (key.empty())
    client_error("missing key");
  return key;
}

const buffer
Session::parse_update(buffer &args, unsigned long *flags,
                      unsigned long *exptime, unsigned long *bytes)
{
  const buffer key = parse_key(args);
  if (!consume_int(args, flags))
    client_error("missing flags");
  if (!consume_int(args, exptime))
    client_error("missing exptime");
  if (!consume_int(args, bytes))
    client_error("missing bytes");
  return key;
}

void
Session::set(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  parse_noreply(args);
  
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.set(key, flags, exptime, data);
              result(done, cache_error_code(res));
            });
}

void
Session::add(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  parse_noreply(args);
  
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.add(key, flags, exptime, data);
              result(done, cache_error_code(res));
            });
}

void
Session::replace(buffer &args, session_result done)
{
  unsigned long flags, exptime, bytes;
  const buffer key = parse_update(args, &flags, &exptime, &bytes);
  parse_noreply(args);
  recv_data(bytes, done,
            [=](rope data) {
              cache_error_t res = money.replace(key, flags, exptime, data);
              result(done, cache_error_code(res));
            });
}

void
Session::del(buffer &args, session_result done)
{
  const buffer key = parse_key(args);
  parse_noreply(args);
  cache_error_t res = money.del(key);
  result(done, cache_error_code(res));
}

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

void
Session::incr(buffer &args, session_result done)
{
  const buffer key = parse_key(args);
  uint64_t v;
  if (!consume_u64(args, &v))
    client_error("missing value");
  parse_noreply(args);
  cache_error_t res = money.incr(key, v, &v);
  if (res == cache_error_t::stored) {
    sendf("%llu", v);
    result(done, success());
  } else {
    result(done, cache_error_code(res));
  }
}

void
Session::decr(buffer &args, session_result done)
{
  const buffer key = parse_key(args);
  uint64_t v;
  if (!consume_u64(args, &v))
    client_error("missing value");
  parse_noreply(args);
  cache_error_t res = money.decr(key, v, &v);
  if (res == cache_error_t::stored) {
    sendf("%llu", v);
    result(done, success());
  } else {
    result(done, cache_error_code(res));
  }
}

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

void
Session::touch(buffer &args, session_result done)
{
  unsigned long exptime;
  const buffer key = parse_key(args);
  if (!consume_int(args, &exptime))
    client_error("missing exptime");
  parse_noreply(args);
  money.touch(key, exptime);
  sendln("TOUCHED");
  result(done, success());
}

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

void
Session::version(buffer &cmdline, session_result done)
{
  sendln("VERSION " PACKAGE_VERSION);
  result(done, success());
}

void
Session::stats(buffer &cmdline, session_result done)
{
  stream_buffer buf(obuf);
  std::ostream os(&buf);
  send_stat("version", PACKAGE_VERSION);
  send_stat("pointer_size", sizeof(void*));
  send_stat("bytes", money.bytes());
  send_stat("buckets", money.buckets());
  send_stat("keys", money.keys());
  send("END" CRLF);
  result(done, success());
}

void
Session::dispatch(buffer &cmdline, session_result done)
{
  log << INFO << "cmd> " << cmdline << std::endl;
  const buffer cmd = consume_token(cmdline);
  if (cmd.empty()) {
    result(done, success());    // ignore empty commands
  } else if (cmd.is("get")) {
    get(cmdline, done);
  } else if (cmd.is("gets")) {
    gets(cmdline, done);
  } else if (cmd.is("set")) {
    set(cmdline, done);
  } else if (cmd.is("add")) {
    add(cmdline, done);
  } else if (cmd.is("replace")) {
    replace(cmdline, done);
  } else if (cmd.is("incr")) {
    incr(cmdline, done);
  } else if (cmd.is("decr")) {
    decr(cmdline, done);
  } else if (cmd.is("delete")) {
    del(cmdline, done);
  } else if (cmd.is("cas")) {
    cas(cmdline, done);
  } else if (cmd.is("append")) {
    append(cmdline, done);
  } else if (cmd.is("prepend")) {
    prepend(cmdline, done);
  } else if (cmd.is("touch")) {
    touch(cmdline, done);
  } else if (cmd.is("flush_all")) {
    flush_all(cmdline, done);
  } else if (cmd.is("version")) {
    version(cmdline, done);
  } else if (cmd.is("stats")) {
    stats(cmdline, done);
  } else if (cmd.is("quit")) {
    //done(session_closed); // same thing, right?
    done(session_error_code(session_error_t::closed));
    //throw closed();             // XXX
  } else {
    log << INFO << "unknown command: " << cmd << endl;
    client_error("unknown command: '%.*s'", cmd.used(), cmd.headp());
  }
}

void
Session::recv_data(size_t bytes, session_result err, session_recv done)
{
  mem *m = mem_alloc(bytes);       // XXX - memory chunk size
  size_t ready = min(bytes, (size_t)ibuf.used());
  memcpy(m->data, ibuf.headp(), ready);
  ibuf.notify_read(ready);
  if (ready == bytes) {
    done(rope(m, m));
    return;
  }
  in.async_read(boost::asio::mutable_buffers_1(m->data, bytes),
                boost::asio::transfer_exactly(bytes - ready),
                [=](boost::system::error_code ec, size_t bytes) {
                  if (ec) {
                    mem_free(m);
                    err(convert_error(ec));
                  } else {
                    done(rope(m, m));
                  }
                });
  // XXX - we leave the trailing CRLF, interpreted as an empty command
}

void
Session::recv_command_done(session_result err, session_cmdline done,
                           boost::system::error_code ec, size_t bytes)
{
  ibuf.notify_write(bytes);
  if (ec) {
    result(err, convert_error(ec));
    return;
  }
  const char *end = (char *)memchr(ibuf.headp(), '\n', ibuf.used());
  if (!end) {
    result(err, success());            // XXX - eof
    //throw closed();
    return;
  }
  
  done(ibuf.sub(end - ibuf.headp() + 1));
}

void
Session::recv_command_read(session_result err, session_cmdline done)
{
  in.async_read(boost::asio::mutable_buffers_1(ibuf.tailp(), ibuf.available()),
                [=](boost::system::error_code ec, size_t bytes) -> size_t {
                  if (ec)
                    return 0;
                  if (memchr(ibuf.headp(), '\n', ibuf.used() + bytes)) {
                    return 0;
                  }
                  return ibuf.available() - bytes;
                },
                bind(&Session::recv_command_done, this, err, done, _1, _2));
}

void
Session::recv_command(session_result err, session_cmdline done)
{
  if (prompt) {
    send(prompt);
    flush([=](std::error_code ec) {
        if (ec) {
          result(err, ec);
        } else {
          recv_command_read(err, done);
        }
      });
  } else {
    recv_command_read(err, done);
  }
}

// Dispatch a command and handle common error conditions
void
Session::execute_common(buffer &cmdline, session_result done)
{
  try {
    dispatch(cmdline, [=](error_code ec) {
        if (ec.category() == cache_error_category::instance()) {
          switch(cache_error_t(ec.value())) {
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
          ec = success();
        }
        result(done, ec);
      });
  } catch (::client_error &e) {
    result(done, success());
  }
  /*
  } catch (cache::incr_decr_error) {
    // XXX Is this a client error?
    client_error("value not an integer");
  */
}

void
Session::execute_one_done(session_result done, error_code ec)
{
  gc_unlock();
  ibuf.compact();
  if (ec) {
    result(done, ec);
  } else {
    flush(done);                // XXX - always flush?
  }
}

void
Session::execute_one_read(session_result done, buffer cmdline)
{
  try {
    gc_lock();
    execute_common(cmdline, bind(&Session::execute_one_done,
                                 this, done, _1));
    return;
  } catch (const class client_error &) {
    // okay
  } catch (const class server_error &) {
    // also fine
  }
  // XXX - hmmm
  assert(0);
}

void
Session::execute_one(session_result done)
{
  noreply = false;
  recv_command(done, bind(&Session::execute_one_read, this, done, _1));
}

void
Session::interact_iter(session_done done, error_code ec)
{
  gc_checkpoint();              // XXX - remove
  
  if (ec) {
    io_service.dispatch(done);                     // schedule
    return;
  }
  
  interact(done);
}

void
Session::interact(session_done done)
{
  execute_one(bind(&Session::interact_iter, this, done, _1));
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
