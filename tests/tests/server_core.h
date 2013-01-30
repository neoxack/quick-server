#pragma once

#ifndef UNICODE
#define UNICODE
#endif

//#define USE_IPV6

#include <ws2tcpip.h>
#include <mswsock.h>

#pragma comment(lib, "ws2_32.lib")


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

typedef enum _server_status {
	not_runned,
	runned
} server_status;

struct _connection {
	struct socket client;
    BYTE  *buffer;
	unsigned long bytes_transferred;
	void *user_data;
};


struct _io_context;

struct _extend_connection {
	struct _connection connection;
	struct _io_context *io_context;
};

struct _io_context {
	OVERLAPPED ov;
	states ended_operation;
	struct _extend_connection connection;
	WSABUF wsa_buf;
	time_t last_activity;
};

typedef struct _connection connection;
typedef struct _io_context io_context;
typedef struct _extend_connection extend_connection;

typedef struct _server_info {
	volatile long opened_sockets_count;
} server_info;

void sockaddr_to_string(char *buf, size_t len, union usa *usa) ;

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

typedef struct _server_params {
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
} server_params;

// 
// Server functions.
//
unsigned long  server_create(void **server_instance );
void		   server_delete(void *server_instance );
//unsigned long  server_add_listener( void * server_instance, PLISTENER pParam, void *  user_data);
//unsigned long  server_remove_listener( void * server_instance, connection * pConnection );
//unsigned long  server_get_listeners(  void * server_instance, SOCKET **listeners);
unsigned int  server_start( void *server_instance, server_params * params );
unsigned int  server_stop( void *server_instance );
unsigned int  server_send(connection *connection, BYTE *buffer, unsigned long len);
unsigned int  server_send_file( void *server_instance, connection *connection, HANDLE file);
unsigned int  server_recv(connection *connection, BYTE *buffer, unsigned long len);
unsigned int  server_close_connection( void *server_instance, connection *connection );
unsigned int  server_post_message_to_pool(void *server_instance, void *message, connection *connection);
unsigned int  server_query_server_information( void *server_instance, server_info *server_information );

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

typedef struct _server_context {
	server_status status;
	server_info server_info;
	HANDLE iocp;
	connection_list* connections;
	struct socket server_socket;
	memory_manager *mem_manager;
	server_params server_params;
			
	struct _ex_funcs {
		LPFN_ACCEPTEX AcceptEx; 
		LPFN_TRANSMITPACKETS TransmitPackets; 
		LPFN_DISCONNECTEX DisconnectEx;
		LPFN_TRANSMITFILE TransmitFile;
	} ex_funcs;

} server_context;




memory_manager *memory_manager_create(size_t pre_alloc_count, size_t size_of_buffer);
io_context *memory_manager_alloc(memory_manager *manager);
void memory_manager_free(memory_manager *manager, io_context *io_context);
void memory_manager_destroy(memory_manager *manager);

connection_list* connection_list_new();
void connection_list_delete(connection_list* list);
void connection_list_add(connection_list* list, connection *con);
void connection_list_remove(connection_list* list, connection *con);
void clear_inactive_connections(server_context* context, int t);