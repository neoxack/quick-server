#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <ws2tcpip.h>
#include <mswsock.h>


// Unified socket address. For IPv6 support, add IPv6 address structure
// in the union u.
union usa {
  struct sockaddr sa;
  struct sockaddr_in sin;
#if defined(USE_IPV6)
  struct sockaddr_in6 sin6;
#endif
};

// Describes listening socket, or socket which was accept()-ed
struct socket {
  SOCKET sock;          // Listening socket
  union usa lsa;        // Local socket address
  union usa rsa;        // Remote socket address
 // unsigned is_ssl:1;    // Is port SSL-ed
 // unsigned ssl_redir:1; // Is port supposed to redirect everything to SSL port
};

typedef enum _states {
    send_done,
    recv_done,
	on_connect,
	on_disconnect,
	user_message,
	transmit_file,
	start_server,
	start_clean
} states;

typedef enum _qs_status {
	not_runned,
	runned
} qs_status;

struct _connection {
	struct socket client;
    BYTE  *buffer;
	unsigned long bytes_transferred;
	void *user_data;
};

struct _io_context {
	OVERLAPPED ov;
	struct _connection connection;
	states ended_operation;
	WSABUF wsa_buf;
	time_t last_activity;
};

typedef struct _connection connection;
typedef struct _io_context io_context;

typedef struct _qs_info {
	volatile long opened_sockets_count;
} qs_info;

void sockaddr_to_string(char *buf, size_t len, const union usa *usa) ;

// 
// Callback functions.
//
typedef BOOL (*ON_CONNECT_PROC)( connection *connection );
typedef void (*ON_DISCONNECT_PROC)( connection *connection );
//typedef void (*ON_REMOVE_CONNECTION_PROC)( connection *connection );
typedef BOOL (*ON_RECV_PROC)( connection *connection);
typedef BOOL (*ON_SEND_PROC)( connection *connection);
typedef BOOL (*ON_SENDFILE_PROC)( connection *connection);
typedef void (*ON_ERROR_PROC)( wchar_t *str_error, unsigned long error );
typedef void (*USERMESSAGE_HANDLER_PROC)(connection *connection, void *message);

typedef struct _qs_params {
	struct _listener {
	char *listen_adr;
	unsigned long init_accepts_count;
	} listener;

    unsigned int worker_threads_count;
    unsigned long connection_buffer_size;
    unsigned long keep_alive_time;
    unsigned long keep_alive_interval;
    unsigned int connections_idle_timeout;
    size_t expected_connections_amount;

    struct _callbacks {
        ON_CONNECT_PROC               on_connect;
        ON_DISCONNECT_PROC            on_disconnect;
        ON_SEND_PROC                  on_send;
		ON_SENDFILE_PROC              on_send_file;
        ON_RECV_PROC                  on_recv;
        ON_ERROR_PROC                 on_error;
		USERMESSAGE_HANDLER_PROC	  on_message;
        } callbacks;
} qs_params;

// 
// Server functions.
//
unsigned long  qs_create(void **qs_instance );
void		   qs_delete(void *qs_instance );
//unsigned long  qs_add_listener( void * qs_instance, PLISTENER pParam, void *  user_data);
//unsigned long  qs_remove_listener( void * qs_instance, connection * pConnection );
//unsigned long  qs_get_listeners(  void * qs_instance, SOCKET **listeners);
unsigned int   qs_start( void *qs_instance, qs_params * params );
unsigned int   qs_stop( void *qs_instance );
unsigned int   qs_send(connection *connection, BYTE *buffer, unsigned long len);
unsigned int   qs_send_file( void *qs_instance, connection *connection, HANDLE file);
unsigned int   qs_recv(connection *connection, BYTE *buffer, unsigned long len);
unsigned int   qs_close_connection( void *qs_instance, connection *connection );
unsigned int   qs_post_message_to_pool(void *qs_instance, void *message, connection *connection);
unsigned int   qs_query_qs_information( void *qs_instance, qs_info *qs_information );

#include "fast_buffer.h"

typedef struct _memory_manager {
	fast_buf *context_buf;
	fast_buf *buffer;
} memory_manager;

struct _node {
	connection *con;
	struct _node *next;
};

typedef struct _node  node;

typedef struct _connection_list {
	CRITICAL_SECTION cs;
	size_t count;
	node *head;
} connection_list;

typedef struct _qs_context {
	qs_status status;
	qs_info qs_info;
	HANDLE iocp;
	connection_list* connections;
	struct socket qs_socket;
	memory_manager *mem_manager;
	qs_params qs_params;
			
	struct _ex_funcs {
		LPFN_ACCEPTEX AcceptEx; 
		LPFN_TRANSMITPACKETS TransmitPackets; 
		LPFN_DISCONNECTEX DisconnectEx;
		LPFN_TRANSMITFILE TransmitFile;
	} ex_funcs;

} qs_context;


memory_manager *memory_manager_create(size_t pre_alloc_count, size_t size_of_buffer);
io_context *memory_manager_alloc(memory_manager *manager);
void memory_manager_free(memory_manager *manager, io_context *io_context);
void memory_manager_destroy(memory_manager *manager);

connection_list* connection_list_new();
void connection_list_delete(connection_list* list);
void connection_list_add(connection_list* list, connection *con);
void connection_list_remove(connection_list* list, connection *con);
void clear_inactive_connections(qs_context* context, time_t t);