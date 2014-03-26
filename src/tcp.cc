#include "buffer.h"
#include "cache.h"
#include "session.h"
#include "pool.h"

#include <memory>
#include <boost/asio.hpp>

using boost::asio::io_service;
using boost::asio::ip::tcp;
using namespace std;

class tcp_session 
{
public:

  tcp::socket & socket() { return socket_; }

  void interact(session_done done)
  {
    session_interact(*session_, done);
  }

  tcp_session(io_service& io_service, cache &cache, ostream &log)
    : socket_(io_service), stream_(socket_),
      session_(session_new(io_service, cache, stream_, stream_, log, NULL),
               session_delete) {}

private:
  tcp::socket socket_;
  tcp_stream stream_;
  std::unique_ptr<Session, decltype(&session_delete)> session_;
};

class tcp_server
{
public:
  tcp_server(cache &cache, ostream &log, io_service_pool& pool, int port)
    : cache_(cache), log_(log), pool_(pool),
      acceptor(pool.get_io_service(), tcp::endpoint(tcp::v4(), port))
  {
    start_accept();
  }

private:
  void start_accept()
  {
    tcp_session *new_connection
      = new tcp_session(pool_.get_io_service(), cache_, log_);

    acceptor.async_accept(new_connection->socket(),
                          bind(&tcp_server::handle_accept, this,
                                 new_connection, placeholders::_1));
  }

  void handle_accept(tcp_session *new_connection,
                     const boost::system::error_code& error)
  {
    if (!error) {
      log_ << "new connection " << (void*) new_connection << endl;
      new_connection->interact([=](){
          log_ << "connection close " << (void*) new_connection << endl;
          delete new_connection;
        });
    } else {
      log_ << "accept() error: " << error << endl;
      delete new_connection;
    }

    start_accept();
  }

  cache &cache_;
  ostream &log_;
  io_service_pool &pool_;
  tcp::acceptor acceptor;
};

tcp_server *
tcp_server_new(cache &c, ostream &log, char *iface,
               int memcache_port, int listen_backlog, io_service_pool &pool)
{
  return new tcp_server(c, log, pool, memcache_port);
}

void
tcp_server_delete(tcp_server *tcp)
{
  delete tcp;
}
