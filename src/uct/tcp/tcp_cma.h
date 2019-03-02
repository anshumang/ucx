/**
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 * See file LICENSE for terms.
 */

#if !defined(TCP_CMA_H)
#define TCP_CMA_H

#include <netinet/in.h>
#include <sys/socket.h>

#define TCP_MAX_PRIVATE_DATA		256

/*
 * Upon receiving a device removal event, users must destroy the associated
 * TCP identifier and release all resources allocated with the device.
 */
enum tcp_event_type {
	TCP_CM_EVENT_ADDR_RESOLVED,
	TCP_CM_EVENT_ADDR_ERROR,
	TCP_CM_EVENT_ROUTE_RESOLVED,
	TCP_CM_EVENT_ROUTE_ERROR,
	TCP_CM_EVENT_CONNECT_REQUEST,
	TCP_CM_EVENT_CONNECT_RESPONSE,
	TCP_CM_EVENT_CONNECT_ERROR,
	TCP_CM_EVENT_UNREACHABLE,
	TCP_CM_EVENT_REJECTED,
	TCP_CM_EVENT_ESTABLISHED,
	TCP_CM_EVENT_DISCONNECTED,
	TCP_CM_EVENT_DEVICE_REMOVAL,
	TCP_CM_EVENT_MULTICAST_JOIN,
	TCP_CM_EVENT_MULTICAST_ERROR,
	TCP_CM_EVENT_ADDR_CHANGE,
	TCP_CM_EVENT_TIMEWAIT_EXIT
};

enum tcp_port_space {
	TCP_PS_TCP   = 0x0106,
	TCP_PS_UDP   = 0x0111,
};

#define TCP_IB_IP_PS_MASK   0xFFFFFFFFFFFF0000ULL
#define TCP_IB_IP_PORT_MASK 0x000000000000FFFFULL
#define TCP_IB_IP_PS_TCP    0x0000000001060000ULL
#define TCP_IB_IP_PS_UDP    0x0000000001110000ULL

struct tcp_addr {
	union {
		struct sockaddr		src_addr;
		struct sockaddr_in	src_sin;
		struct sockaddr_in6	src_sin6;
		struct sockaddr_storage src_storage;
	} saddr;
	union {
		struct sockaddr		dst_addr;
		struct sockaddr_in	dst_sin;
		struct sockaddr_in6	dst_sin6;
		struct sockaddr_storage dst_storage;
	} daddr;
};

struct tcp_route {
	struct tcp_addr	 addr;
	struct ibv_sa_path_rec	*path_rec;
	int			 num_paths;
};

struct tcp_event_channel {
	int			fd;
};

struct tcp_id {
	struct tcp_event_channel *channel;
	void			*context;
	struct tcp_route	 route;
	enum tcp_port_space	 ps;
	uint8_t			 port_num;
	struct tcp_event	*event;
};

struct tcp_conn_param {
	const void *private_data;
	uint8_t private_data_len;
	uint8_t responder_resources;
	uint8_t initiator_depth;
	uint8_t flow_control;
	uint8_t retry_count;		/* ignored when accepting */
	uint8_t rnr_retry_count;
};

struct tcp_event {
	struct tcp_id	*id;
	struct tcp_id	*listen_id;
	enum tcp_event_type	 event;
	int			 status;
	union {
		struct tcp_conn_param conn;
	} param;
};

/**
 * tcp_create_event_channel - Open a channel used to report communication events.
 * Description:
 *   Asynchronous events are reported to users through event channels.  Each
 *   event channel maps to a file descriptor.
 * Notes:
 *   All created event channels must be destroyed by calling
 *   tcp_destroy_event_channel.  Users should call tcp_get_cm_event to
 *   retrieve events on an event channel.
 * See also:
 *   tcp_get_cm_event, tcp_destroy_event_channel
 */
struct tcp_event_channel *tcp_create_event_channel(void);

/**
 * tcp_destroy_event_channel - Close an event communication channel.
 * @channel: The communication channel to destroy.
 * Description:
 *   Release all resources associated with an event channel and closes the
 *   associated file descriptor.
 * Notes:
 *   All tcp_id's associated with the event channel must be destroyed,
 *   and all returned events must be acked before calling this function.
 * See also:
 *  tcp_create_event_channel, tcp_get_cm_event, tcp_ack_cm_event
 */
void tcp_destroy_event_channel(struct tcp_event_channel *channel);

/**
 * tcp_create_id - Allocate a communication identifier.
 * @channel: The communication channel that events associated with the
 *   allocated tcp_id will be reported on.
 * @id: A reference where the allocated communication identifier will be
 *   returned.
 * @context: User specified context associated with the tcp_id.
 * @ps: RDMA port space.
 * Description:
 *   Creates an identifier that is used to track communication information.
 * Notes:
 *   tcp_id's are equivalent to a socket.
 *   Communication events on an tcp_id are 
 * reported through the associated event
 *   channel.  Users must release the tcp_id by calling tcp_destroy_id.
 * See also:
 *   tcp_create_event_channel, tcp_destroy_id, tcp_get_devices,
 *   tcp_bind_addr, tcp_resolve_addr, tcp_connect, tcp_listen,
 */
int tcp_create_id(struct tcp_event_channel *channel,
		   struct tcp_id **id, void *context,
		   enum tcp_port_space ps);

/**
 * tcp_destroy_id - Release a communication identifier.
 * @id: The communication identifier to destroy.
 * Description:
 *   Destroys the specified tcp_id and cancels any outstanding
 *   asynchronous operation.
 * Notes:
 *   Users must free any associated QP with the tcp_id before
 *   calling this routine and ack an related events.
 * See also:
 *   tcp_create_id, tcp_destroy_qp, tcp_ack_cm_event
 */
int tcp_destroy_id(struct tcp_id *id);

/**
 * tcp_bind_addr - Bind an TCP identifier to a source address.
 * @id: TCP identifier.
 * @addr: Local address information.  Wildcard values are permitted.
 * Description:
 *   Associates a source address with an tcp_id.  The address may be
 *   wildcarded.  If binding to a specific local address, the tcp_id
 *   will also be bound to a local TCP device.
 * Notes:
 *   Typically, this routine is called before calling tcp_listen to bind
 *   to a specific port number, but it may also be called on the active side
 *   of a connection before calling tcp_resolve_addr to bind to a specific
 *   address.
 * See also:
 *   tcp_create_id, tcp_listen, tcp_resolve_addr
 */
int tcp_bind_addr(struct tcp_id *id, struct sockaddr *addr);

/**
 * tcp_resolve_addr - Resolve destination and optional source addresses.
 * @id: TCP identifier.
 * @src_addr: Source address information.  This parameter may be NULL.
 * @dst_addr: Destination address information.
 * @timeout_ms: Time to wait for resolution to complete.
 * Description:
 *   Resolve destination and optional source addresses from IP addresses
 *   to ??? (an RDMA address).  If successful, the specified tcp_id will
 *   be bound to ???(a local device).
 * Notes:
 *   This call is used to map a given destination IP address to a ??? (usable RDMA
 *   address).  If a source address is given, the tcp_id is bound to that
 *   address, the same as if tcp_bind_addr were called.  If no source
 *   address is given, and the tcp_id has not yet been bound to a device,
 *   then the tcp_id will be bound to a source address based on the
 *   local routing tables.  After this call, the tcp_id will be bound to
 *   ??? (an RDMA device).  This call is typically made from the active side of a
 *   connection before calling tcp_resolve_route and tcp_connect.
 * See also:
 *   tcp_create_id, tcp_resolve_route, tcp_connect,
 *   tcp_get_cm_event, tcp_bind_addr
 */
int tcp_resolve_addr(struct tcp_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms);

/**
 * tcp_resolve_route - Resolve the route information needed to establish a connection.
 * @id: TCP identifier.
 * @timeout_ms: Time to wait for resolution to complete.
 * Description:
 *   Resolves an TCP route to the destination address in order to establish
 *   a connection.  The destination address must have already been resolved
 *   by calling tcp_resolve_addr.
 * Notes:
 *   This is called on the client side of a connection after calling
 *   tcp_resolve_addr, but before calling tcp_connect.
 * See also:
 *   tcp_resolve_addr, tcp_connect, tcp_get_cm_event
 */
int tcp_resolve_route(struct tcp_id *id, int timeout_ms);

/**
 * rdma_connect - Initiate an active connection request.
 * @id: TCP identifier.
 * @conn_param: optional connection parameters.
 * Description:
 *   For a connected tcp_id, this call initiates a connection request
 *   to a remote destination.
 * Notes:
 *   Users must have resolved a route to the destination address
 *   by having called tcp_resolve_route before calling this routine.
 *   A user may override the default connection parameters and exchange
 *   private data as part of the connection by using the conn_param parameter.
 * See also:
 *   tcp_resolve_route, tcp_listen, tcp_get_cm_event
 */
int tcp_connect(struct tcp_id *id, struct tcp_conn_param *conn_param);

/**
 * tcp_listen - Listen for incoming connection requests.
 * @id: RDMA identifier.
 * @backlog: backlog of incoming connection requests.
 * Description:
 *   Initiates a listen for incoming connection requests or datagram service
 *   lookup.  The listen will be restricted to the locally bound source
 *   address.
 * Notes:
 *   Users must have bound the tcp_id to a local address by calling
 *   tcp_bind_addr before calling this routine.  If the tcp_id is
 *   bound to a specific IP address, the listen will be restricted to that
 *   address.  If the tcp_id is bound
 *   to a TCP port number only, the listen will occur across all ??? (RDMA devices).
 * See also:
 *   tcp_bind_addr, tcp_connect, tcp_accept, tcp_get_cm_event
 */
int tcp_listen(struct tcp_id *id, int backlog);

/**
 * tcp_accept - Called to accept a connection request.
 * @id: Connection identifier associated with the request.
 * @conn_param: Optional information needed to establish the connection.
 * Description:
 *   Called from the listening side to accept a connection or datagram
 *   service lookup request.
 * Notes:
 *   Unlike the socket accept routine, tcp_accept is not called on a
 *   listening tcp_id.  Instead, after calling tcp_listen, the user
 *   waits for a connection request event to occur.  Connection request
 *   events give the user a newly created tcp_id, similar to a new
 *   socket.
 *   tcp_accept is called on the new tcp_id.
 *   A user may override the default connection parameters and exchange
 *   private data as part of the connection by using the conn_param parameter.
 * See also:
 *   tcp_listen, tcp_reject, tcp_get_cm_event
 */
int tcp_accept(struct tcp_id *id, struct tcp_conn_param *conn_param);

/**
 * tcp_get_cm_event - Retrieves the next pending communication event.
 * @channel: Event channel to check for events.
 * @event: Allocated information about the next communication event.
 * Description:
 *   Retrieves a communication event.  If no events are pending, by default,
 *   the call will block until an event is received.
 * Notes:
 *   The default synchronous behavior of this routine can be changed by
 *   modifying the file descriptor associated with the given channel.  All
 *   events that are reported must be acknowledged by calling tcp_ack_cm_event.
 *   Destruction of an tcp_id will block until related events have been
 *   acknowledged.
 * See also:
 *   tcp_ack_cm_event, tcp_create_event_channel, tcp_event_str
 */
int tcp_get_cm_event(struct tcp_event_channel *channel,
		      struct tcp_event **event);

/**
 * tcp_ack_cm_event - Free a communication event.
 * @event: Event to be released.
 * Description:
 *   All events which are allocated by tcp_get_cm_event must be released,
 *   there should be a one-to-one correspondence between successful gets
 *   and acks.
 * See also:
 *   tcp_get_cm_event, tcp_destroy_id
 */
int tcp_ack_cm_event(struct tcp_event *event);

uint16_t tcp_get_src_port(struct tcp_id *id);
/**
 * tcp_event_str - Returns a string representation of an tcpcm event.
 * @event: Asynchronous event.
 * Description:
 *   Returns a string representation of an asynchronous event.
 * See also:
 *   rdma_get_cm_event
 */
const char *tcp_event_str(enum tcp_event_type event);

#endif /* TCP_CMA_H */
