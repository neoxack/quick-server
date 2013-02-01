// tests.cpp: определяет точку входа для консольного приложения.
//
#include <conio.h>

#include "stdafx.h"
#include "qs_core.h"

#define COUNT 64000
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
		"AAAAAAAAAAAAAAAAAAAARrrrrrrrrrrrrrrrrrrrrrrrrrrr"
		"</body>"
		"</html>";

	
	char *response = "HTTP/1.0 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
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
	//qs_close_connection(server, connection);
	return 1;
}

static void on_error( wchar_t *func_name, unsigned long error )
{
}

int _tmain()
{
	qs_params params = {0};
	qs_create(&server);

	params.worker_threads_count = 6;
	params.expected_connections_amount = COUNT;
	params.connection_buffer_size = BUF_SIZE;
	params.keep_alive_time = 5000;
	params.keep_alive_interval = 500;
	params.listener.listen_adr = "80";
	params.listener.init_accepts_count = 200;

	params.callbacks.on_connect = on_connect1;
	params.callbacks.on_disconnect = on_disconnect1;
	params.callbacks.on_recv = on_recv;
	params.callbacks.on_send = on_send;
	params.callbacks.on_error = on_error;


	qs_start(server, &params);

	
	_getch();
	return 0;
}

