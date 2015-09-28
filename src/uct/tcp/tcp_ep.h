/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
* Copyright (C) The University of Tennessee and The University                                                                        
*               of Tennessee Research Foundation. 2015. ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#ifndef UCT_SYSV_EP_H
#define UCT_SYSV_EP_H

#include <uct/api/uct.h>
#include <uct/tl/tl_base.h>

#include "ucs/type/class.h"

typedef struct uct_tcp_ep_addr {
    int                ep_id;
} uct_tcp_ep_addr_t;

typedef struct uct_tcp_ep {
    uct_base_ep_t      super;
    struct uct_tcp_ep *next;
} uct_tcp_ep_t;

UCS_CLASS_DECLARE_NEW_FUNC(uct_tcp_ep_t, uct_ep_t, uct_iface_t*, const struct sockaddr *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_tcp_ep_t, uct_ep_t);

ucs_status_t uct_tcp_ep_put_short(uct_ep_h tl_ep, const void *buffer, unsigned length, 
                                   uint64_t remote_addr, uct_rkey_t rkey);
ucs_status_t uct_tcp_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                                  const void *payload, unsigned length);
#endif
