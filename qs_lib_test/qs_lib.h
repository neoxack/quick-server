#pragma once


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

struct _connection {
	struct socket client;
	BYTE  *buffer;
	unsigned long bytes_transferred;
	void *user_data;
};

typedef struct _connection connection;

// 
// Callback functions.
//
typedef BOOL (CALLBACK *ON_CONNECT_PROC)( connection *connection );
typedef void (CALLBACK *ON_DISCONNECT_PROC)( connection *connection );
//typedef void (*ON_REMOVE_CONNECTION_PROC)( connection *connection );
typedef BOOL (CALLBACK *ON_RECV_PROC)( connection *connection);
typedef BOOL (CALLBACK *ON_SEND_PROC)( connection *connection);
typedef BOOL (CALLBACK *ON_SENDFILE_PROC)( connection *connection);
typedef void (CALLBACK *ON_ERROR_PROC)( wchar_t *str_error, unsigned long error );
typedef void (CALLBACK *USERMESSAGE_HANDLER_PROC)(connection *connection, void *message);

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

typedef struct _qs_info {
	volatile long opened_sockets_count;
} qs_info;



// 
// Server functions.
//
extern "C" {

MYDLL_API unsigned long  qs_create(void **qs_instance );
MYDLL_API void		     qs_delete(void *qs_instance );
MYDLL_API unsigned int   qs_start( void *qs_instance, qs_params * params );
MYDLL_API unsigned int   qs_stop( void *qs_instance );
MYDLL_API unsigned int   qs_send(connection *connection, BYTE *buffer, unsigned long len);
MYDLL_API unsigned int   qs_send_file( void *qs_instance, connection *connection, HANDLE file);
MYDLL_API unsigned int   qs_recv(connection *connection, BYTE *buffer, unsigned long len);
MYDLL_API unsigned int   qs_close_connection( void *qs_instance, connection *connection );
MYDLL_API unsigned int   qs_post_message_to_pool(void *qs_instance, void *message, connection *connection);
MYDLL_API unsigned int   qs_query_qs_information( void *qs_instance, qs_info *qs_information );
MYDLL_API void			 sockaddr_to_string(char *buf, size_t len, const union usa *usa) ;

};