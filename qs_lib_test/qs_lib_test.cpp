#include "stdafx.h"
#include "qs_lib.h"

#define BUF_SIZE 1024 * 1024

# define ADDRSTRLEN 64
static void *server;

typedef struct test_struct {
	int num;
	bool flag;
} test_struct;

static BOOL on_connect1( connection *connection )
{
	char buf1[ADDRSTRLEN];
	sockaddr_to_string(buf1, sizeof(buf1), &connection->socket.rsa); 
	printf("connection from: %s\n", buf1);
	test_struct *data = (test_struct *)qs_memory_alloc(sizeof(test_struct));
	data->flag = true;
	data->num = 50;
	connection->user_data = data;
	connection->buffer.data_len = BUF_SIZE;
	if(qs_recv(connection) != 0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static void on_disconnect1( connection *connection )
{
	char buf[ADDRSTRLEN];

	sockaddr_to_string(buf, sizeof(buf), &connection->socket.rsa); 
	printf("%s disconnect\n", buf);
}

static BOOL on_recv( connection *connection)
{
	test_struct *data = (test_struct *)connection->user_data;
	char *html = "<!DOCTYPE html>\n"
		"<html>"
		"<head>"
		"<meta charset=\"utf-8\" />"
		"<title>Test HTML5 site</title>"
		"</head>"
		"<body>"
		"<center><h1>Server is work</h1></center>"
		"</body>"
		"</html>";

	char *response = "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		//"Connection: close\r\n"
		"Content-Length: %d\r\n\r\n";

	sprintf((char *)connection->buffer.buf, response, (u_int)strlen(html));
	strcat((char *)connection->buffer.buf, html);
	connection->buffer.data_len = (u_long)strlen((char *)connection->buffer.buf);

	if (qs_send(connection) != 0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static BOOL on_send( connection *connection)
{
	connection->buffer.data_len = BUF_SIZE;
	if(qs_recv(connection)!=0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static void on_error( wchar_t *error)
{
	wprintf(L"%s", error);
}

static void  enum_proc(connection *con)
{
	char buf[64];
	connection *conn = (connection *)con;
	sockaddr_to_string(buf, sizeof(buf), &conn->socket.rsa); 
	printf("enum %s\n", buf);
}

int _tmain()
{
	u_int res;
	qs_params params = {0};
	
	params.worker_threads_count = 8;
	params.connection_buffer_size = BUF_SIZE;
	params.keep_alive_time = 10000;
	params.keep_alive_interval = 1000;
	params.connections_idle_timeout = 10000;
	params.listener.listen_adr = "127.0.0.1:90";
	params.listener.init_accepts_count = 10;

	params.callbacks.on_connect = on_connect1;
	params.callbacks.on_disconnect = on_disconnect1;
	params.callbacks.on_recv = on_recv;
	params.callbacks.on_send = on_send;
	params.callbacks.on_error = on_error;

	qs_create(&server);
	res = qs_start(server, &params);
	if(res == 0)
	{
		printf("%s", "Server started\n");
		system("pause");
		qs_enum_connections(server, enum_proc);
		system("pause");
		qs_stop(server);
		printf("%s", "Server stopped\n");
	}

	system("pause");
	return 0;
}

