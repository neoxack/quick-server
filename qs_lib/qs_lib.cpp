#include "qs_lib.h"

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS // Disable deprecation warning in VS
#endif

#if defined (_MSC_VER)
// non-constant aggregate initializer: issued due to missing C99 support
#pragma warning (disable : 4204)
#endif

// Disable WIN32_LEAN_AND_MEAN.
// This makes windows.h always include winsock2.h
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

#pragma comment(lib, "ws2_32.lib")

#include <process.h>
#include <stdio.h>
#include <time.h>
#include <Mstcpip.h>

#define BUF_LEN 512

#ifdef _MSC_VER
#define __func__ __FUNCTION__
#endif

#include "fast_buffer.h"

typedef struct _memory_manager {
	fast_buf *context_buf;
	fast_buf *buffer;
} memory_manager;

typedef enum _states {
	send_done,
	recv_done,
	on_connect,
	on_disconnect,
	user_message,
	transmit_file,
	start_server,
	start_clean,
	stop_server
} states;

typedef enum _qs_status {
	not_runned,
	runned
} qs_status;

struct _io_context {
	OVERLAPPED ov;
	struct _connection connection;
	states ended_operation;
	WSABUF wsa_buf;
	time_t last_activity;
};

typedef struct _io_context io_context;

typedef struct _qs_context {
	qs_status status;
	qs_info qs_info;
	HANDLE iocp;
	uintptr_t *threads;
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


// Print error message
static void cry(const char *fmt, ...) 
{
	char buf[BUF_LEN];
	va_list ap;
	time_t seconds;
	struct tm timeinfo;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	seconds = time(NULL);
	localtime_s(&timeinfo, &seconds);

	printf("[%02d:%02d:%02d %02d.%02d.%02d] %s\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year-100, buf);
}

__forceinline static io_context *get_context(connection *connection)
{
	ptrdiff_t  p = (ptrdiff_t)connection;
	return (io_context *)(p - sizeof(OVERLAPPED));
}

static uintptr_t create_thread(unsigned (__stdcall * start_addr) (void *), void * args, unsigned int stack_size)
{
	unsigned int threadID;
	uintptr_t thread = _beginthreadex(NULL, stack_size, start_addr, args, 0, &threadID );
	return thread;
}

memory_manager *memory_manager_create(size_t pre_alloc_count, size_t size_of_buffer)
{
	fast_buf *context_buf;
	fast_buf *buffer;
	memory_manager *res = (memory_manager *)malloc(sizeof(memory_manager));
	if(!res) return NULL;

	context_buf = fast_buf_create(sizeof(io_context), pre_alloc_count);
	if(!context_buf)
	{
		free(res);
		return NULL;
	}
	buffer = fast_buf_create(size_of_buffer, pre_alloc_count);
	if(!buffer)
	{
		free(context_buf);
		free(res);
		return NULL;
	}

	res->buffer = buffer;
	res->context_buf = context_buf;
	return res;
}

io_context *memory_manager_alloc(memory_manager *manager)
{
	io_context *io_cont = (io_context *)fast_buf_alloc(manager->context_buf);
	io_cont->connection.buffer = (BYTE *)fast_buf_alloc(manager->buffer);
	return io_cont;
}

void memory_manager_free(memory_manager *manager, io_context *io_context)
{
	fast_buf_free(manager->buffer, io_context->connection.buffer);
	fast_buf_free(manager->context_buf, io_context);
}

void memory_manager_destroy(memory_manager *manager)
{
	fast_buf_destroy(manager->buffer);
	fast_buf_destroy(manager->context_buf);
	free(manager);
}

__inline SOCKET socket_create(qs_info *info)
{
	SOCKET sock;
#if defined(USE_IPV6)
	sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
#else
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
	InterlockedIncrement(&info->sockets_count);
	return sock;
}

__inline void socket_close(SOCKET sock, qs_info *info)
{
	closesocket(sock);
	InterlockedDecrement(&info->sockets_count);
}

static BOOL init_ex_funcs(qs_context* server)
{
#if defined(USE_IPV6)
	SOCKET s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
#else
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif

	GUID accept_ex_GUID =        WSAID_ACCEPTEX; 
	GUID transmit_packets_GUID = WSAID_TRANSMITPACKETS; 
	GUID disconnect_ex_GUID =    WSAID_DISCONNECTEX;
	GUID transmitfile_GUID =     WSAID_TRANSMITFILE;
	unsigned long dwTmp;
	int res;

	if ( ( WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &accept_ex_GUID, sizeof(accept_ex_GUID), &server->ex_funcs.AcceptEx, sizeof(server->ex_funcs.AcceptEx), &dwTmp, NULL, NULL)!=0) 
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &transmit_packets_GUID, sizeof(transmit_packets_GUID), &server->ex_funcs.TransmitPackets, sizeof(server->ex_funcs.TransmitPackets), &dwTmp, NULL, NULL)!=0) 
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &disconnect_ex_GUID, sizeof(disconnect_ex_GUID), &server->ex_funcs.DisconnectEx, sizeof(server->ex_funcs.DisconnectEx), &dwTmp, NULL, NULL)!=0)
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &transmitfile_GUID, sizeof(transmitfile_GUID), &server->ex_funcs.TransmitFile, sizeof(server->ex_funcs.TransmitFile), &dwTmp, NULL, NULL)!=0)) 
		res = FALSE;
	res = TRUE;
	closesocket(s);
	return res;
}

MYDLL_API unsigned long qs_create(void **qs_instance )
{
	int result;
	int error;
	WSADATA wsaData;
	qs_context* server = (qs_context*)malloc(sizeof(qs_context));
	*qs_instance = server;

	if(*qs_instance)
	{
		memset(*qs_instance, 0, sizeof(qs_instance));

		result = WSAStartup(MAKEWORD(2,2), &wsaData);
		if (result != 0) 
		{
			cry("%s: WSAStartup() fail with error: %d",
				__func__, GetLastError());
			return GetLastError();
		}

		//server->connections = connection_list_new();

		server->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
		if(!server->iocp) 
		{	
			error = GetLastError();
			WSACleanup();
			cry("%s: CreateIoCompletionPort() fail with error: %d",	__func__, error);
			return error;
		}

		if(!init_ex_funcs(server)) 
		{	
			CloseHandle(server->iocp);
			error = WSAGetLastError();
			WSACleanup();
			cry("%s: init_ex_funcs() fail with error: %d",	__func__, error);
			return error;
		}

		return ERROR_SUCCESS;
	}
	else return ERROR_ALLOCATE_BUCKET;
}

MYDLL_API void qs_delete(void *qs_instance )
{
	free(qs_instance);
}

// Examples: 80, 127.0.0.1:3128
// TODO(lsm): add parsing of the IPv6 address
static int parse_port_string(const char *ptr, struct socket *so)
{
	int a, b, c, d, port, len;

	memset(so, 0, sizeof(*so));

	if (sscanf_s(ptr, "%d.%d.%d.%d:%d%n", &a, &b, &c, &d, &port, &len) == 5) 
	{
		// Bind to a specific IPv4 address
		so->lsa.sin.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
	} 
	else if (sscanf_s(ptr, "%d%n", &port, &len) != 1 || len <= 0)
	{
		return 0;
	}

#if defined(USE_IPV6)
	so->lsa.sin6.sin6_family = AF_INET6;
	so->lsa.sin6.sin6_port = htons((u_short) port);
#else
	so->lsa.sin.sin_family = AF_INET;
	so->lsa.sin.sin_port = htons((u_short)port);
#endif

	return 1;
}

#define THREAD_STACK_SIZE 256
unsigned __stdcall working_thread(void *s);

MYDLL_API unsigned int qs_start( void *qs_instance, qs_params * params )
{
	qs_context* server;
	unsigned int i;
	io_context *io_context;
	struct socket so;
	int on = 1;

	if(!qs_instance || !params) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;	
	if(server->status == runned) return ERROR_ALREADY_EXISTS;

	memcpy(&server->qs_params, params, sizeof(qs_params));

	if (!parse_port_string(params->listener.listen_adr, &so))
	{
		WSACleanup();
		cry("%s: invalid port spec.\nExpecting list of: %s",
			__func__, "[IP_ADDRESS:]PORT[s|p]");
	} 
	else if ((so.sock = socket(so.lsa.sa.sa_family, SOCK_STREAM, 6)) ==
		INVALID_SOCKET ||

		// Set TCP keep-alive. This is needed because if HTTP-level
		// keep-alive is enabled, and client resets the connection,
		// server won't get TCP FIN or RST and will keep the connection
		// open forever. With TCP keep-alive, next keep-alive
		// handshake will figure out that the client is down and
		// will close the server end.
		setsockopt(so.sock, SOL_SOCKET, SO_KEEPALIVE, (char *) &on,
		sizeof(on)) != 0 ||
		bind(so.sock, &so.lsa.sa, sizeof(so.lsa)) != 0 ||
		listen(so.sock, SOMAXCONN) != 0)
	{
		closesocket(so.sock);
		cry("%s: cannot bind to %s", __func__, params->listener.listen_adr);
	} 
	else
	{
		server->mem_manager = memory_manager_create(server->qs_params.expected_connections_amount,
			(size_t)server->qs_params.connection_buffer_size);

		if(!server->mem_manager)
		{
			closesocket(so.sock);
			cry("%s: memory_manager_create() fail",	__func__);
			return ERROR_ALLOCATE_BUCKET;
		}

		memcpy(&server->qs_socket, &so, sizeof(so));
		CreateIoCompletionPort((HANDLE)server->qs_socket.sock, server->iocp, server->qs_socket.sock, 0);

		server->qs_info.sockets_count = 0;

		server->threads = (uintptr_t *)malloc(sizeof(uintptr_t) * server->qs_params.worker_threads_count);

		for(i = 0; i<server->qs_params.worker_threads_count; ++i)
		{
			server->threads[i] = create_thread(working_thread, server, THREAD_STACK_SIZE);
		}
		//	create_thread(cleaner_thread, server, 512);

		io_context = memory_manager_alloc(server->mem_manager);
		io_context->ended_operation = start_server;

		PostQueuedCompletionStatus(server->iocp, 8, 0, (LPOVERLAPPED)io_context);
		server->status = runned;
		return ERROR_SUCCESS;
	}

	return GetLastError();
}


MYDLL_API unsigned int qs_stop( void *qs_instance )
{
	qs_context* server = (qs_context*)qs_instance;
	u_int i;

	for(i = 0; i<server->qs_params.worker_threads_count; i++)
	{
		io_context *io_context = memory_manager_alloc(server->mem_manager);
		io_context->ended_operation = stop_server;
		PostQueuedCompletionStatus(server->iocp, 8, 0, (LPOVERLAPPED)io_context);
	}

	WaitForMultipleObjects(server->qs_params.worker_threads_count, (HANDLE *)server->threads, TRUE, INFINITE);
	for(i = 0; i<server->qs_params.worker_threads_count; i++)
	{
		CloseHandle((HANDLE)server->threads[i]);
	}

	socket_close(server->qs_socket.sock, &server->qs_info);
	CloseHandle(server->iocp);
	memory_manager_destroy(server->mem_manager);
	free(server->threads);
	server->qs_info.sockets_count = 0;
	server->qs_info.active_connections_count = 0;
	server->status = not_runned;
	
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_send(connection *connection, BYTE *buffer, unsigned long len)
{
	io_context *context;
	int  res;
	int  error;
	unsigned long bytes_send;
	if(!connection || !buffer) return ERROR_INVALID_PARAMETER;
	context = get_context(connection);
	context->ended_operation = send_done;
	context->wsa_buf.len = len;
	context->wsa_buf.buf = (CHAR*)buffer;
	res = WSASend(connection->client.sock, &(context->wsa_buf), 1, &bytes_send, 0, (LPOVERLAPPED)context, 0);
	if ((res == SOCKET_ERROR) && (WSA_IO_PENDING != (error = WSAGetLastError())))
	{
		return error;
	}
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_send_file( void *qs_instance, connection *connection, HANDLE file)
{
	qs_context* server;	
	io_context *context;
	if(!qs_instance || !connection || file == INVALID_HANDLE_VALUE) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	context = get_context(connection);
	context->ended_operation = transmit_file;
	server->ex_funcs.TransmitFile(connection->client.sock, file, 0, 0, (LPOVERLAPPED)context, 0, TF_DISCONNECT | TF_USE_KERNEL_APC);	
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_recv(connection *connection, BYTE *buffer, unsigned long len)
{
	io_context *context;
	int res;
	int error;
	unsigned long bytes_recv;
	unsigned long flags = 0;
	if(!connection || !buffer) return ERROR_INVALID_PARAMETER;
	context = get_context(connection);
	context->ended_operation = recv_done;
	context->wsa_buf.len = len;
	context->wsa_buf.buf = (CHAR*)buffer;
	res = WSARecv(connection->client.sock, &(context->wsa_buf), 1, &bytes_recv, &flags, (LPOVERLAPPED)context, 0);
	if ((res == SOCKET_ERROR) && (WSA_IO_PENDING != (error = WSAGetLastError())))
	{
		return error;
	}
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_close_connection( void *qs_instance, connection *connection )
{
	qs_context* server;	
	io_context *context;
	if(!qs_instance || !connection) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	context = get_context(connection);
	context->ended_operation = on_disconnect;
	server->ex_funcs.DisconnectEx(connection->client.sock, (LPOVERLAPPED)context, 0, 0);
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_post_message_to_pool(void *qs_instance, void *message, connection *connection)
{
	qs_context* server;	
	io_context *context;
	if(!qs_instance || !connection) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	context = get_context(connection);
	context->ended_operation = user_message;
	PostQueuedCompletionStatus(server->iocp, 8, (uintptr_t)message, (LPOVERLAPPED)context);
	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_query_qs_information( void *qs_instance, qs_info *qs_information )
{
	qs_context *server;
	if(!qs_instance || !qs_information) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	memcpy(qs_information, &server->qs_info, sizeof(qs_info));
	return ERROR_SUCCESS;
}

MYDLL_API void sockaddr_to_string(char *buf, size_t len, const union usa *usa) 
{
	buf[0] = '\0';
#if defined(USE_IPV6)
	inet_ntop(usa->sa.sa_family, usa->sa.sa_family == AF_INET ?
		(void *) &usa->sin.sin_addr :
	(void *) &usa->sin6.sin6_addr, buf, len);
#elif defined(_WIN32)
	// Only Windoze Vista (and newer) have inet_ntop()
	strncpy(buf, inet_ntoa(usa->sin.sin_addr), len);
#else
	inet_ntop(usa->sa.sa_family, (void *) &usa->sin.sin_addr, buf, len);
#endif
}

static void init_accept(qs_context *server, BYTE *out_buf)
{
	SOCKET client;
	io_context *new_context;
	unsigned long bytes_transferred;
	new_context = memory_manager_alloc(server->mem_manager);
	if(new_context != NULL)
	{
		client = socket_create(&server->qs_info);
		new_context->ended_operation = on_connect;
		new_context->connection.client.sock = client;

		if(server->ex_funcs.AcceptEx(server->qs_socket.sock, new_context->connection.client.sock, out_buf, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16, 
			&bytes_transferred, (LPOVERLAPPED)new_context) != 0)
		{
			cry("%s: AcceptEx() fail with error: %d",	__func__, WSAGetLastError());
		}
	}
}

static void set_keep_alive(connection *con, u_long  keepalivetime, u_long keepaliveinterval)
{
	struct tcp_keepalive alive;
	u_long dwRet, dwSize;
	io_context *context;

	if(keepalivetime !=0 && keepaliveinterval!=0)
	{
		context = get_context(con);

		alive.onoff = 1;
		alive.keepalivetime = keepalivetime;
		alive.keepaliveinterval = keepaliveinterval;

		dwRet = WSAIoctl(con->client.sock, SIO_KEEPALIVE_VALS, &alive, sizeof(alive),
			NULL, 0, &dwSize, (LPOVERLAPPED)context, NULL);
		if (dwRet == SOCKET_ERROR)
		{
			cry("%s: WSAIoctl() fail with error: %d", __func__, WSAGetLastError());
		}
	}	
}


unsigned __stdcall working_thread(void *s) 
{
	qs_context *server = (qs_context *)s;
	unsigned long bytes_transferred;
	ULONG_PTR key;
	io_context *io_context;
	unsigned char buf[128];
	int len;
	unsigned long max_accepts = server->qs_params.listener.init_accepts_count / server->qs_params.worker_threads_count;
	unsigned long accepts = 0;

	for(;;)
	{
		if (!GetQueuedCompletionStatus(server->iocp, &bytes_transferred, &key, (LPOVERLAPPED *)&io_context, INFINITE))
		{
			if(io_context != NULL)
			{
				cry("%s: GetQueuedCompletionStatus() fail with error: %d",	__func__, GetLastError());
			}
			else
			{
				cry("%s: GetQueuedCompletionStatus() fail, io_context == NULL");
			}
			break;
		}

		if(!bytes_transferred && io_context->ended_operation != on_connect)
		{
			//connection_list_remove(server->connections, &io_context->connection.connection);
			InterlockedDecrement(&server->qs_info.active_connections_count);
			(*server->qs_params.callbacks.on_disconnect)(&io_context->connection);
			socket_close(io_context->connection.client.sock, &server->qs_info);
			memory_manager_free(server->mem_manager, io_context);

			for(; accepts < max_accepts; ++accepts)
			{
				init_accept(server, buf);		
			}
			continue;
		}

		if(io_context->ended_operation == on_connect) 
		{	
			accepts--;
			//connection_list_add(server->connections, &io_context->connection.connection);
			InterlockedIncrement(&server->qs_info.active_connections_count);			
			setsockopt(io_context->connection.client.sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
				(char *)&server->qs_socket, sizeof(server->qs_socket) );
			set_keep_alive(&io_context->connection, server->qs_params.keep_alive_time, server->qs_params.keep_alive_interval);
			len = sizeof(io_context->connection.client.lsa.sa);
			getsockname(io_context->connection.client.sock, &io_context->connection.client.lsa.sa, &len);
			len = sizeof(io_context->connection.client.rsa.sa);
			getpeername(io_context->connection.client.sock, &io_context->connection.client.rsa.sa, &len);

			if(CreateIoCompletionPort((HANDLE)io_context->connection.client.sock, server->iocp, 0, 0) == NULL )
			{
				cry("%s: CreateIoCompletionPort() fail with error: %d",	__func__, GetLastError());
			}
			(*server->qs_params.callbacks.on_connect)(&(io_context->connection));
			continue;
		}

		io_context->connection.bytes_transferred = bytes_transferred;
		switch(io_context->ended_operation) 
		{
		case(send_done):
			(*server->qs_params.callbacks.on_send)(&(io_context->connection));
			continue;

		case(recv_done):
			(*server->qs_params.callbacks.on_recv)(&(io_context->connection));
			continue;

		case(transmit_file):
			(*server->qs_params.callbacks.on_send_file)(&(io_context->connection));
			continue;

		case(user_message):
			(*server->qs_params.callbacks.on_message)(&(io_context->connection), (void *)key);
			continue;

		case(start_server):
			for(; accepts < server->qs_params.listener.init_accepts_count; ++accepts)
			{
				init_accept(server, buf);		
			}
			memory_manager_free(server->mem_manager, io_context);
			continue;
		case(start_clean):
			//clear_inactive_connections(server, 20);
			continue;
		case(stop_server):
			memory_manager_free(server->mem_manager, io_context);
			goto exit;
		}

	}
	exit:

	return 0;
}


BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}