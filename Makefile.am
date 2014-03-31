bin_PROGRAMS = jimcached cachetest standalone loadtest jimcached2 standalone2

AM_CPPFLAGS = $(BOOST_CPPFLAGS)
AM_LDFLAGS = $(BOOST_LDFLAGS)

jimcached_LDADD = $(BOOST_SYSTEM_LIB)
jimcached2_LDADD = $(BOOST_SYSTEM_LIB)
standalone_LDADD = $(BOOST_SYSTEM_LIB)
standalone_CPPFLAGS = -DBOOST_ASIO_DISABLE_EPOLL=1
standalone2_LDADD = $(BOOST_SYSTEM_LIB)
standalone2_CPPFLAGS = -DBOOST_ASIO_DISABLE_EPOLL=1

COMMON_SRC = \
	src/log.h src/log.cc \
	src/cpu.h src/cpu.cc \
	src/gc.h src/gc.cc \
	src/buffer.h \
	src/murmur2.h src/murmur2.cc \
	src/mem.h src/mem.cc \
	src/atime.h \
	src/rope.h \
	src/const_rope.h src/const_rope.cc \
	src/entry.h src/entry.cc \
	src/table.h src/cache.h src/cache.cc

SESSION_SRC = \
	src/error.h \
	src/session.h src/session.cc \
	src/utils.h src/utils.cc

SESSION2_SRC = \
	src/error.h \
	src/session.h src/session2.cc \
	src/utils.h src/utils.cc

jimcached_SOURCES = \
	$(COMMON_SRC) \
	$(SESSION_SRC) \
	src/jimcached.cc \
	src/pool.h src/pool.cc \
	src/tcp.h src/tcp.cc \
	src/service.h src/service.cc

jimcached2_SOURCES = \
	$(COMMON_SRC) \
	$(SESSION2_SRC) \
	src/jimcached.cc \
	src/pool.h src/pool.cc \
	src/tcp.h src/tcp.cc \
	src/service.h src/service.cc

cachetest_SOURCES = \
	$(COMMON_SRC) \
	src/cachetest.cc

loadtest_SOURCES = \
	$(COMMON_SRC) \
	src/loadtest.cc

standalone_SOURCES = \
	$(COMMON_SRC) \
	$(SESSION_SRC) \
	src/standalone.cc

standalone2_SOURCES = \
	$(COMMON_SRC) \
	$(SESSION2_SRC) \
	src/standalone.cc

