/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_TCP_IFACE_H
#define UCT_TCP_IFACE_H

#include <uct/tl/tl_base.h>

#define UCT_TCP_TL_NAME    "tcp"
#define UCT_TCP_DEV_NAME   "eth0"


typedef struct uct_tcp_iface {
    uct_base_iface_t        super;
} uct_tcp_iface_t;


typedef struct uct_tcp_iface_config {
    uct_iface_config_t      super;
} uct_tcp_iface_config_t;


#endif
