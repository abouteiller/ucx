/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "match.h"
#include "eager.h"
#include "rndv.h"

#include <ucp/core/ucp_worker.h>
#include <ucp/core/ucp_request.inl>
#include <ucs/datastruct/mpool.inl>
#include <ucs/datastruct/queue.h>
#include <ucs/debug/instrument.h>


static UCS_F_ALWAYS_INLINE ucs_status_t
ucp_tag_search_unexp(ucp_worker_h worker, void *buffer, size_t count,
                     ucp_datatype_t datatype, ucp_tag_t tag, uint64_t tag_mask,
                     ucp_request_t *req, ucp_tag_recv_info_t *info, unsigned *save_rreq)
{
    ucp_context_h context = worker->context;
    ucp_recv_desc_t *rdesc;
    ucp_tag_hdr_t *hdr;
    ucs_queue_iter_t iter;
    ucs_status_t status;
    ucp_tag_t recv_tag;
    unsigned flags;

    ucs_queue_for_each_safe(rdesc, iter, &context->tag.unexpected, queue) {
        hdr      = (void*)(rdesc + 1);
        recv_tag = hdr->tag;
        flags    = rdesc->flags;
        ucs_trace_req("searching for %"PRIx64"/%"PRIx64"/%"PRIx64" offset %zu, "
                      "checking desc %p %"PRIx64" %c%c%c%c%c",
                      tag, tag_mask, info->sender_tag, req->recv.state.offset,
                      rdesc, recv_tag,
                      (flags & UCP_RECV_DESC_FLAG_FIRST) ? 'f' : '-',
                      (flags & UCP_RECV_DESC_FLAG_LAST)  ? 'l' : '-',
                      (flags & UCP_RECV_DESC_FLAG_EAGER) ? 'e' : '-',
                      (flags & UCP_RECV_DESC_FLAG_SYNC)  ? 's' : '-',
                      (flags & UCP_RECV_DESC_FLAG_RNDV)  ? 'r' : '-');
        if (ucp_tag_recv_is_match(recv_tag, flags, tag, tag_mask,
                                  req->recv.state.offset, info->sender_tag))
        {
            ucp_tag_log_match(recv_tag, req, tag, tag_mask,
                              req->recv.state.offset, "unexpected");
            ucs_queue_del_iter(&context->tag.unexpected, iter);
            if (rdesc->flags & UCP_RECV_DESC_FLAG_EAGER) {
                status = ucp_eager_unexp_match(worker, rdesc, recv_tag, flags,
                                               buffer, count, datatype,
                                               &req->recv.state, info);
                ucs_trace_req("release receive descriptor %p", rdesc);
                uct_iface_release_am_desc(rdesc);
                if (status != UCS_INPROGRESS) {
                    return status;
                }
            } else if (rdesc->flags & UCP_RECV_DESC_FLAG_RNDV) {
                *save_rreq = 0;
                req->recv.buffer   = buffer;
                req->recv.count    = count;
                req->recv.datatype = datatype;
                ucp_rndv_matched(worker, req, (void*)(rdesc + 1));
                uct_iface_release_am_desc(rdesc);
                return UCS_INPROGRESS;
            }
        }
    }

    return UCS_INPROGRESS;
}

static inline ucp_request_t*
ucp_tag_recv_request_get(ucp_worker_h worker, void* buffer, size_t count,
                         ucp_datatype_t datatype)
{
    ucp_dt_generic_t *dt_gen;
    ucp_request_t *req;

    req = ucp_request_get(worker);
    if (req == NULL) {
        return NULL;
    }

    req->flags             = UCP_REQUEST_FLAG_EXPECTED;
    req->recv.state.offset = 0;
    if ((datatype & UCP_DATATYPE_CLASS_MASK) == UCP_DATATYPE_GENERIC) {
        dt_gen = ucp_dt_generic(datatype);
        req->recv.state.dt.generic.state = dt_gen->ops.start_unpack(dt_gen->context,
                                                                    buffer, count);
        ucs_debug("req %p buffer %p count %zu dt_gen state=%p", req, buffer, count,
                  req->recv.state.dt.generic.state);
    }
    if (ucs_log_enabled(UCS_LOG_LEVEL_TRACE_REQ)) {
        req->recv.info.sender_tag = 0;
    }

    return req;
}

static UCS_F_ALWAYS_INLINE void
ucp_tag_recv_request_completed(ucp_request_t *req, ucp_tag_recv_callback_t cb,
                               ucs_status_t status, ucp_tag_recv_info_t *info,
                               const char *function)
{
    ucs_trace_req("%s returning completed request %p (%p) stag 0x%"PRIx64" len %zu, %s",
                  function, req, req + 1, info->sender_tag, info->length,
                  ucs_status_string(status));
    cb(req + 1, status, &req->recv.info);

    UCS_INSTRUMENT_RECORD(UCS_INSTRUMENT_TYPE_UCP_RX,
                          "ucp_tag_recv_request_completed",
                          req, info->length);
    ucp_request_put(req);
}

ucs_status_ptr_t ucp_tag_recv_nb(ucp_worker_h worker, void *buffer, size_t count,
                                 uintptr_t datatype, ucp_tag_t tag, ucp_tag_t tag_mask,
                                 ucp_tag_recv_callback_t cb)
{
    ucs_status_t status;
    ucp_request_t *req;
    unsigned save_rreq = 1;

    ucs_trace_req("recv_nb buffer %p count %zu tag %"PRIx64"/%"PRIx64, buffer,
                  count, tag, tag_mask);

    req = ucp_tag_recv_request_get(worker, buffer, count, datatype);
    if (req == NULL) {
        return UCS_STATUS_PTR(UCS_ERR_NO_MEMORY);
    }

    UCS_INSTRUMENT_RECORD(UCS_INSTRUMENT_TYPE_UCP_RX, "ucp_tag_recv_nb",
                          req, ucp_contig_dt_length(datatype, count));

    req->recv.cb = cb;

    /* First, search in unexpected list */
    status = ucp_tag_search_unexp(worker, buffer, count, datatype, tag,
                                  tag_mask, req, &req->recv.info, &save_rreq);
    if (status != UCS_INPROGRESS) {
        ucp_tag_recv_request_completed(req, cb, status, &req->recv.info,
                                       "recv_nb");
    } else if (save_rreq) {
        /* If not found on unexpected, wait until it arrives.
         * If was found but need this receive request for later completion, save it */
        req->recv.buffer   = buffer;
        req->recv.count    = count;
        req->recv.datatype = datatype;
        req->recv.tag      = tag;
        req->recv.tag_mask = tag_mask;
        ucs_queue_push(&worker->context->tag.expected, &req->recv.queue);
        ucs_trace_req("recv_nb returning expected request %p (%p)", req, req + 1);
    }

    return req + 1;
}

ucs_status_ptr_t ucp_tag_msg_recv_nb(ucp_worker_h worker, void *buffer,
                                     size_t count, ucp_datatype_t datatype,
                                     ucp_tag_message_h message,
                                     ucp_tag_recv_callback_t cb)
{
    ucp_recv_desc_t *rdesc = message;
    ucs_status_t status;
    ucp_request_t *req;
    ucp_tag_t tag;
    unsigned save_rreq = 1;

    ucs_trace_req("msg_recv_nb buffer %p count %zu message %p", buffer, count,
                  message);

    req = ucp_tag_recv_request_get(worker, buffer, count, datatype);
    if (req == NULL) {
        return UCS_STATUS_PTR(UCS_ERR_NO_MEMORY);
    }

    UCS_INSTRUMENT_RECORD(UCS_INSTRUMENT_TYPE_UCP_RX, "ucp_tag_msg_recv_nb",
                          req, ucp_contig_dt_length(datatype, count));

    req->recv.cb       = cb;

    /* First, handle the first packet that was already matched */
    if (rdesc->flags & UCP_RECV_DESC_FLAG_EAGER) {
        tag = ((ucp_tag_hdr_t*)(rdesc + 1))->tag;
        status = ucp_eager_unexp_match(worker, rdesc, tag, rdesc->flags,
                                       buffer, count, datatype, &req->recv.state,
                                       &req->recv.info);
        ucs_trace_req("release receive descriptor %p", rdesc);
        uct_iface_release_am_desc(rdesc);
    } else if (rdesc->flags & UCP_RECV_DESC_FLAG_RNDV) {
        req->recv.buffer   = buffer;
        req->recv.count    = count;
        req->recv.datatype = datatype;
        ucp_rndv_matched(worker, req, (void*)(rdesc + 1));
        uct_iface_release_am_desc(rdesc);
        status = UCS_INPROGRESS;
        save_rreq = 0;
    } else {
        ucs_mpool_put(req);
        return UCS_STATUS_PTR(UCS_ERR_INVALID_PARAM);
    }

    /* Since the message contains only the first fragment, we might want
     * to receive additional fragments.
     */
    if (status == UCS_INPROGRESS) {
        status = ucp_tag_search_unexp(worker, buffer, count, datatype, 0,
                                      -1, req, &req->recv.info, &save_rreq);
    }

    if (status != UCS_INPROGRESS) {
        ucp_tag_recv_request_completed(req, cb, status, &req->recv.info,
                                       "msg_recv_nb");
    } else if (save_rreq) {
        ucs_trace_req("msg_recv_nb returning inprogress request %p (%p)", req, req + 1);
        /* For eager - need to put the recv_req in expected since more packets
         * will follow. For rndv - don't need to keep the recv_req as the match
         * to the rts already happened. */
        req->recv.buffer   = buffer;
        req->recv.count    = count;
        req->recv.datatype = datatype;
        ucs_queue_push(&worker->context->tag.expected, &req->recv.queue);
    }
    return req + 1;
}

void ucp_tag_cancel_expected(ucp_context_h context, ucp_request_t *req)
{
    ucs_queue_iter_t iter;
    ucp_request_t *qreq;

    ucs_queue_for_each_safe(qreq, iter, &context->tag.expected, recv.queue) {
        if (qreq == req) {
            ucs_queue_del_iter(&context->tag.expected, iter);
            UCS_INSTRUMENT_RECORD(UCS_INSTRUMENT_TYPE_UCP_RX,
                                  "ucp_tag_cancel_expected",
                                  req, 0);
            return;
        }
    }

    ucs_bug("expected request not found");
}
