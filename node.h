#ifndef INCLUDE_node_h__
#define INCLUDE_node_h__

#include <sys/types.h>
#include <sys/time.h>
#include "binlog.h"

#define MERLIN_PROTOCOL_VERSION 0

/* various magic options for the "type" field */
#define CTRL_PACKET   0xffff  /* control packet. "code" described below */
#define ACK_PACKET    0xfffe  /* ACK ("I understood") (not used) */
#define NAK_PACKET    0xfffd  /* NAK ("I don't understand") (not used) */

/* If "type" is CTRL_PACKET, then "code" is one of the following */
#define CTRL_PULSE    1 /* keep-alive signal */
#define CTRL_INACTIVE 2 /* signals that a slave went offline */
#define CTRL_ACTIVE   3 /* signals that a slave went online */
#define CTRL_PATHS    4 /* body contains paths to import */
#define CTRL_STALL    5 /* signal that we can't accept events for a while */
#define CTRL_RESUME   6 /* now we can accept events again */
#define CTRL_STOP     7 /* exit() immediately (only accepted via ipc) */
#define CTRL_GENERIC  0xffff  /* generic control packet */

#define HDR_SIZE (sizeof(merlin_header))
#define PKT_SIZE (sizeof(merlin_event))
#define BODY_SIZE (TOTAL_PKT_SIZE - sizeof(merlin_header))
#define TOTAL_PKT_SIZE 32768
#define MAX_PKT_SIZE TOTAL_PKT_SIZE /* for now. remove this macro later */

#define packet_size(pkt) ((pkt)->hdr.len + HDR_SIZE)

struct merlin_header {
	u_int16_t protocol;   /* always 0 for now */
	u_int16_t type;       /* event type */
	u_int16_t code;       /* event code (used for control packets) */
	u_int16_t selection;  /* used when noc Nagios communicates with mrd */
	u_int32_t len;        /* size of body */
	struct timeval sent;  /* when this message was sent */

	/* pad to 64 bytes for future extensions */
	char padding[64 - sizeof(struct timeval) - (2 * 6)];
} __attribute__((packed));
typedef struct merlin_header merlin_header;

struct merlin_event {
	merlin_header hdr;
	char body[BODY_SIZE];
} __attribute__((packed));
typedef struct merlin_event merlin_event;

struct merlin_event_counter {
	unsigned long long sent, read, logged, dropped;
	time_t last_logged;     /* when we logged the event-count last */
	struct timeval start;
};
typedef struct merlin_event_counter merlin_event_counter;

/* for node->type */
#define MODE_LOCAL     0
#define MODE_NOC       1
#define MODE_PEER      (1 << 1)
#define MODE_POLLER    (1 << 2)

/* for node->status */
#define STATE_NONE 0
#define STATE_PENDING 1
#define STATE_NEGOTIATING 2
#define STATE_CONNECTED 3

struct merlin_node {
	char *name;             /* name of this node */
	int id;                 /* internal index lookup number */
	int sock;               /* the socket */
	int type;               /* server type (master, slave, peer) */
	int status;             /* status of this node (down, pending, active) */
	unsigned zread;         /* zero reads. 5 of those indicates closed con */
	unsigned selection;     /* numeric index for hostgroup */
	char *hostgroup;        /* only set for pollers on the noc-side */
	struct sockaddr *sa;    /* should always point to sain */
	struct sockaddr_in sain;
	time_t last_recv;       /* last time node sent something to us */
	time_t last_sent;       /* when we sent something last */
	int last_action;        /* LA_CONNECT | LA_DISCONNECT | LA_HANDLED */
	int poller_active;	/* Is the poller active? */
	binlog *binlog;         /* binary backlog for this node */
	merlin_event_counter events; /* event count */
	int (*action)(struct merlin_node *, int); /* (daemon) action handler */
	struct merlin_node *next; /* linked list (and tabulated) */
};
typedef struct merlin_node merlin_node;

extern void node_log_event_count(merlin_node *node, int force);
extern int node_send_event(merlin_node *node, merlin_event *pkt);
extern int node_read_event(merlin_node *node, merlin_event *pkt);
extern int node_send_ctrl(merlin_node *node, int type, int selection);

extern const char *node_state(merlin_node *node);
extern const char *node_type(merlin_node *node);
#endif
