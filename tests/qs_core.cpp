#include "qs_core.h"

#include <process.h>
#include <stdio.h>
#include <time.h>
#include <Mstcpip.h>

#define BUF_LEN 4096

#ifdef _MSC_VER
#define __func__ __FUNCTION__
#endif

__forceinline io_context *get_context(connection *connection)
{
	extend_connection *ext_conn = (extend_connection *)connection;
	return ext_conn->io_context;
}

connection_list* connection_list_new()
{
	connection_list* res = (connection_list*)malloc(sizeof(connection_list));
	if(!res) return NULL;


	res->count = 0;
	res->head = NULL;
	InitializeCriticalSection(&res->cs);

	return res;
}

void connection_list_delete(connection_list* list)
{
	node *cur = list->head;
	while(cur != NULL)
	{
		node * d = cur;
		cur = cur->next;
		free(d);
	}
	free(list);
}

void connection_list_add(connection_list* list, connection *con)
{
	node *new_node = (node *)malloc(sizeof(node));
	new_node->con = con;
	EnterCriticalSection(&list->cs);
	new_node->next = list->head;
	list->head = new_node;
	list->count++;
	LeaveCriticalSection(&list->cs);
}

void connection_list_remove(connection_list* list, connection *con)
{
	node *cur = list->head;
	EnterCriticalSection(&list->cs);
	
	if(list->count == 1)
	{
		free(list->head);
	}
	else
	while(cur->next != NULL)
	{
		if(cur->next->con == con)
		{
			node * d = cur->next;
			cur->next = cur->next->next;
			free(d);
		}
		cur = cur->next;
	}
	list->count--;
	LeaveCriticalSection(&list->cs);
}

void clear_inactive_connections(qs_context* context, int t)
{
	connection_list* list =  context->connections;
	time_t now = time(NULL);
	node *cur;
	EnterCriticalSection(&list->cs);
	cur = list->head;
	while(cur != NULL)
	{
		io_context *cont = get_context(cur->con);
		if(now - cont->last_activity > t)
		{
			qs_close_connection(context, cur->con);
		}
		cur = cur->next;
	}
	LeaveCriticalSection(&list->cs);
}

// Print error message
static void cry(const char *fmt, ...) 
{
	char buf[BUF_LEN];
	va_list ap;
	time_t seconds;
	struct tm* timeinfo;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	seconds = time(NULL);
	timeinfo = localtime(&seconds);
	
	printf("[%02d:%02d:%02d %02d.%02d.%02d] %s\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year-100, buf);
}


void sockaddr_to_string(char *buf, size_t len, union usa *usa) 
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

// Examples: 80, 127.0.0.1:3128
// TODO(lsm): add parsing of the IPv6 address
static int parse_port_string(const char *ptr, struct socket *so)
{
	int a, b, c, d, port, len;

	memset(so, 0, sizeof(*so));

	if (sscanf(ptr, "%d.%d.%d.%d:%d%n", &a, &b, &c, &d, &port, &len) == 5) 
	{
	// Bind to a specific IPv4 address
		so->lsa.sin.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
	} 
	else if (sscanf(ptr, "%d%n", &port, &len) != 1 || len <= 0)
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

static void create_thread(unsigned (__stdcall * start_addr) (void *), void * args, unsigned int stack_size)
{
	unsigned threadID;
	uintptr_t thread = _beginthreadex(NULL, stack_size, start_addr, args, 0, &threadID );
	CloseHandle((HANDLE)thread);
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
	io_cont->connection.connection.buffer = (BYTE *)fast_buf_alloc(manager->buffer);
	io_cont->connection.io_context = io_cont;
	return io_cont;
}

void memory_manager_free(memory_manager *manager, io_context *io_context)
{
	fast_buf_free(manager->buffer, io_context->connection.connection.buffer);
	fast_buf_free(manager->context_buf, io_context);
}

void memory_manager_destroy(memory_manager *manager)
{
	fast_buf_destroy(manager->buffer);
	fast_buf_destroy(manager->context_buf);
	free(manager);
}



__forceinline SOCKET socket_create(qs_info *info)
{
	SOCKET sock;
	#if defined(USE_IPV6)
	sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    #else
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	#endif
	InterlockedIncrement(&info->opened_sockets_count);
	return sock;
}

__forceinline void socket_close(SOCKET sock, qs_info *info)
{
	closesocket(sock);
	InterlockedDecrement(&info->opened_sockets_count);
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


unsigned long  qs_create(void **qs_instance )
{
	qs_context* context = (qs_context*)malloc(sizeof(qs_context));
	*qs_instance = context;
	if(*qs_instance)
	{
		memset(*qs_instance, 0, sizeof(qs_instance));
	
		return ERROR_SUCCESS;
	}
	else return ERROR_ALLOCATE_BUCKET;
}

void qs_delete(void *qs_instance )
{
	free(qs_instance);
}

unsigned __stdcall working_thread(void *s);
unsigned __stdcall cleaner_thread(void *s);

unsigned int qs_start( void *qs_instance, qs_params * params )
{
	WSADATA wsaData;
	int result;
	int error;	
	qs_context* server;
	unsigned int i;
	io_context *io_context;
	struct socket so;
	int on = 1;
	
	if(!qs_instance || !params) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;	
	if(server->status == runned) return ERROR_ALREADY_EXISTS;

	memcpy(&server->qs_params, params, sizeof(qs_params));

	result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (result != 0) 
	{
		cry("%s: WSAStartup() fail with error: %d",
			__func__, GetLastError());
		return GetLastError();
	}

	server->mem_manager = memory_manager_create(server->qs_params.expected_connections_amount,
												(size_t)server->qs_params.connection_buffer_size);

	if(!server->mem_manager)
	{
		cry("%s: memory_manager_create() fail",	__func__);
		return ERROR_ALLOCATE_BUCKET;
	}
	server->connections = connection_list_new();

	server->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if(!server->iocp) 
	{
		memory_manager_destroy(server->mem_manager);
		error = GetLastError();
		WSACleanup();
		cry("%s: CreateIoCompletionPort() fail with error: %d",	__func__, error);
		return error;
	}

	if(!init_ex_funcs(server)) 
	{
		memory_manager_destroy(server->mem_manager);
		error = WSAGetLastError();
		WSACleanup();
		cry("%s: init_ex_funcs() fail with error: %d",	__func__, error);
		return error;
	}

	if (!parse_port_string(params->listener.listen_adr, &so))
	{
		memory_manager_destroy(server->mem_manager);
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
		memory_manager_destroy(server->mem_manager);
		closesocket(so.sock);
		cry("%s: cannot bind to %s", __func__, params->listener.listen_adr);
    } 
	else
	{
		memcpy(&server->qs_socket, &so, sizeof(so));
		CreateIoCompletionPort((HANDLE)server->qs_socket.sock, server->iocp, server->qs_socket.sock, 0);

		server->qs_info.opened_sockets_count = 0;

		for(i = 0; i<server->qs_params.worker_threads_count; ++i)
		{
			create_thread(working_thread, server, 256);
		}
	//	create_thread(cleaner_thread, server, 512);

		io_context = memory_manager_alloc(server->mem_manager);
		io_context->ended_operation = start_server;

		PostQueuedCompletionStatus(server->iocp, 8, (ULONG_PTR)&params->listener.init_accepts_count, (LPOVERLAPPED)io_context);
		server->status = runned;
		return ERROR_SUCCESS;
	}
	
	return GetLastError();
}

unsigned int qs_send(connection *connection, BYTE *buffer, unsigned long len)
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

unsigned int  qs_send_file(void *qs_instance, connection *connection, HANDLE file)
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

unsigned int qs_recv(connection *connection, BYTE *buffer, unsigned long len)
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

unsigned int  qs_close_connection( void *qs_instance, connection *connection )
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

unsigned int qs_post_message_to_pool(void *qs_instance, void *message, connection *connection)
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

unsigned int qs_query_qs_information( void *qs_instance, qs_info *qs_information )
{
	qs_context *server;
	if(!qs_instance || !qs_information) return ERROR_INVALID_PARAMETER;
	server = (qs_context*)qs_instance;
	memcpy(qs_information, &server->qs_info, sizeof(qs_info));
	return ERROR_SUCCESS;
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
		new_context->connection.connection.client.sock = client;

		if(server->ex_funcs.AcceptEx(server->qs_socket.sock, new_context->connection.connection.client.sock, out_buf, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16, 
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
			NULL, 0, &dwSize,(LPOVERLAPPED)context, NULL);
		if (dwRet == SOCKET_ERROR)
		{
			cry("%s: WSAIoctl() fail with error: %d", __func__, WSAGetLastError());
		}
	}	
}

unsigned __stdcall cleaner_thread(void *s) 
{
	qs_context *server = (qs_context *)s;

	io_context *context = memory_manager_alloc(server->mem_manager);
	if(context != NULL)
	{
		for(;;)
		{
			Sleep(10000);
			context->ended_operation = start_clean;
			PostQueuedCompletionStatus(server->iocp, 8, (uintptr_t)0, (LPOVERLAPPED)context);
		}
	}
	return 0;
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
			(*server->qs_params.callbacks.on_disconnect)((connection *)&io_context->connection);
			socket_close(io_context->connection.connection.client.sock, &server->qs_info);
			memory_manager_free(server->mem_manager, io_context);
			for(; accepts < max_accepts; ++accepts)
			{
				init_accept(server, buf);		
			}
			continue;
		}
		
		if(io_context->ended_operation == on_connect) 
		{
			 
			//connection_list_add(server->connections, &io_context->connection.connection);
			//InterlockedIncrement(&server->qs_info.connections_count);
			set_keep_alive(&io_context->connection.connection, server->qs_params.keep_alive_time, server->qs_params.keep_alive_interval);
			setsockopt(io_context->connection.connection.client.sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
				(char *)&server->qs_socket, sizeof(server->qs_socket) );
		
			len = sizeof(io_context->connection.connection.client.lsa.sa);
			getsockname(io_context->connection.connection.client.sock, &io_context->connection.connection.client.lsa.sa, &len);
			len = sizeof(io_context->connection.connection.client.rsa.sa);
			getpeername(io_context->connection.connection.client.sock, &io_context->connection.connection.client.rsa.sa, &len);
			
			if(CreateIoCompletionPort((HANDLE)io_context->connection.connection.client.sock, server->iocp, 0, 0) == NULL )
			{
				cry("%s: CreateIoCompletionPort() fail with error: %d",	__func__, GetLastError());
			}
			(*server->qs_params.callbacks.on_connect)(&(io_context->connection.connection));
			continue;
		}

		io_context->connection.connection.bytes_transferred = bytes_transferred;
		switch(io_context->ended_operation) 
		{
		case(send_done):
			(*server->qs_params.callbacks.on_send)(&(io_context->connection.connection));
				continue;

		case(recv_done):
			(*server->qs_params.callbacks.on_recv)(&(io_context->connection.connection));
				continue;

		case(transmit_file):
			(*server->qs_params.callbacks.on_send_file)(&(io_context->connection.connection));
				continue;

		case(user_message):
			(*server->qs_params.callbacks.on_message)(&(io_context->connection.connection), (void *)key);
				continue;

		case(start_server):
			for(; accepts < max_accepts; ++accepts)
			{
				init_accept(server, buf);		
			}
			memory_manager_free(server->mem_manager, io_context);
			continue;
		case(start_clean):
			clear_inactive_connections(server, 20);
			continue;
		}

	}


	return 0;
}