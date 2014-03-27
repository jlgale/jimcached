AC_INIT([jimcached], [0.1], [jlgale@gmail.com], \
        [jimcached], [http://github.com/jlgale])
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([src/jimcached.cc])
AM_INIT_AUTOMAKE([no-define foreign])
AC_CONFIG_HEADERS([config.h])
AC_PROG_CC
AC_PROG_CXX([clang++ g++])

AC_CONFIG_MACRO_DIR([m4])
AX_BOOST_BASE([1.54],, [AC_MSG_ERROR([jimcached needs Boost, but it was not found in your system])])
AX_BOOST_SYSTEM

#CXXFLAGS="$CXXFLAGS -Wall -Werror -Wextra -Wmissing-declarations -std=c++11 -stdlib=libc++"
CXXFLAGS="$CXXFLAGS -march=corei7 -pthread -Wall -Werror -Wno-strict-aliasing -Wno-sign-compare -std=c++11"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
