/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/udp.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include <net/checksum.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_log.h>

enum udplite_conntrack {
	UDPLITE_CT_UNREPLIED,
	UDPLITE_CT_REPLIED,
	UDPLITE_CT_MAX
};

static unsigned int udplite_timeouts[UDPLITE_CT_MAX] = {
	[UDPLITE_CT_UNREPLIED]	= 30*HZ,
	[UDPLITE_CT_REPLIED]	= 180*HZ,
};

static bool udplite_pkt_to_tuple(const struct sk_buff *skb,
				 unsigned int dataoff,
				 struct net *net,
				 struct nf_conntrack_tuple *tuple)
{
	const struct udphdr *hp;
	struct udphdr _hdr;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return false;

	tuple->src.u.udp.port = hp->source;
	tuple->dst.u.udp.port = hp->dest;
	return true;
}

static bool udplite_invert_tuple(struct nf_conntrack_tuple *tuple,
				 const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return true;
}

/* Print out the per-protocol part of the tuple. */
static void udplite_print_tuple(struct seq_file *s,
				const struct nf_conntrack_tuple *tuple)
{
	seq_printf(s, "sport=%hu dport=%hu ",
		   ntohs(tuple->src.u.udp.port),
		   ntohs(tuple->dst.u.udp.port));
}

/* Returns verdict for packet, and may modify conntracktype */
static int udplite_packet(struct nf_conn *ct,
			  const struct sk_buff *skb,
			  unsigned int dataoff,
			  enum ip_conntrack_info ctinfo,
			  const struct nf_hook_state *state)
{
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   udplite_timeouts[UDPLITE_CT_REPLIED]);
		/* Also, more likely to be important, and not a probe */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &ct->status))
			nf_conntrack_event_cache(IPCT_ASSURED, ct);
	} else {
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   udplite_timeouts[UDPLITE_CT_UNREPLIED]);
	}
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */

/* ==== 7.2 adaptation: static registration, no per-net ==== */
const struct nf_conntrack_l4proto nf_conntrack_l4proto_udplite = {
	.l4proto		= IPPROTO_UDPLITE,
	.allow_clash		= true,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nlattr_tuple_size	= nf_ct_port_nlattr_tuple_size,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
};

int nf_conntrack_udplite_packet(struct nf_conn *ct,
				struct sk_buff *skb,
				unsigned int dataoff,
				enum ip_conntrack_info ctinfo,
				const struct nf_hook_state *state)
{
	return udplite_packet(ct, skb, dataoff, ctinfo, state);
}
EXPORT_SYMBOL_GPL(nf_conntrack_udplite_packet);
