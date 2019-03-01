/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "tcp.h"

static ucs_config_field_t uct_tcp_md_config_table[] = {
  {"", "", NULL,
   ucs_offsetof(uct_tcp_md_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_md_config_table)},

  {"ADDR_RESOLVE_TIMEOUT", "500ms",
   "Time to wait for address resolution to complete",
    ucs_offsetof(uct_tcp_md_config_t, addr_resolve_timeout), UCS_CONFIG_TYPE_TIME},

  {NULL}
};


static ucs_status_t uct_tcp_md_query(uct_md_h md, uct_md_attr_t *attr)
{
    attr->cap.flags         = UCT_MD_FLAG_SOCKADDR;
    attr->cap.max_alloc     = 0;
    attr->cap.reg_mem_types = 0;
    attr->cap.mem_type      = UCT_MD_FLAG_SOCKADDR;
    attr->cap.max_reg       = 0;
    attr->rkey_packed_size  = 0;
    attr->reg_cost.overhead = 0;
    attr->reg_cost.growth   = 0;
    memset(&attr->local_cpus, 0xff, sizeof(attr->local_cpus));
    return UCS_OK;
}

static ucs_status_t uct_tcp_query_md_resources(uct_md_resource_desc_t **resources_p,
                                                unsigned *num_resources_p)
{
    struct tcp_event_channel *event_ch = NULL;

    /* Create a dummy event channel to check if TCPCM can be used */
    event_ch = tcp_create_event_channel();
    if (event_ch == NULL) {
        ucs_debug("could not create an TCPCM event channel. %m. "
                  "Disabling the TCPCM resource");
        *resources_p     = NULL;
        *num_resources_p = 0;
        return UCS_OK;
    }

    tcp_destroy_event_channel(event_ch);

    return uct_single_md_resource(&uct_tcp_md, resources_p, num_resources_p);
}

static void uct_tcp_md_close(uct_md_h md);

static uct_md_ops_t uct_tcp_md_ops = {
    .close        = uct_tcp_md_close,
    .query        = uct_tcp_md_query,
    .mkey_pack    = ucs_empty_function_return_unsupported,
    .mem_reg      = ucs_empty_function_return_unsupported,
    .mem_dereg    = ucs_empty_function_return_unsupported,
    .is_sockaddr_accessible = uct_tcp_is_sockaddr_accessible,
    .is_mem_type_owned = (void *)ucs_empty_function_return_zero,
};

/*static ucs_status_t uct_tcp_md_open(const char *md_name, const uct_md_config_t *md_config,
                                    uct_md_h *md_p)
{
    static uct_md_ops_t md_ops = {
        .close        = uct_tcp_md_close,
        .query        = uct_tcp_md_query,
        .mkey_pack    = ucs_empty_function_return_unsupported,
        .mem_reg      = ucs_empty_function_return_unsupported,
        .mem_dereg    = ucs_empty_function_return_unsupported,
        .is_sockaddr_accessible = uct_tcp_is_sockaddr_accessible,
        .is_mem_type_owned = (void *)ucs_empty_function_return_zero,
    };
    static uct_md_t md = {
        .ops          = &md_ops,
        .component    = &uct_tcp_md
    };

    *md_p = &md;
    return UCS_OK;
}*/

static void uct_tcp_md_close(uct_md_h md)
{
    uct_tcp_md_t *tcpcm_md = ucs_derived_of(md, uct_tcp_md_t);
    ucs_free(tcpcm_md);
}

static int uct_tcp_get_event_type(struct tcp_event_channel *event_ch) /*XXX rdma_event_channel in librdma*/
{
    struct tcp_cm_event *event;
    int ret, event_type;

    /* Fetch an event */
    ret = tcp_get_cm_event(event_ch, &event); /*XXX rdma_get_cm_event in librdma*/
    if (ret) {
        ucs_warn("tcp_get_cm_event() failed: %m");
        return 0;
    }

    event_type = event->event;
    ret = tcp_ack_cm_event(event); /*XXX rdma_ack_cm_event in librdma*/
    if (ret) {
        ucs_warn("rdma_ack_cm_event() failed. event status: %d. %m.", event->status);
    }

    return event_type;
}

static int uct_tcp_is_addr_route_resolved(struct tcp_id *cm_id, /*XXX struct rdma_cm_id defined in librdma*/
                                             struct sockaddr *addr,
                                             int timeout_ms)
{
    char ip_port_str[UCS_SOCKADDR_STRING_LEN];
    ucs_status_t status;
    int event_type;

    status = uct_tcp_resolve_addr(cm_id, addr, timeout_ms, UCS_LOG_LEVEL_DEBUG);
    if (status != UCS_OK) {
        return 0;
    }

    event_type = uct_tcp_get_event_type(cm_id->channel);
    if (event_type != TCP_CM_EVENT_ADDR_RESOLVED) { /*XXX RDMA_CM_EVENT_ADDR_RESOLVED in librdma*/
        ucs_debug("failed to resolve address (addr = %s). TCPCM event %s.",
                  ucs_sockaddr_str(addr, ip_port_str, UCS_SOCKADDR_STRING_LEN),
                  tcp_event_str(event_type)); /*XXX tcp_event_str in librdma*/
        return 0;
    }

    if (cm_id->verbs->device->transport_type == IBV_TRANSPORT_IWARP) {
        ucs_debug("%s: iWarp support is not implemented",
                  ucs_sockaddr_str(addr, ip_port_str, UCS_SOCKADDR_STRING_LEN));
        return 0;
    }

    if (tcp_resolve_route(cm_id, timeout_ms)) { /*XXX rdma_resolve_route in librdma*/
        ucs_debug("rdma_resolve_route(addr = %s) failed: %m",
                   ucs_sockaddr_str(addr, ip_port_str, UCS_SOCKADDR_STRING_LEN));
        return 0;
    }

    event_type = uct_tcp_get_event_type(cm_id->channel);
    if (event_type != TCP_CM_EVENT_ROUTE_RESOLVED) { /*XXX RDMA_CM_EVENT_ROUTE_RESOLVED in librdma*/
        ucs_debug("failed to resolve route to addr = %s. TCPCM event %s.",
                  ucs_sockaddr_str(addr, ip_port_str, UCS_SOCKADDR_STRING_LEN),
                  tcp_event_str(event_type));
        return 0;
    }

    return 1;
}

static int uct_tcp_is_sockaddr_inaddr_any(struct sockaddr *addr)
{
    struct sockaddr_in6 *addr_in6;
    struct sockaddr_in *addr_in;

    switch (addr->sa_family) {
    case AF_INET:
        addr_in = (struct sockaddr_in *)addr;
        return addr_in->sin_addr.s_addr == INADDR_ANY;
    case AF_INET6:
        addr_in6 = (struct sockaddr_in6 *)addr;
        return !memcmp(&addr_in6->sin6_addr, &in6addr_any, sizeof(addr_in6->sin6_addr));
    default:
        ucs_debug("Invalid address family: %d", addr->sa_family);
    }

    return 0;
}

int uct_tcp_is_sockaddr_accessible(uct_md_h md, const ucs_sock_addr_t *sockaddr,
                                      uct_sockaddr_accessibility_t mode)
{
    uct_tcp_md_t *tcpcm_md = ucs_derived_of(md, uct_tcp_md_t);
    struct tcp_event_channel *event_ch = NULL;
    struct tcp_cm_id *cm_id = NULL;
    int is_accessible = 0;
    char ip_port_str[UCS_SOCKADDR_STRING_LEN];

    if ((mode != UCT_SOCKADDR_ACC_LOCAL) && (mode != UCT_SOCKADDR_ACC_REMOTE)) {
        ucs_error("Unknown sockaddr accessibility mode %d", mode);
        return 0;
    }

    event_ch = tcp_create_event_channel(); /*XXX rdma_create_event_channel in librdma*/
    if (event_ch == NULL) {
        ucs_error("tcp_create_event_channel() failed: %m");
        goto out;
    }

    if (tcp_create_id(event_ch, &cm_id, NULL, TCP_PS_UDP)) { /*XXX rdma_create_id RDMA_PS_UDP in librdma*/
        ucs_error("tcp_create_id() failed: %m");
        goto out_destroy_event_channel;
    }

    if (mode == UCT_SOCKADDR_ACC_LOCAL) {
        /* Server side to check if can bind to the given sockaddr */
        if (tcp_bind_addr(cm_id, (struct sockaddr *)sockaddr->addr)) { /*XXX rdma_bind_addr in librdma*/
            ucs_debug("rdma_bind_addr(addr = %s) failed: %m",
                      ucs_sockaddr_str((struct sockaddr *)sockaddr->addr,
                                       ip_port_str, UCS_SOCKADDR_STRING_LEN));
            goto out_destroy_id;
        }

        if (uct_tcp_is_sockaddr_inaddr_any((struct sockaddr *)sockaddr->addr)) {
            is_accessible = 1;
            goto out_print;
        }
    }

    /* Client and server sides check if can access the given sockaddr.
     * The timeout needs to be passed in ms */
    is_accessible = uct_tcp_is_addr_route_resolved(cm_id,
                                                     (struct sockaddr *)sockaddr->addr,
                                                     UCS_MSEC_PER_SEC * rdmacm_md->addr_resolve_timeout);
    if (!is_accessible) {
        goto out_destroy_id;
    }

out_print:
    ucs_debug("address %s (port %d) is accessible from tcpcm_md %p with mode: %d",
              ucs_sockaddr_str((struct sockaddr *)sockaddr->addr, ip_port_str,
                               UCS_SOCKADDR_STRING_LEN),
              ntohs(tcp_get_src_port(cm_id)), tcpcmcm_md, mode); /*XXX rdma_get_src_port in librdma*/

out_destroy_id:
    tcp_destroy_id(cm_id); /*XXX rdma_destroy_id in librdma*/
out_destroy_event_channel:
    tcp_destroy_event_channel(event_ch); /*XXX rdma_destroy_event_channel in librdma*/
out:
    return is_accessible;
}

static ucs_status_t
uct_tcp_md_open(const char *md_name, const uct_md_config_t *uct_md_config,
                   uct_md_h *md_p)
{
    uct_tcp_md_config_t *md_config = ucs_derived_of(uct_md_config, uct_tcp_md_config_t);
    uct_tcp_md_t *md;
    ucs_status_t status;

    md = ucs_malloc(sizeof(*md), "tcpcm_md");
    if (md == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto out;
    }

    md->super.ops            = &uct_tcp_md_ops;
    md->super.component      = &uct_tcp_md;
    md->addr_resolve_timeout = md_config->addr_resolve_timeout;

    *md_p = &md->super;
    status = UCS_OK;

out:
    return status;
}

UCT_MD_COMPONENT_DEFINE(uct_tcp_md, UCT_TCP_NAME,
                        uct_tcp_query_md_resources, uct_tcp_md_open, NULL,
                        ucs_empty_function_return_unsupported,
                        ucs_empty_function_return_success, "TCP_",
                        uct_tcp_md_config_table, uct_tcp_md_config_t);
