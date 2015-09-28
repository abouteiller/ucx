#
# Copyright (C) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
# Copyright (C) The University of Tennessee and The University 
#               of Tennessee Research Foundation. 2015. ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#
tcp_happy="no"
AC_ARG_ENABLE([tcp],
              [AC_HELP_STRING([--enable-tcp],
                              [Enable TCP/IP])],
                              [],
                              [enable_tcp=yes])

AS_IF([test "x$enable_tcp" != xno],
      [AC_CHECK_HEADERS([sys/socket.h netinet/in.h netinet/tcp.h],
            [tcp_happy="yes"],
            [],
            [[#ifdef STDC_HEADERS
              # include <stdlib.h>
              # include <stddef.h>
              #else
              # ifdef HAVE_STDLIB_H
              #  include <stdlib.h>
              # endif
              #endif]])
       AC_CHECK_DECLS([AF_INET, SOCK_STREAM],
             [],
             [tcp_happy="no"],
             [[#ifdef STDC_HEADERS
               # include <stdlib.h>
               # include <stddef.h>
               #else
               # ifdef HAVE_STDLIB_H
               #  include <stdlib.h>
               # endif
               #endif
               #include <sys/types.h>
               #include <sys/socket.h>]])
       AS_IF([test "x$tcp_happy" == xyes],
             [AC_DEFINE([HAVE_TCP], 1, [tcp support])
              transports="${transports},tcp"],
             [AC_MSG_WARN([TCP/IP support not found.])
              enable_tcp="no"])],
      [])

AM_CONDITIONAL([HAVE_TCP], [test "x$tcp_happy" != xno])
