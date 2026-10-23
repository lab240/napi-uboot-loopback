// SPDX-License-Identifier: GPL-2.0+
/*
 * loopdetect - detect a single-port L2 loopback on the onboard ethernet.
 *
 * Sends a few raw Ethernet frames (custom ethertype + random nonce) from the
 * active network device and listens briefly for the same frame to return on
 * RX. A passive TX->RX loopback plug (pins 1-2 looped to 3-6) echoes the frame
 * back; a real switch never reflects a frame to its source port. The result is
 * stored in env 'napi_loop' (1 = loopback present, 0 = not) for the boot
 * script to branch on. Matching is by ethertype + nonce, so it does not depend
 * on the source MAC being correct.
 */
#include <command.h>
#include <dm.h>
#include <env.h>
#include <net.h>
#include <time.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <vsprintf.h>
#include <stdio.h>

#define NAPI_ET_HI		0x88
#define NAPI_ET_LO		0xb5
#define NAPI_MAGIC		"NAPILOOP"
#define NAPI_MAGIC_LEN		8
#define NAPI_NONCE_LEN		8
#define NAPI_PAYLOAD_LEN	(NAPI_MAGIC_LEN + NAPI_NONCE_LEN)
#define NAPI_HDR_LEN		14
#define NAPI_FRAME_LEN		60
#define NAPI_LISTEN_MS		1000
#define NAPI_SEND_COUNT		3

static int napi_loop_probe(void)
{
	struct udevice *dev;
	struct eth_ops *ops;
	uchar frame[NAPI_FRAME_LEN];
	uchar nonce[NAPI_NONCE_LEN];
	ulong t0, seed;
	int i, found = 0, len;

	if (eth_init()) {
		printf("loopdetect: eth_init failed (no link?)\n");
		return 0;
	}

	dev = eth_get_dev();
	if (!dev) {
		printf("loopdetect: no active eth device\n");
		eth_halt();
		return 0;
	}
	ops = eth_get_ops(dev);
	if (!ops || !ops->send || !ops->recv) {
		printf("loopdetect: eth device lacks send/recv\n");
		eth_halt();
		return 0;
	}

	seed = get_timer(0);
	for (i = 0; i < NAPI_NONCE_LEN; i++)
		nonce[i] = (uchar)((seed >> (i * 5)) ^ (i * 37 + 0x5a));

	memset(frame, 0, sizeof(frame));
	memset(frame, 0xff, ARP_HLEN);			 /* dst: broadcast */
	memcpy(frame + ARP_HLEN, net_ethaddr, ARP_HLEN); /* src: our MAC */
	frame[12] = NAPI_ET_HI;
	frame[13] = NAPI_ET_LO;
	memcpy(frame + NAPI_HDR_LEN, NAPI_MAGIC, NAPI_MAGIC_LEN);
	memcpy(frame + NAPI_HDR_LEN + NAPI_MAGIC_LEN, nonce, NAPI_NONCE_LEN);

	for (i = 0; i < NAPI_SEND_COUNT; i++)
		ops->send(dev, frame, NAPI_FRAME_LEN);

	t0 = get_timer(0);
	while (get_timer(t0) < NAPI_LISTEN_MS) {
		uchar *pkt = NULL;

		len = ops->recv(dev, 0, &pkt);
		if (len >= (NAPI_HDR_LEN + NAPI_PAYLOAD_LEN) && pkt &&
		    pkt[12] == NAPI_ET_HI && pkt[13] == NAPI_ET_LO &&
		    !memcmp(pkt + NAPI_HDR_LEN, NAPI_MAGIC, NAPI_MAGIC_LEN) &&
		    !memcmp(pkt + NAPI_HDR_LEN + NAPI_MAGIC_LEN, nonce,
			    NAPI_NONCE_LEN))
			found = 1;

		if (len > 0 && ops->free_pkt)
			ops->free_pkt(dev, pkt, len);

		if (found)
			break;
		if (len == -EAGAIN)
			udelay(1000);
	}

	eth_halt();
	return found;
}

static int do_napi_loopdetect(struct cmd_tbl *cmdtp, int flag, int argc,
			      char *const argv[])
{
	int found = napi_loop_probe();

	env_set("napi_loop", found ? "1" : "0");
	printf("loopdetect: onboard ethernet loopback %s (napi_loop=%d)\n",
	       found ? "DETECTED" : "not detected", found);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(loopdetect, 1, 0, do_napi_loopdetect,
	   "detect single-port L2 loopback on the onboard ethernet",
	   "- probes the active ethernet and sets env 'napi_loop' to 1 or 0");
