#include "stdafx.h"
#include "qs_lib.h"

#define COUNT 10000
#define BUF_SIZE 4096

static void *server;

static BOOL on_connect1( connection *connection )
{
	char buf1[64];
	sockaddr_to_string(buf1, 64, &connection->client.rsa); 
	printf("connection from: %s\n", buf1);
	if(qs_recv(connection, connection->buffer, BUF_SIZE) != 0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static void on_disconnect1( connection *connection )
{
	char buf[128];
	sockaddr_to_string(buf, 128, &connection->client.rsa); 
	printf("%s disconnect\n", buf);
}

static BOOL on_recv( connection *connection)
{
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
	//	"Connection: close\r\n"
		"Content-Length: %d\r\n\r\n";

	sprintf((char *)connection->buffer, response, (u_int)strlen(html));
	strcat((char *)connection->buffer, html);

	if (qs_send(connection, connection->buffer, (u_long)strlen((char *)connection->buffer)) != 0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static BOOL on_send( connection *connection)
{
	if(qs_recv(connection, connection->buffer, BUF_SIZE)!=0)
	{
		qs_close_connection(server, connection);
	}
	return 1;
}

static void on_error( wchar_t *error)
{
	wprintf(L"%s", error);
}

static void  enum_proc(void *con, void *param)
{
	char buf[64];
	connection *conn = (connection *)con;
	sockaddr_to_string(buf, sizeof(buf), &conn->client.rsa); 
	printf("enum %s\n", buf);
}

int _tmain()
{
	u_int res;
	qs_params params = {0};
	
	params.worker_threads_count = 8;
	params.expected_connections_amount = COUNT;
	params.connection_buffer_size = BUF_SIZE;
	params.keep_alive_time = 10000;
	params.keep_alive_interval = 1000;
	params.connections_idle_timeout = 10000;
	params.listener.listen_adr = "80";
	params.listener.init_accepts_count = 200;

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
		qs_enum_connections(server, enum_proc, 0);
		system("pause");
		qs_stop(server);
		printf("%s", "Server stopped\n");
	}

	system("pause");
	return 0;
}

