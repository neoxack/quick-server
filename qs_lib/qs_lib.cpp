#include "qs_lib.h"

#if defined (_MSC_VER)
// non-constant aggregate initializer: issued due to missing C99 support
#pragma warning (disable : 4204)
#endif

#pragma comment(lib, "ws2_32.lib")

#include <process.h>
#include <time.h>
#include <Mstcpip.h>

#define BUF_LEN 256
#define HAVE_INET_NTOP

#ifdef _MSC_VER
#define __func__ __FUNCTION__
#endif

#include "dl_list.h"

#include "nedmalloc.h"
#if !defined(USE_NEDMALLOC_DLL)
#include "nedmalloc.c"
#endif

using namespace nedalloc;

typedef struct _connection_storage {
	list *list;
	allocator allocator;
	CRITICAL_SECTION cs;
} connection_storage;

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
	u_long last_activity;
};

typedef struct _io_context io_context;

typedef struct _qs_context {
	qs_status status;
	qs_info qs_info;
	HANDLE iocp;
	uintptr_t *threads;
	void *timer;
	connection_storage * storage;
	struct socket qs_socket;
	qs_params qs_params;
	nedpool *pool;

	struct _ex_funcs {
		LPFN_ACCEPTEX AcceptEx; 
		LPFN_TRANSMITPACKETS TransmitPackets; 
		LPFN_DISCONNECTEX DisconnectEx;
		LPFN_TRANSMITFILE TransmitFile;
	} ex_funcs;

} qs_context;



void *my_alloc(size_t size)
{
	return nedmalloc(size);
}

void my_free(void *p)
{
	nedfree(p);
}

static connection_storage * connection_storage_new(size_t max_count)
{
	connection_storage * storage = (connection_storage *)malloc(sizeof connection_storage);
	if(!storage) return NULL;
	allocator allocator;
	allocator.alloc = my_alloc;
	allocator.free = my_free;

	storage->list = create_list(allocator);
	InitializeCriticalSectionAndSpinCount(&storage->cs, 0x400);
	return storage;
}


static void connection_storage_add(connection_storage * storage, connection * con)
{
	EnterCriticalSection(&storage->cs);
	push_front(storage->list, con);
	LeaveCriticalSection(&storage->cs);
}

static void connection_storage_traverse(connection_storage * storage, void (*do_func) (void *, void *), void *param)
{
	EnterCriticalSection(&storage->cs);
	traverse(storage->list, do_func, param);
	LeaveCriticalSection(&storage->cs);
}

int eq(const void* a, const void* b)
{
	return a == b;
}

static void connection_storage_delete(connection_storage * storage, connection * connection)
{
	EnterCriticalSection(&storage->cs);
	remove_data_once(storage->list, connection, eq);
	LeaveCriticalSection(&storage->cs);
}

static void connection_storage_free(connection_storage * storage)
{
	DeleteCriticalSection(&storage->cs);
	empty_list(storage->list);
	free(storage);
}

#define malloc nedmalloc
#define calloc nedcalloc
#define free nedfree


// Print error message
static void cry(qs_context* server, const char *fmt, ...) 
{
	char buf[BUF_LEN];
	wchar_t widechar_buf[BUF_LEN];
	wchar_t buf_on_error[BUF_LEN];
	va_list ap;
	time_t seconds;
	struct tm timeinfo;
	size_t convertedChars;

	va_start(ap, fmt);
	vsnprintf_s(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	convertedChars = 0;
	mbstowcs_s(&convertedChars, widechar_buf, strlen(buf) + 1, buf, BUF_LEN);

	seconds = time(NULL);
	localtime_s(&timeinfo, &seconds);

	wsprintf(buf_on_error, L"[%02d:%02d:%02d %02d.%02d.%02d] %s\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year-100, widechar_buf);
	server->qs_params.callbacks.on_error(buf_on_error);
}

__inline static io_context *get_context(connection *connection)
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

static io_context *alloc_context(qs_context *server)
{
	io_context *io_cont = (io_context *)nedpmalloc(server->pool, sizeof(io_context));
	memset(io_cont, 0, sizeof(io_context));
	io_cont->connection.buffer = (BYTE *)nedpmalloc(server->pool, (size_t)server->qs_params.connection_buffer_size);
	return io_cont;
}

static void free_context(qs_context *server, io_context * io_context)
{
	nedpfree(server->pool, io_context->connection.buffer);
	nedpfree(server->pool, io_context);
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
	SOCKET s = socket_create(&server->qs_info);

	GUID accept_ex_GUID =        WSAID_ACCEPTEX; 
	GUID transmit_packets_GUID = WSAID_TRANSMITPACKETS; 
	GUID disconnect_ex_GUID =    WSAID_DISCONNECTEX;
	GUID transmitfile_GUID =     WSAID_TRANSMITFILE;
	u_long dwTmp;
	int res = TRUE;

	if ( ( WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &accept_ex_GUID, sizeof(accept_ex_GUID), &server->ex_funcs.AcceptEx, sizeof(server->ex_funcs.AcceptEx), &dwTmp, NULL, NULL)!=0) 
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &transmit_packets_GUID, sizeof(transmit_packets_GUID), &server->ex_funcs.TransmitPackets, sizeof(server->ex_funcs.TransmitPackets), &dwTmp, NULL, NULL)!=0) 
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &disconnect_ex_GUID, sizeof(disconnect_ex_GUID), &server->ex_funcs.DisconnectEx, sizeof(server->ex_funcs.DisconnectEx), &dwTmp, NULL, NULL)!=0)
		||(WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &transmitfile_GUID, sizeof(transmitfile_GUID), &server->ex_funcs.TransmitFile, sizeof(server->ex_funcs.TransmitFile), &dwTmp, NULL, NULL)!=0)) 
		res = FALSE;
	socket_close(s, &server->qs_info);
	return res;
}

MYDLL_API u_long qs_create(void **qs_instance )
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
			error = GetLastError();
			cry(server, "%s: WSAStartup() fail with error: %d",
				__func__, error);
			return error;
		}

		server->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
		if(!server->iocp) 
		{	
			error = GetLastError();
			WSACleanup();
			cry(server, "%s: CreateIoCompletionPort() fail with error: %d",	__func__, error);
			return error;
		}

		if(!init_ex_funcs(server)) 
		{		
			error = WSAGetLastError();
			CloseHandle(server->iocp);
			WSACleanup();
			cry(server, "%s: init_ex_funcs() fail with error: %d",	__func__, error);
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



static int parse_ipvX_addr_string(char *addr_buf, int port, union usa *u) 
{
#if defined(USE_IPV6) && defined(HAVE_INET_NTOP)
	// Only Windoze Vista (and newer) have inet_pton()
	struct in_addr a = {0};
	struct in6_addr a6 = {0};

	memset(u, 0, sizeof(usa));
	if (inet_pton(AF_INET6, addr_buf, &a6) > 0) {

		u->sin6.sin6_family = AF_INET6;
		u->sin6.sin6_port = htons((uint16_t) port);
		u->sin6.sin6_addr = a6;
		return 1;
	} else if (inet_pton(AF_INET, addr_buf, &a) > 0) {

		u->sin.sin_family = AF_INET;
		u->sin.sin_port = htons((uint16_t) port);
		u->sin.sin_addr = a;
		return 1;
	} else {
		return 0;
	}
#elif defined(HAVE_GETNAMEINFO)
	struct addrinfo hints = {0};
	struct addrinfo *rset = NULL;
#if defined(USE_IPV6)
	hints.ai_family = AF_UNSPEC;
#else
	hints.ai_family = AF_INET;
#endif
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_NUMERICHOST;
	if (!getaddrinfo(addr_buf, NULL, &hints, &rset) && rset) {
		memcpy(&usa->u.sa, rset->ai_addr, rset->ai_addrlen);
#if defined(USE_IPV6)
		if (rset->ai_family == PF_INET6) {
			usa->len = sizeof(usa->u.sin6);
			assert(rset->ai_addrlen == sizeof(usa->u.sin6));
			assert(usa->u.sin6.sin6_family == AF_INET6);
			usa->u.sin6.sin6_port = htons((uint16_t) port);
			freeaddrinfo(rset);
			return 1;
		} else
#endif
			if (rset->ai_family == PF_INET) {
				usa->len = sizeof(usa->u.sin);
				assert(rset->ai_addrlen == sizeof(usa->u.sin));
				assert(usa->u.sin.sin_family == AF_INET);
				usa->u.sin.sin_port = htons((uint16_t) port);
				freeaddrinfo(rset);
				return 1;
			}
	}
	if (rset) freeaddrinfo(rset);
	return 0;
#else
	int a, b, c, d, len;

	memset(u, 0, sizeof(usa));
	if (sscanf(addr_buf, "%d.%d.%d.%d%n", &a, &b, &c, &d, &len) == 4
		&& len == (int) strlen(addr_buf)) {
			// Bind to a specific IPv4 address
			u->sin.sin_family = AF_INET;
			u->sin.sin_port = htons((uint16_t) port);
			u->sin.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
			return 1;
	}
	return 0;
#endif
}

// Examples: 80, 127.0.0.1:3128
static int parse_port_string(const char *addr, struct socket *so) {
	union usa *usa = &so->lsa;
	int port, len;
	char addr_buf[128];

	memset(so, 0, sizeof(*so));

	if (sscanf(addr, " [%40[^]]]:%d%n", addr_buf, &port, &len) == 2
		&& len > 0
		&& parse_ipvX_addr_string(addr_buf, port, usa)) {
			// all done: probably IPv6 URI
	} else if (sscanf(addr, " %40[^:]:%d%n", addr_buf, &port, &len) == 2
		&& len > 0
		&& parse_ipvX_addr_string(addr_buf, port, usa)) {
			// all done: probably IPv4 URI
	} else if (sscanf(addr, "%d%n", &port, &len) != 1 ||
		len <= 0 ||
		(addr[len] && strchr("sp, \t", addr[len]) == NULL)) {
			return 0;
	} else {
#if defined(USE_IPV6)
		usa->sin6.sin6_family = AF_INET6;
		usa->sin6.sin6_port = htons((uint16_t) port);
#else
		usa->sin.sin_family = AF_INET;
		usa->sin.sin_port = htons((uint16_t) port);
#endif
	}

	return 1;
}

#define THREAD_STACK_SIZE 1024
unsigned __stdcall working_thread(void *s);
void WINAPI clean_timer_callback(void * , BOOL );

MYDLL_API unsigned int qs_start( void *qs_instance, qs_params * params )
{
	qs_context* server;
	size_t i;
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
		cry(server, "%s: invalid port spec.\nExpecting list of: %s",
			__func__, "[IP_ADDRESS:]PORT[s|p]");
		return ERROR_INVALID_PARAMETER;
	} 
	if ((so.sock = socket(so.lsa.sa.sa_family, SOCK_STREAM, IPPROTO_TCP)) ==
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
		u_int error = GetLastError();
		closesocket(so.sock);
		cry(server, "%s: cannot bind to %s", __func__, params->listener.listen_adr);
		return error;
	} 

	server->pool = nedcreatepool(server->qs_params.expected_connections_amount * ((size_t)server->qs_params.connection_buffer_size + sizeof(io_context)), server->qs_params.worker_threads_count);

	server->storage = connection_storage_new(server->qs_params.expected_connections_amount);

	memcpy(&server->qs_socket, &so, sizeof(so));
	CreateIoCompletionPort((HANDLE)server->qs_socket.sock, server->iocp, server->qs_socket.sock, 0);

	server->qs_info.sockets_count = 0;

	server->threads = (uintptr_t *)malloc(sizeof(uintptr_t) * (size_t)server->qs_params.worker_threads_count);

	for(i = 0; i<(size_t)server->qs_params.worker_threads_count; ++i)
	{
		server->threads[i] = create_thread(working_thread, server, THREAD_STACK_SIZE);
	}

	io_context = alloc_context(server);
	io_context->ended_operation = start_server;

	u_long idle_check_period = server->qs_params.connections_idle_timeout;
	if(idle_check_period)
	{
		CreateTimerQueueTimer(&server->timer, NULL, (WAITORTIMERCALLBACK)clean_timer_callback, server,  idle_check_period,  idle_check_period/2, NULL);
	}

	PostQueuedCompletionStatus(server->iocp, 8, 0, (LPOVERLAPPED)io_context);
	server->status = runned;
	return ERROR_SUCCESS;

}

void idle_check(void * p, void * user_data)
{
	qs_context *server = (qs_context *)user_data;
	connection *con = (connection *)p;
	io_context *context = get_context(con);
	u_long count = GetTickCount();
	if((count - context->last_activity) > server->qs_params.connections_idle_timeout)
	{
		shutdown(con->client.sock, SD_BOTH);
	}

}

void WINAPI clean_timer_callback(void * context, BOOL fTimerOrWaitFired)
{
	qs_context * server = (qs_context *)context;
	connection_storage *storage = server->storage;
	connection_storage_traverse(storage, idle_check, server);
}

MYDLL_API unsigned int qs_stop( void *qs_instance )
{
	qs_context* server = (qs_context*)qs_instance;
	size_t i;

	DeleteTimerQueueTimer(NULL, server->timer, NULL);
	for(i = 0; i<(size_t)server->qs_params.worker_threads_count; i++)
	{
		io_context *io_context = alloc_context(server);
		io_context->ended_operation = stop_server;
		PostQueuedCompletionStatus(server->iocp, 8, 0, (LPOVERLAPPED)io_context);
	}

	WaitForMultipleObjects(server->qs_params.worker_threads_count, (HANDLE *)server->threads, TRUE, INFINITE);
	for(i = 0; i<(size_t)server->qs_params.worker_threads_count; i++)
	{
		CloseHandle((HANDLE)server->threads[i]);
	}

	socket_close(server->qs_socket.sock, &server->qs_info);
	CloseHandle(server->iocp);
	connection_storage_free(server->storage);
	free(server->threads);
	neddestroypool(server->pool);
	neddisablethreadcache(0);
	server->qs_info.sockets_count = 0;
	server->qs_info.active_connections_count = 0;
	server->status = not_runned;

	return ERROR_SUCCESS;
}

MYDLL_API unsigned int qs_send(connection *connection, BYTE *buffer, u_long len)
{
	io_context *context;
	int  res;
	int  error;
	u_long bytes_send;
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

MYDLL_API unsigned int qs_recv(connection *connection, BYTE *buffer, u_long len)
{
	io_context *context;
	int res;
	int error;
	u_long bytes_recv;
	u_long flags = 0;
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

MYDLL_API unsigned int qs_enum_connections( void *qs_instance, ENUM_CONNECTIONS_PROC enum_connections_proc, void *param)
{
	qs_context *server;
	if(!qs_instance || !enum_connections_proc) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	connection_storage_traverse(server->storage, enum_connections_proc, param);
	return ERROR_SUCCESS;
}



MYDLL_API void sockaddr_to_string(char *buf, size_t len, const union usa *usa) {
	buf[0] = '\0';
#if defined(USE_IPV6) && defined(HAVE_INET_NTOP)
	// Only Windoze Vista (and newer) have inet_ntop()
	inet_ntop(usa->sa.sa_family, (usa->sa.sa_family == AF_INET ?
		(void *) &usa->sin.sin_addr :
	(void *) &usa->sin6.sin6_addr), buf, len);
#elif defined(HAVE_INET_NTOP)
	inet_ntop(usa->sa.sa_family, (void *) &usa->sin.sin_addr, buf, len);
#elif defined(_WIN32)
	strncpy(buf, inet_ntoa(usa->sin.sin_addr), len);
#else
#error check your platform for inet_ntop/etc.
#endif
	buf[len - 1] = 0;
}

static void init_accept(qs_context *server, BYTE *out_buf)
{
	SOCKET client;
	io_context *new_context;
	u_long bytes_transferred;
	int error;
	new_context = alloc_context(server);
	if(new_context != NULL)
	{
		client = socket_create(&server->qs_info);
		new_context->ended_operation = on_connect;
		new_context->connection.client.sock = client;

		if(server->ex_funcs.AcceptEx(server->qs_socket.sock, new_context->connection.client.sock, out_buf, 0, sizeof(struct sockaddr_storage) + 16, sizeof(struct sockaddr_storage) + 16, 
			&bytes_transferred, (LPOVERLAPPED)new_context) == 0 && (error = WSAGetLastError()!=997))
		{
			cry(server, "%s: AcceptEx() fail with error: %d",	__func__, error);
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
	}	
}


unsigned __stdcall working_thread(void *s) 
{
	qs_context *server = (qs_context *)s;
	u_long bytes_transferred;
	ULONG_PTR key;
	io_context *io_ctx;
	unsigned char buf[256];
	int len;
	u_long max_accepts = server->qs_params.listener.init_accepts_count / server->qs_params.worker_threads_count;
	u_long accepts = 0;

	for(;;)
	{
		if (!GetQueuedCompletionStatus(server->iocp, &bytes_transferred, &key, (LPOVERLAPPED *)&io_ctx, INFINITE))
		{
			if(io_ctx != NULL)
			{
				cry(server, "%s: GetQueuedCompletionStatus() fail with error: %d\n",	__func__, GetLastError());
				continue;
			}
			else
			{
				cry(server, "%s: GetQueuedCompletionStatus() fail, io_context == NULL");
				break;
			}
			
		}

		if((!bytes_transferred && io_ctx->ended_operation != on_connect) || io_ctx->ended_operation == on_disconnect)
		{
			(*server->qs_params.callbacks.on_disconnect)(&io_ctx->connection);
			socket_close(io_ctx->connection.client.sock, &server->qs_info);
			connection_storage_delete(server->storage, &io_ctx->connection);	
			free_context(server, io_ctx);

			for(; accepts < max_accepts; ++accepts)
			{
				init_accept(server, buf);		
			}
			continue;
		}

		if(io_ctx->ended_operation == on_connect) 
		{	
			accepts--;
			io_ctx->last_activity = GetTickCount();
			setsockopt(io_ctx->connection.client.sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
				(char *)&server->qs_socket, sizeof(server->qs_socket) );
			set_keep_alive(&io_ctx->connection, server->qs_params.keep_alive_time, server->qs_params.keep_alive_interval);
			len = sizeof(io_ctx->connection.client.lsa);
			getsockname(io_ctx->connection.client.sock, &io_ctx->connection.client.lsa.sa, &len);
			len = sizeof(io_ctx->connection.client.rsa);
			getpeername(io_ctx->connection.client.sock, &io_ctx->connection.client.rsa.sa, &len);

			if(CreateIoCompletionPort((HANDLE)io_ctx->connection.client.sock, server->iocp, 0, 0) == NULL )
			{
				cry(server, "%s: CreateIoCompletionPort() fail with error: %d",	__func__, GetLastError());
			}
			connection_storage_add(server->storage, &io_ctx->connection);	
			server->qs_params.callbacks.on_connect(&io_ctx->connection);
			continue;
		}
	
		io_ctx->connection.bytes_transferred = bytes_transferred;
		switch(io_ctx->ended_operation) 
		{
		case(send_done):
			io_ctx->last_activity = GetTickCount();
			(*server->qs_params.callbacks.on_send)(&(io_ctx->connection));
			break;

		case(recv_done):
			io_ctx->last_activity = GetTickCount();
			(*server->qs_params.callbacks.on_recv)(&(io_ctx->connection));
			break;

		case(transmit_file):
			io_ctx->last_activity = GetTickCount();
			(*server->qs_params.callbacks.on_send_file)(&(io_ctx->connection));
			break;

		case(user_message):
			io_ctx->last_activity = GetTickCount();
			(*server->qs_params.callbacks.on_message)(&(io_ctx->connection), (void *)key);
			break;

		case(start_server):
			for(; accepts < server->qs_params.listener.init_accepts_count; ++accepts)
			{
				init_accept(server, buf);		
			}
			free_context(server, io_ctx);
			break;
		case(stop_server):
			free_context(server, io_ctx);
			goto exit;
		}

	}
exit:
	neddisablethreadcache(server->pool);

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