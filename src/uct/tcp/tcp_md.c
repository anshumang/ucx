/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "tcp.h"


static ucs_status_t uct_tcp_md_query(uct_md_h md, uct_md_attr_t *attr)
{
    attr->cap.flags         = UCT_MD_FLAG_SOCKADDR;
    attr->cap.max_alloc     = 0;
    attr->cap.reg_mem_types = 0;
    attr->cap.mem_type      = 0;
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
    return uct_single_md_resource(&uct_tcp_md, resources_p, num_resources_p);
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
    int server_sock = 0;
    int is_accessible = 0;
    fprintf(stderr, "uct_tcp_is_sockaddr_accessible entry...\n");
    if ((mode != UCT_SOCKADDR_ACC_LOCAL) && (mode != UCT_SOCKADDR_ACC_REMOTE)) {
        ucs_error("Unknown sockaddr accessibility mode %d", mode);
        return UCS_ERR_UNSUPPORTED;
    }    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0){
        ucs_error("Could not create socket\n");
        return UCS_ERR_UNSUPPORTED;
    }    
    if (mode == UCT_SOCKADDR_ACC_LOCAL) {
        /* Server side to check if can bind to the given sockaddr */
        if (bind(server_sock, (struct sockaddr *) sockaddr->addr,
                 sizeof(*((struct sockaddr *) sockaddr->addr)))) {
            ucs_error("Could not bind socket\n");
            goto out_close_socket;
        }
        if (uct_tcp_is_sockaddr_inaddr_any((struct sockaddr *)sockaddr->addr)) {
            is_accessible = 1;
            goto out;
        }
    }
out_close_socket:
    close(server_sock);
out:
    return is_accessible;
}

static ucs_status_t uct_tcp_md_open(const char *md_name, const uct_md_config_t *md_config,
                                    uct_md_h *md_p)
{
    static uct_md_ops_t md_ops = {
        .close        = ucs_empty_function,
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
}

UCT_MD_COMPONENT_DEFINE(uct_tcp_md, UCT_TCP_NAME,
                        uct_tcp_query_md_resources, uct_tcp_md_open, NULL,
                        ucs_empty_function_return_unsupported,
                        ucs_empty_function_return_success, "TCP_",
                        uct_md_config_table, uct_md_config_t);
