/**
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 * See file LICENSE for terms.
 */

#include <ucs/sys/string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "tcp_cma.h"
#include "tcp.h"

struct tcp_cma_event {
	struct tcp_event	event;
	uint8_t			private_data[TCP_MAX_PRIVATE_DATA];
};

struct tcp_event_channel *tcp_create_event_channel(void)
{
	struct tcp_event_channel *channel;

	channel = malloc(sizeof(*channel));
	if (!channel)
		return NULL;

	return channel;
}

void tcp_destroy_event_channel(struct tcp_event_channel *channel)
{
	free(channel);
}

int tcp_create_id(struct tcp_event_channel *channel,
		   struct tcp_id **id, void *context,
		   enum tcp_port_space ps)
{
	*id = calloc(1, sizeof(struct tcp_id));
	if (!id)
		return UCS_ERR_NO_MEMORY;
	return 0;
}

int tcp_destroy_id(struct tcp_id *id)
{
	return 0;
}

int tcp_bind_addr(struct tcp_id *id, struct sockaddr *addr)
{
	return 0;
}

int tcp_resolve_addr(struct tcp_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms)
{
	return 0;
}

int tcp_resolve_route(struct tcp_id *id, int timeout_ms)
{
	return 0;
}

int tcp_connect(struct tcp_id *id, struct tcp_conn_param *conn_param)
{
        struct sockaddr *dest_addr = &(id->route.addr.saddr.src_addr);
        int ret = connect(id->channel->fd, dest_addr, sizeof(*dest_addr));
        if (ret < 0) {
                ucs_error("connect() failed: %m");
                return UCS_ERR_UNREACHABLE;
        }
	return UCS_OK;
}

int tcp_listen(struct tcp_id *id, int backlog)
{
	int ret = listen(id->channel->fd, backlog);
        if (ret < 0) {
                ucs_error("listen(backlog=%d)", backlog);
                return UCS_ERR_IO_ERROR;
        }
        return UCS_OK;
}

int tcp_accept(struct tcp_id *id, struct tcp_conn_param *conn_param)
{
        struct sockaddr *dest_addr = &(id->route.addr.saddr.src_addr);   
        socklen_t addrlen = sizeof(*dest_addr);

        int fd = accept(id->channel->fd, dest_addr, &addrlen);
        if (fd < 0) {
                if ((errno != EAGAIN) && (errno != EINTR)) {
                        ucs_error("accept() failed: %m");
                        if(id->channel->fd != -1){
                                close(id->channel->fd);
                                id->channel->fd = -1;
                        }
                }
                return UCS_ERR_IO_ERROR;
        }
        return UCS_OK;
}

int tcp_ack_cm_event(struct tcp_event *event)
{
	return 0;
}

int tcp_get_cm_event(struct tcp_event_channel *channel,
		      struct tcp_event **event)
{
	struct tcp_cma_event *evt;
	evt = malloc(sizeof(*evt));
	if (!evt){
                ucs_error("tcp_get_cm_event:malloc() failed: %m");
		return UCS_ERR_NO_MEMORY;
        }

	*event = &evt->event;
	return 0;
}

const char *tcp_event_str(enum tcp_event_type event)
{
	switch (event) {
	case TCP_CM_EVENT_ADDR_RESOLVED:
		return "TCP_CM_EVENT_ADDR_RESOLVED";
	case TCP_CM_EVENT_ADDR_ERROR:
		return "TCP_CM_EVENT_ADDR_ERROR";
	case TCP_CM_EVENT_ROUTE_RESOLVED:
		return "TCP_CM_EVENT_ROUTE_RESOLVED";
	case TCP_CM_EVENT_ROUTE_ERROR:
		return "TCP_CM_EVENT_ROUTE_ERROR";
	case TCP_CM_EVENT_CONNECT_REQUEST:
		return "TCP_CM_EVENT_CONNECT_REQUEST";
	case TCP_CM_EVENT_CONNECT_RESPONSE:
		return "TCP_CM_EVENT_CONNECT_RESPONSE";
	case TCP_CM_EVENT_CONNECT_ERROR:
		return "TCP_CM_EVENT_CONNECT_ERROR";
	case TCP_CM_EVENT_UNREACHABLE:
		return "TCP_CM_EVENT_UNREACHABLE";
	case TCP_CM_EVENT_REJECTED:
		return "TCP_CM_EVENT_REJECTED";
	case TCP_CM_EVENT_ESTABLISHED:
		return "TCP_CM_EVENT_ESTABLISHED";
	case TCP_CM_EVENT_DISCONNECTED:
		return "TCP_CM_EVENT_DISCONNECTED";
	case TCP_CM_EVENT_DEVICE_REMOVAL:
		return "TCP_CM_EVENT_DEVICE_REMOVAL";
	case TCP_CM_EVENT_MULTICAST_JOIN:
		return "TCP_CM_EVENT_MULTICAST_JOIN";
	case TCP_CM_EVENT_MULTICAST_ERROR:
		return "TCP_CM_EVENT_MULTICAST_ERROR";
	case TCP_CM_EVENT_ADDR_CHANGE:
		return "TCP_CM_EVENT_ADDR_CHANGE";
	case TCP_CM_EVENT_TIMEWAIT_EXIT:
		return "TCP_CM_EVENT_TIMEWAIT_EXIT";
	default:
		return "UNKNOWN EVENT";
	}
}

uint16_t utcp_get_port(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		return ((struct sockaddr_in *) addr)->sin_port;
	case AF_INET6:
		return ((struct sockaddr_in6 *) addr)->sin6_port;
	default:
		return 0;
	}
}

uint16_t tcp_get_src_port(struct tcp_id *id)
{
	return utcp_get_port(&id->route.addr.saddr.src_addr);
}

