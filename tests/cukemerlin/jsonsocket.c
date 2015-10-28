#include "jsonsocket.h"

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define LINEBUF_SOCKET_BUFSIZE 1024

/* Some backward compatible stuff for glib */
#ifndef G_SOURCE_CONTINUE
#define G_SOURCE_CONTINUE 1
#endif
#ifndef G_SOURCE_REMOVE
#define G_SOURCE_REMOVE 0
#endif

struct JSONSocket_ {
	GSocketService *sockserv;
	gpointer (*session_new)(GSocket *, gpointer);
	gboolean (*session_newline)(GSocket *, JsonNode *, gpointer);
	void (*session_destroy)(gpointer);
	gpointer user_data;
};

typedef struct JSONSocketStorage_ {
	gchar buffer[LINEBUF_SOCKET_BUFSIZE];
	gssize bufptr;
	JSONSocket *csock;
	gpointer user_data;
} JSONSocketStorage;

static gboolean jsonsocket_new_request(GSocketService *service,
		GSocketConnection *connection, GObject *source_object,
		gpointer user_data);

static gboolean jsonsocket_recv(GSocket *sock, GIOCondition condition,
		gpointer user_data);
static void jsonsocket_disconnect(gpointer user_data);

JSONSocket *jsonsocket_new(const gchar *bind_addr, const gint bind_port,
		gpointer (*session_new)(GSocket *, gpointer),
		gboolean (*session_newline)(GSocket *, JsonNode *, gpointer),
		void (*session_destroy)(gpointer), gpointer user_data) {
	GInetAddress *inetaddr = NULL;
	GSocketAddress *addr = NULL;
	JSONSocket *csock;

	csock = g_malloc(sizeof(JSONSocket));
	csock->session_new = session_new;
	csock->session_newline = session_newline;
	csock->session_destroy = session_destroy;
	csock->user_data = user_data;

	csock->sockserv = g_socket_service_new();
	if (!csock->sockserv) {
		/* Some error */
	}

	/* Create destination address */
	inetaddr = g_inet_address_new_from_string(bind_addr);
	addr = g_inet_socket_address_new(inetaddr, bind_port);
	g_object_unref(G_OBJECT(inetaddr));

	if (!g_socket_listener_add_address((GSocketListener*) csock->sockserv, addr,
			G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
			NULL,
			NULL,
			NULL /* (GError **) */
			)) {
		/* Some error */
	}
	g_object_unref(addr);

	g_signal_connect((GObject * )csock->sockserv, "incoming",
			G_CALLBACK (jsonsocket_new_request), csock);

	return csock;
}

void jsonsocket_destroy(JSONSocket *csock) {
	if (csock == NULL)
		return;
	g_socket_listener_close((GSocketListener*) csock->sockserv);
	g_free(csock);
}

static gboolean jsonsocket_new_request(GSocketService *service,
		GSocketConnection *connection, GObject *source_object,
		gpointer user_data) {
	JSONSocket *csock = (JSONSocket *) user_data;
	GSocket *sock;
	GSource *source;
	JSONSocketStorage *stor;

	sock = g_socket_connection_get_socket(connection);
	g_socket_set_blocking(sock, FALSE);

	stor = g_malloc(sizeof(JSONSocketStorage));
	stor->bufptr = 0;
	stor->csock = csock;

	source = g_socket_create_source(sock, G_IO_IN, NULL);

	g_source_set_callback(source, (GSourceFunc) jsonsocket_recv, stor,
			jsonsocket_disconnect);
	g_source_attach(source, NULL);

	stor->user_data = (*csock->session_new)(sock, csock->user_data);

	return FALSE;
}

static gboolean jsonsocket_recv(GSocket *sock, GIOCondition condition,
		gpointer user_data) {
	JSONSocketStorage *stor = (JSONSocketStorage *) user_data;
	GError *error = NULL;
	gssize sz;
	gssize i;
	gboolean found_line;
	gboolean running;

	JsonNode *node = NULL;


	sz = g_socket_receive(sock, stor->buffer + stor->bufptr,
	LINEBUF_SOCKET_BUFSIZE - stor->bufptr,
	NULL, &error);

	if (sz > 0) {
		stor->bufptr += sz;
		stor->buffer[stor->bufptr] = '\0';
		running = G_SOURCE_CONTINUE;
		do {
			found_line = FALSE;
			for (i = 0; i < stor->bufptr; i++) {
				if (stor->buffer[i] == '\n') {
					found_line = TRUE;
					stor->buffer[i] = '\0';

					node = json_decode(stor->buffer);
					running = (*stor->csock->session_newline)(sock,
							node, stor->user_data);
					json_delete(node);

					stor->bufptr -= (i + 1);
					memmove(stor->buffer, &(stor->buffer[i + 1]), stor->bufptr);
					break;
				}
			}
		} while (found_line && (running == G_SOURCE_CONTINUE));

		return running;
	}

	if (sz == 0) {
		g_object_unref(sock);
		return G_SOURCE_REMOVE;
	}

	printf("Some error: %s\n", error->message);
	g_error_free(error);

	g_object_unref(sock);
	return G_SOURCE_REMOVE;
}

static void jsonsocket_disconnect(gpointer user_data) {
	JSONSocketStorage *stor = (JSONSocketStorage *) user_data;
	(*stor->csock->session_destroy)(stor->user_data);
	g_free(stor);
}

void jsonsocket_send(GSocket *sock, const JsonNode *node) {
	char *buf = json_encode(node);
	g_socket_send(sock, buf, strlen(buf), NULL, NULL);
	g_socket_send(sock, "\n", 1, NULL, NULL);
	free(buf);
}
