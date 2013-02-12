#pragma once

//#define USE_IPV6

#ifdef MYDLL_EXPORTS
#define MYDLL_API extern "C" __declspec(dllexport)
#else
#define MYDLL_API extern "C" __declspec( dllimport )
#endif

#ifndef UNICODE
#define UNICODE
#endif

#include <ws2tcpip.h>
#include <mswsock.h>

// Unified socket address.
union usa {
	struct sockaddr sa;
	struct sockaddr_in sin;
#if defined(USE_IPV6)
	struct sockaddr_in6 sin6;
#else

#endif
};

// Describes listening socket, or socket which was accept()-ed
struct socket {
	SOCKET sock;          // Listening socket
	union usa lsa;        // Local socket address
	union usa rsa;        // Remote socket address
};

struct _connection {
	struct socket client;
	BYTE  *buffer;
	u_long bytes_transferred;
	void *user_data;
};

typedef struct _connection connection;

// Callback functions.
typedef BOOL (*ON_CONNECT_PROC)( connection *connection );
typedef void (*ON_DISCONNECT_PROC)( connection *connection );
typedef BOOL (*ON_RECV_PROC)( connection *connection);
typedef BOOL (*ON_SEND_PROC)( connection *connection);
typedef BOOL (*ON_SENDFILE_PROC)( connection *connection);
typedef void (*ON_ERROR_PROC)( wchar_t *str_error);
typedef void (*USERMESSAGE_HANDLER_PROC)(connection *connection, void *message);
typedef void ( *ENUM_CONNECTIONS_PROC)(void *connection, void *param);

typedef struct _qs_params {
	struct _listener {
		char *listen_adr;
		u_long init_accepts_count;
	} listener;

	unsigned int worker_threads_count;
	u_long connection_buffer_size;
	u_long keep_alive_time;
	u_long keep_alive_interval;
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

typedef struct _qs_info {
	volatile u_long sockets_count;
	volatile u_long active_connections_count;
} qs_info;

// Server functions.
MYDLL_API u_long  qs_create(void **qs_instance );
MYDLL_API void		     qs_delete(void *qs_instance );
MYDLL_API unsigned int   qs_start( void *qs_instance, qs_params * params );
MYDLL_API unsigned int   qs_stop( void *qs_instance );
MYDLL_API unsigned int   qs_send(connection *connection, BYTE *buffer, u_long len);
MYDLL_API unsigned int   qs_send_file( void *qs_instance, connection *connection, HANDLE file);
MYDLL_API unsigned int   qs_recv(connection *connection, BYTE *buffer, u_long len);
MYDLL_API unsigned int   qs_close_connection( void *qs_instance, connection *connection );
MYDLL_API unsigned int   qs_post_message_to_pool(void *qs_instance, void *message, connection *connection);
MYDLL_API unsigned int   qs_query_qs_information( void *qs_instance, qs_info *qs_information );
MYDLL_API unsigned int   qs_enum_connections( void *qs_instance, ENUM_CONNECTIONS_PROC enum_connections_proc, void *param);
MYDLL_API void			 sockaddr_to_string(char *buf, size_t len, const union usa *usa) ;
