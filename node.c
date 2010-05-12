#include "shared.h"

/*
 * Reads one event from the given socket into the given merlin_event
 * structure. Returns 0 on success and < 0 on errors
 */
int node_read_event(merlin_node *node, merlin_event *pkt)
{
	int len;
	uint result;

	len = io_recv_all(node->sock, &pkt->hdr, HDR_SIZE);
	if (len != HDR_SIZE) {
		lerr("In read_event: Incomplete header read(). Expected %zu, got %d",
			 HDR_SIZE, len);
		lerr("Sync lost with %s?", node->name);
		return -1;
	}

	if (pkt->hdr.protocol != MERLIN_PROTOCOL_VERSION) {
		lerr("Bad protocol version (%d, expected %d)\n",
			 pkt->hdr.protocol, MERLIN_PROTOCOL_VERSION);
		return -1;
	}

	if (!pkt->hdr.len && pkt->hdr.type != CTRL_PACKET) {
		lerr("Non-control packet of type %d with zero size length (this should never happen)", pkt->hdr.type);
		return len;
	}

	if (!pkt->hdr.len)
		return HDR_SIZE;

	result = io_recv_all(node->sock, pkt->body, pkt->hdr.len);
	if (result != pkt->hdr.len) {
		lwarn("Bogus read in proto_read_event(). got %d, expected %d",
			  result, pkt->hdr.len);
		lwarn("Sync lost with %s?", node->name);
	}

	return result;
}

/*
 * Send the given event "pkt" to the node "node", or take appropriate
 * actions on the node itself in case sending fails.
 * Returns 0 on success, and < 0 otherwise.
 */
int node_send_event(merlin_node *node, merlin_event *pkt)
{
	pkt->hdr.protocol = MERLIN_PROTOCOL_VERSION;

	if (packet_size(pkt) > TOTAL_PKT_SIZE) {
		ldebug("header is invalid, or packet is too large. aborting\n");
		return -1;
	}

	return io_send_all(node->sock, pkt, packet_size(pkt));
}

/*
 * Sends a control event of type "type" with selection "selection"
 * to node "node"
 */
int node_send_ctrl(merlin_node *node, int type, int selection)
{
	merlin_event pkt;

	memset(&pkt.hdr, 0, HDR_SIZE);

	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.len = 0;
	pkt.hdr.code = control_type;
	pkt.hdr.selection = selection;

	return proto_send_event(node, &pkt);
}
