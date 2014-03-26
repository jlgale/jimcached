#include <boost/asio.hpp>
#include <functional>

class stream
{
public:
  typedef std::function<void (boost::system::error_code, size_t)> handler_t;
  typedef std::function<size_t (boost::system::error_code, size_t)> complete_t;
  virtual void async_read(const boost::asio::mutable_buffers_1 &mb,
                          const complete_t &c, const handler_t &h) = 0;
  virtual void async_write(const boost::asio::const_buffers_1 &cb,
                           const handler_t &h) = 0;
};

class descriptor_stream : public stream
{
  boost::asio::posix::stream_descriptor &d;
  
public:
  descriptor_stream(boost::asio::posix::stream_descriptor &d) : d(d) { }
  typedef std::function<void (boost::system::error_code, size_t)> handler_t;
  void async_read(const boost::asio::mutable_buffers_1 &mb,
                  const complete_t &c, const handler_t &h)
  {
    boost::asio::async_read(d, mb, c, h);
  }
  void async_write(const boost::asio::const_buffers_1 &cb, const handler_t &h)
  {
    boost::asio::async_write(d, cb, h);
  }
};

class tcp_stream : public stream
{
  boost::asio::ip::tcp::socket &s;
  
public:
  tcp_stream(boost::asio::ip::tcp::socket &s) : s(s) { }
  typedef std::function<void (boost::system::error_code, size_t)> handler_t;
  void async_read(const boost::asio::mutable_buffers_1 &mb,
                  const complete_t &c, const handler_t &h)
  {
    boost::asio::async_read(s, mb, c, h);
  }
  void async_write(const boost::asio::const_buffers_1 &cb, const handler_t &h)
  {
    boost::asio::async_write(s, cb, h);
  }
};
