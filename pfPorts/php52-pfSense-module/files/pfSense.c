/*
        Copyright (C) 2010 Ermal Lu�i
        All rights reserved.

        Redistribution and use in source and binary forms, with or without
        modification, are permitted provided that the following conditions are met:

        1. Redistributions of source code must retain the above copyright notice,
           this list of conditions and the following disclaimer.

        2. Redistributions in binary form must reproduce the above copyright
           notice, this list of conditions and the following disclaimer in the
           documentation and/or other materials provided with the distribution.

        THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
        INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
        AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
        AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
        OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
        SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
        INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
        CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
        ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
        POSSIBILITY OF SUCH DAMAGE.

*/

/*

Functions copied from util.c and modem.c of mpd5 source are protected by
this copyright.
They are Uulock, Uunlock, ExclusiveOpenDevice/ExclusiveCloseDevice and
OpenSerialDevice.

Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.

Subject to the following obligations and disclaimer of warranty,
use and redistribution of this software, in source or object code
forms, with or without modifications are expressly permitted by
Whistle Communications; provided, however, that:   (i) any and
all reproductions of the source or object code must include the
copyright notice above and the following disclaimer of warranties;
and (ii) no rights are granted, in any manner or form, to use
Whistle Communications, Inc. trademarks, including the mark "WHISTLE
COMMUNICATIONS" on advertising, endorsements, or otherwise except
as such appears in the above copyright notice or in the software.

THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS",
AND TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS
MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED,
REGARDING THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY AND
ALL IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE, OR NON-INFRINGEMENT.  WHISTLE COMMUNICATIONS DOES NOT
WARRANT, GUARANTEE, OR MAKE ANY REPRESENTATIONS REGARDING THE USE
OF, OR THE RESULTS OF THE USE OF THIS SOFTWARE IN TERMS OF ITS
CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.  IN NO EVENT
SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES RESULTING
FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING WITHOUT
LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED
AND UNDER ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS
IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#define IS_EXT_MODULE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_pfSense.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <net/pfvar.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/ethernet.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_ether.h>

#include <vm/vm_param.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>

#include <glob.h>
#include <termios.h>
#include <poll.h>

#include <netgraph.h>

ZEND_DECLARE_MODULE_GLOBALS(pfSense)

static function_entry pfSense_functions[] = {
    PHP_FE(pfSense_get_interface_info, NULL)
    PHP_FE(pfSense_get_interface_addresses, NULL)
    PHP_FE(pfSense_get_interface_stats, NULL)
    PHP_FE(pfSense_get_pf_stats, NULL)
    PHP_FE(pfSense_get_os_hw_data, NULL)
    PHP_FE(pfSense_get_os_kern_data, NULL)
    PHP_FE(pfSense_vlan_create, NULL)
    PHP_FE(pfSense_interface_rename, NULL)
    PHP_FE(pfSense_interface_mtu, NULL)
    PHP_FE(pfSense_interface_create, NULL)
    PHP_FE(pfSense_interface_destroy, NULL)
    PHP_FE(pfSense_interface_flags, NULL)
    PHP_FE(pfSense_interface_capabilities, NULL)
    PHP_FE(pfSense_interface_setaddress, NULL)
    PHP_FE(pfSense_interface_deladdress, NULL)
    PHP_FE(pfSense_ngctl_name, NULL)
    PHP_FE(pfSense_ngctl_attach, NULL)
    PHP_FE(pfSense_ngctl_detach, NULL)
    PHP_FE(pfSense_get_modem_devices, NULL)
	PHP_FE(pfSense_sync, NULL)
    {NULL, NULL, NULL}
};

zend_module_entry pfSense_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    PHP_PFSENSE_WORLD_EXTNAME,
    pfSense_functions,
    PHP_MINIT(pfSense_socket),
    PHP_MSHUTDOWN(pfSense_socket_close),
    NULL,
    NULL,
    NULL,
#if ZEND_MODULE_API_NO >= 20010901
    PHP_PFSENSE_WORLD_VERSION,
#endif
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PFSENSE
ZEND_GET_MODULE(pfSense)
#endif

enum {	PFRB_TABLES = 1, PFRB_TSTATS, PFRB_ADDRS, PFRB_ASTATS,
	PFRB_IFACES, PFRB_TRANS, PFRB_MAX };
struct pfr_buffer {
	int	 pfrb_type;	/* type of content, see enum above */
	int	 pfrb_size;	/* number of objects in buffer */
	int	 pfrb_msize;	/* maximum number of objects in buffer */
	void	*pfrb_caddr;	/* malloc'ated memory area */
};
#define PFRB_FOREACH(var, buf)				\
	for ((var) = pfr_buf_next((buf), NULL);		\
	    (var) != NULL;				\
	    (var) = pfr_buf_next((buf), (var)))

/* interface management code */

static int
pfi_get_ifaces(int dev, const char *filter, struct pfi_kif *buf, int *size)
{
	struct pfioc_iface io;

	if (size == NULL || *size < 0 || (*size && buf == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	bzero(&io, sizeof io);
	if (filter != NULL)
		if (strlcpy(io.pfiio_name, filter, sizeof(io.pfiio_name)) >=
		    sizeof(io.pfiio_name)) {
			errno = EINVAL;
			return (-1);
		}
	io.pfiio_buffer = buf;
	io.pfiio_esize = sizeof(*buf);
	io.pfiio_size = *size;
	if (ioctl(dev, DIOCIGETIFACES, &io))
		return (-1);
	*size = io.pfiio_size;
	return (0);
}

/* buffer management code */

size_t buf_esize[PFRB_MAX] = { 0,
	sizeof(struct pfr_table), sizeof(struct pfr_tstats),
	sizeof(struct pfr_addr), sizeof(struct pfr_astats),
	sizeof(struct pfi_kif), sizeof(struct pfioc_trans_e)
};

/*
 * return next element of the buffer (or first one if prev is NULL)
 * see PFRB_FOREACH macro
 */
static void *
pfr_buf_next(struct pfr_buffer *b, const void *prev)
{
	size_t bs;

	if (b == NULL || b->pfrb_type <= 0 || b->pfrb_type >= PFRB_MAX)
		return (NULL);
	if (b->pfrb_size == 0)
		return (NULL);
	if (prev == NULL)
		return (b->pfrb_caddr);
	bs = buf_esize[b->pfrb_type];
	if ((((caddr_t)prev)-((caddr_t)b->pfrb_caddr)) / bs >= b->pfrb_size-1)
		return (NULL);
	return (((caddr_t)prev) + bs);
}

/*
 * minsize:
 *    0: make the buffer somewhat bigger
 *    n: make room for "n" entries in the buffer
 */
static int
pfr_buf_grow(struct pfr_buffer *b, int minsize)
{
	caddr_t p;
	size_t bs;

	if (b == NULL || b->pfrb_type <= 0 || b->pfrb_type >= PFRB_MAX) {
		errno = EINVAL;
		return (-1);
	}
	if (minsize != 0 && minsize <= b->pfrb_msize)
		return (0);
	bs = buf_esize[b->pfrb_type];
	if (!b->pfrb_msize) {
		if (minsize < 64)
			minsize = 64;
		b->pfrb_caddr = calloc(bs, minsize);
		if (b->pfrb_caddr == NULL)
			return (-1);
		b->pfrb_msize = minsize;
	} else {
		if (minsize == 0)
			minsize = b->pfrb_msize * 2;
		if (minsize < 0 || minsize >= SIZE_T_MAX / bs) {
			/* msize overflow */
			errno = ENOMEM;
			return (-1);
		}
		p = realloc(b->pfrb_caddr, minsize * bs);
		if (p == NULL)
			return (-1);
		bzero(p + b->pfrb_msize * bs, (minsize - b->pfrb_msize) * bs);
		b->pfrb_caddr = p;
		b->pfrb_msize = minsize;
	}
	return (0);
}

/*
 * reset buffer and free memory.
 */
static void
pfr_buf_clear(struct pfr_buffer *b)
{
	if (b == NULL)
		return;
	if (b->pfrb_caddr != NULL)
		free(b->pfrb_caddr);
	b->pfrb_caddr = NULL;
	b->pfrb_size = b->pfrb_msize = 0;
}

PHP_MINIT_FUNCTION(pfSense_socket)
{
	int csock;

	PFSENSE_G(s) = socket(AF_LOCAL, SOCK_DGRAM, 0);
        if (PFSENSE_G(s) < 0)
        	return FAILURE;

	PFSENSE_G(inets) = socket(AF_INET, SOCK_DGRAM, 0);
        if (PFSENSE_G(inets) < 0) {
		close(PFSENSE_G(s));
        	return FAILURE;
	}

	/* Create a new socket node */
	if (NgMkSockNode(NULL, &csock, NULL) < 0) {
		return FAILURE;
	}

	PFSENSE_G(csock) = csock;

	/* Don't leak these sockets to child processes */
	fcntl(PFSENSE_G(s), F_SETFD, fcntl(PFSENSE_G(s), F_GETFD, 0) | FD_CLOEXEC);
	fcntl(PFSENSE_G(inets), F_SETFD, fcntl(PFSENSE_G(inets), F_GETFD, 0) | FD_CLOEXEC);
	fcntl(PFSENSE_G(csock), F_SETFD, fcntl(PFSENSE_G(csock), F_GETFD, 0) | FD_CLOEXEC);

	REGISTER_LONG_CONSTANT("IFF_UP", IFF_UP, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFF_LINK0", IFF_LINK0, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFF_LINK1", IFF_LINK1, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFF_LINK2", IFF_LINK2, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFF_NOARP", IFF_NOARP, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFF_STATICARP", IFF_STATICARP, CONST_PERSISTENT | CONST_CS);
#if defined(IFF_IPFW_FILTER)
	REGISTER_LONG_CONSTANT("IFF_IPFW_FILTER", IFF_IPFW_FILTER, CONST_PERSISTENT | CONST_CS);
#endif
	REGISTER_LONG_CONSTANT("IFCAP_RXCSUM", IFCAP_RXCSUM, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_TXCSUM", IFCAP_TXCSUM, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_POLLING", IFCAP_POLLING, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_TSO", IFCAP_TSO, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_LRO", IFCAP_LRO, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_WOL", IFCAP_WOL, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_WOL_UCAST", IFCAP_WOL_UCAST, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_WOL_MCAST", IFCAP_WOL_MCAST, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_WOL_MAGIC", IFCAP_WOL_MAGIC, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_RXCSUM", IFCAP_RXCSUM, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_RXCSUM", IFCAP_RXCSUM, CONST_PERSISTENT | CONST_CS);

	REGISTER_LONG_CONSTANT("IFCAP_VLAN_HWTAGGING", IFCAP_VLAN_HWTAGGING, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_VLAN_MTU", IFCAP_VLAN_MTU, CONST_PERSISTENT | CONST_CS);
	REGISTER_LONG_CONSTANT("IFCAP_VLAN_HWFILTER", IFCAP_VLAN_HWFILTER, CONST_PERSISTENT | CONST_CS);
#ifdef IFCAP_VLAN_HWTSO
	REGISTER_LONG_CONSTANT("IFCAP_VLAN_HWTSO", IFCAP_VLAN_HWTSO, CONST_PERSISTENT | CONST_CS);
#endif

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(pfSense_socket_close)
{
	close(PFSENSE_G(csock));
	close(PFSENSE_G(inets));
	close(PFSENSE_G(s));

	return SUCCESS;
}

PHP_FUNCTION(pfSense_get_interface_addresses)
{
	struct ifaddrs *ifdata, *mb;
        struct if_data *md;
	struct sockaddr_in *tmp;
        struct sockaddr_dl *tmpdl;
	struct ifreq ifr;
        char outputbuf[128];
        char *ifname;
        int ifname_len, s, addresscnt = 0;
	zval *caps;
	zval *encaps;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ifname, &ifname_len) == FAILURE) {
                RETURN NULL;
        }

        array_init(return_value);

	getifaddrs(&ifdata);
        if (ifdata == NULL) {
                //printf("No data found\n");
                RETURN NULL;
        }

        for(mb = ifdata; mb != NULL; mb = mb->ifa_next) {
		if (mb == NULL)
                        continue;
		if (ifname_len != strlen(mb->ifa_name))
                        continue;
                if (strncmp(ifname, mb->ifa_name, ifname_len) != 0)
                        continue;
	if (mb->ifa_flags & IFF_UP)
		add_assoc_string(return_value, "status", "up", 1);
	else
		add_assoc_string(return_value, "status", "down", 1);
	if (mb->ifa_flags & IFF_LINK0)
		add_assoc_long(return_value, "link0", 1);
	if (mb->ifa_flags & IFF_LINK1)
		add_assoc_long(return_value, "link1", 1);
	if (mb->ifa_flags & IFF_LINK2)
		add_assoc_long(return_value, "link2", 1);
	if (mb->ifa_flags & IFF_MULTICAST)
		add_assoc_long(return_value, "multicast", 1);
	if (mb->ifa_flags & IFF_LOOPBACK)
                add_assoc_long(return_value, "loopback", 1);
	if (mb->ifa_flags & IFF_POINTOPOINT)
                add_assoc_long(return_value, "pointtopoint", 1);
	if (mb->ifa_flags & IFF_PROMISC)
                add_assoc_long(return_value, "promisc", 1);
	if (mb->ifa_flags & IFF_PPROMISC)
                add_assoc_long(return_value, "permanentpromisc", 1);
	if (mb->ifa_flags & IFF_OACTIVE)
                add_assoc_long(return_value, "oactive", 1);
	if (mb->ifa_flags & IFF_ALLMULTI)
                add_assoc_long(return_value, "allmulti", 1);
	if (mb->ifa_flags & IFF_SIMPLEX)
                add_assoc_long(return_value, "simplex", 1);
	if (mb->ifa_data != NULL) {
		md = mb->ifa_data;
		if (md->ifi_link_state == LINK_STATE_UP)
                	add_assoc_long(return_value, "linkstateup", 1);
		//add_assoc_long(return_value, "hwassistflag", md->ifi_hwassist);
		add_assoc_long(return_value, "mtu", md->ifi_mtu);
		switch (md->ifi_type) {
		case IFT_IEEE80211:
			add_assoc_string(return_value, "iftype", "wireless", 1);
			break;
		case IFT_ETHER:
		case IFT_FASTETHER:
		case IFT_FASTETHERFX:
		case IFT_GIGABITETHERNET:
			add_assoc_string(return_value, "iftype", "ether", 1);
			break;
		case IFT_L2VLAN:
			add_assoc_string(return_value, "iftype", "vlan", 1);
			break;
		case IFT_BRIDGE:
			add_assoc_string(return_value, "iftype", "bridge", 1);
			break;
		case IFT_TUNNEL:
		case IFT_GIF:
		case IFT_FAITH:
		case IFT_ENC:
		case IFT_PFLOG: 
		case IFT_PFSYNC:
			add_assoc_string(return_value, "iftype", "virtual", 1);
			break;
		case IFT_CARP:
			add_assoc_string(return_value, "iftype", "carp", 1);
			break;
		default:
			add_assoc_string(return_value, "iftype", "other", 1);
		}
	}
	ALLOC_INIT_ZVAL(caps);
	ALLOC_INIT_ZVAL(encaps);
	array_init(caps);
	array_init(encaps);
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(PFSENSE_G(s), SIOCGIFCAP, (caddr_t)&ifr) == 0) {
		add_assoc_long(caps, "flags", ifr.ifr_reqcap);
		if (ifr.ifr_reqcap & IFCAP_POLLING)
			add_assoc_long(caps, "polling", 1);
		if (ifr.ifr_reqcap & IFCAP_RXCSUM)
			add_assoc_long(caps, "rxcsum", 1);
		if (ifr.ifr_reqcap & IFCAP_TXCSUM)
			add_assoc_long(caps, "txcsum", 1);
		if (ifr.ifr_reqcap & IFCAP_VLAN_MTU)
			add_assoc_long(caps, "vlanmtu", 1);
		if (ifr.ifr_reqcap & IFCAP_JUMBO_MTU)
			add_assoc_long(caps, "jumbomtu", 1);
		if (ifr.ifr_reqcap & IFCAP_VLAN_HWTAGGING)
			add_assoc_long(caps, "vlanhwtag", 1);
		if (ifr.ifr_reqcap & IFCAP_VLAN_HWCSUM)
               		add_assoc_long(caps, "vlanhwcsum", 1);
		if (ifr.ifr_reqcap & IFCAP_TSO4)
               		add_assoc_long(caps, "tso4", 1);
		if (ifr.ifr_reqcap & IFCAP_TSO6)
               		add_assoc_long(caps, "tso6", 1);
		if (ifr.ifr_reqcap & IFCAP_LRO)
               		add_assoc_long(caps, "lro", 1);
		if (ifr.ifr_reqcap & IFCAP_WOL_UCAST)
               		add_assoc_long(caps, "wolucast", 1);
		if (ifr.ifr_reqcap & IFCAP_WOL_MCAST)
               		add_assoc_long(caps, "wolmcast", 1);
		if (ifr.ifr_reqcap & IFCAP_WOL_MAGIC)
               		add_assoc_long(caps, "wolmagic", 1);
		if (ifr.ifr_reqcap & IFCAP_TOE4)
              		add_assoc_long(caps, "toe4", 1);
		if (ifr.ifr_reqcap & IFCAP_TOE6)
               		add_assoc_long(caps, "toe6", 1);
		if (ifr.ifr_reqcap & IFCAP_VLAN_HWFILTER)
               		add_assoc_long(caps, "vlanhwfilter", 1);
#if 0
		if (ifr.ifr_reqcap & IFCAP_POLLING_NOCOUNT)
               		add_assoc_long(caps, "pollingnocount", 1);
#endif
		add_assoc_long(encaps, "flags", ifr.ifr_curcap);
		if (ifr.ifr_curcap & IFCAP_POLLING)
                	add_assoc_long(encaps, "polling", 1);
                if (ifr.ifr_curcap & IFCAP_RXCSUM)
                        add_assoc_long(encaps, "rxcsum", 1);
                if (ifr.ifr_curcap & IFCAP_TXCSUM)
                        add_assoc_long(encaps, "txcsum", 1);
                if (ifr.ifr_curcap & IFCAP_VLAN_MTU)
                        add_assoc_long(encaps, "vlanmtu", 1);
                if (ifr.ifr_curcap & IFCAP_JUMBO_MTU)
                        add_assoc_long(encaps, "jumbomtu", 1);
                if (ifr.ifr_curcap & IFCAP_VLAN_HWTAGGING)
                        add_assoc_long(encaps, "vlanhwtag", 1);
                if (ifr.ifr_curcap & IFCAP_VLAN_HWCSUM)
                        add_assoc_long(encaps, "vlanhwcsum", 1);
                if (ifr.ifr_curcap & IFCAP_TSO4)
                        add_assoc_long(encaps, "tso4", 1);
                if (ifr.ifr_curcap & IFCAP_TSO6)
                        add_assoc_long(encaps, "tso6", 1);
                if (ifr.ifr_curcap & IFCAP_LRO)
                        add_assoc_long(encaps, "lro", 1);
                if (ifr.ifr_curcap & IFCAP_WOL_UCAST)
                        add_assoc_long(encaps, "wolucast", 1);
                if (ifr.ifr_curcap & IFCAP_WOL_MCAST)
                        add_assoc_long(encaps, "wolmcast", 1);
                if (ifr.ifr_curcap & IFCAP_WOL_MAGIC)
                        add_assoc_long(encaps, "wolmagic", 1);
                if (ifr.ifr_curcap & IFCAP_TOE4)
                        add_assoc_long(encaps, "toe4", 1);
                if (ifr.ifr_curcap & IFCAP_TOE6)
                        add_assoc_long(encaps, "toe6", 1);
                if (ifr.ifr_curcap & IFCAP_VLAN_HWFILTER)
			add_assoc_long(encaps, "vlanhwfilter", 1);
#if 0
                if (ifr.ifr_reqcap & IFCAP_POLLING_NOCOUNT)
                	add_assoc_long(caps, "pollingnocount", 1);
#endif
	}
	add_assoc_zval(return_value, "caps", caps);
	add_assoc_zval(return_value, "encaps", encaps);
	if (mb->ifa_addr == NULL)
		continue;
	switch (mb->ifa_addr->sa_family) {
	case AF_INET:
		if (addresscnt > 0)
			break;
                bzero(outputbuf, sizeof outputbuf);
                tmp = (struct sockaddr_in *)mb->ifa_addr;
                inet_ntop(AF_INET, (void *)&tmp->sin_addr, outputbuf, 128);
		add_assoc_string(return_value, "ipaddr", outputbuf, 1);
		addresscnt++;
                tmp = (struct sockaddr_in *)mb->ifa_netmask;
		unsigned char mask;
		const unsigned char *byte = (unsigned char *)&tmp->sin_addr.s_addr;
		int i = 0, n = sizeof(tmp->sin_addr.s_addr);
		while (n--) {
			mask = ((unsigned char)-1 >> 1) + 1;
				do {
					if (mask & byte[n])
						i++;
					mask >>= 1;
				} while (mask);
		}
		add_assoc_long(return_value, "subnetbits", i);

                bzero(outputbuf, sizeof outputbuf);
                inet_ntop(AF_INET, (void *)&tmp->sin_addr, outputbuf, 128);
                add_assoc_string(return_value, "subnet", outputbuf, 1);

                if (mb->ifa_flags & IFF_BROADCAST) {
                	bzero(outputbuf, sizeof outputbuf);
                                tmp = (struct sockaddr_in *)mb->ifa_broadaddr;
                                inet_ntop(AF_INET, (void *)&tmp->sin_addr, outputbuf, 128);
                                add_assoc_string(return_value, "broadcast", outputbuf, 1);
                        }

			if (mb->ifa_flags & IFF_POINTOPOINT) {
				bzero(outputbuf, sizeof outputbuf);
                                tmp = (struct sockaddr_in *)mb->ifa_dstaddr;
                                inet_ntop(AF_INET, (void *)&tmp->sin_addr, outputbuf, 128);
                                add_assoc_string(return_value, "tunnel", outputbuf, 1);
			}

		break;
		case AF_LINK:
                        tmpdl = (struct sockaddr_dl *)mb->ifa_addr;
                        bzero(outputbuf, sizeof outputbuf);
                        ether_ntoa_r((struct ether_addr *)LLADDR(tmpdl), outputbuf);
                        add_assoc_string(return_value, "macaddr", outputbuf, 1);
                        md = (struct if_data *)mb->ifa_data;

		break;
               }
        }
	freeifaddrs(ifdata);
}

PHP_FUNCTION(pfSense_interface_create) {
	char *ifname;
        int ifname_len, len;
	struct ifreq ifr;
	struct vlanreq params;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ifname, &ifname_len) == FAILURE) {
                RETURN_NULL();
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(PFSENSE_G(s), SIOCIFCREATE2, &ifr) < 0) {
		array_init(return_value);
		add_assoc_string(return_value, "error", "Could not create interface", 1);
	} else
		RETURN_STRING(ifr.ifr_name, 1)
}

PHP_FUNCTION(pfSense_interface_destroy) {
	char *ifname;
        int ifname_len;
	struct ifreq ifr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ifname, &ifname_len) == FAILURE) {
                RETURN_NULL();
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(PFSENSE_G(s), SIOCIFDESTROY, &ifr) < 0) {
		array_init(return_value);
		add_assoc_string(return_value, "error", "Could not create interface", 1);
	}
	
	RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_setaddress) {
	char *ifname, *ip, *p = NULL;
        int ifname_len, ip_len;
	struct sockaddr_in *sin;
	struct in_aliasreq ifra;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &ip, &ip_len) == FAILURE) {
		RETURN_NULL();
	}

	memset(&ifra, 0, sizeof(ifra));
	strlcpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name));
	sin =  &ifra.ifra_mask;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr.s_addr = 0;
	if ((p = strrchr(ip, '/')) != NULL) {
		/* address is `name/masklen' */
		int masklen;
		int ret;
		*p = '\0';
		ret = sscanf(p+1, "%u", &masklen);
		if(ret != 1 || (masklen < 0 || masklen > 32)) {
			*p = '/';
			RETURN_FALSE;
		}
		sin->sin_addr.s_addr = htonl(~((1LL << (32 - masklen)) - 1) &
			0xffffffff);
	}
	sin =  &ifra.ifra_addr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	if (inet_pton(AF_INET, ip, &sin->sin_addr) <= 0)
		RETURN_FALSE;

	if (ioctl(PFSENSE_G(inets), SIOCAIFADDR, &ifra) < 0) {
		array_init(return_value);
		add_assoc_string(return_value, "error", "Could not set interface address", 1);
	} else
		RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_deladdress) {
        char *ifname, *ip = NULL;
        int ifname_len, ip_len;
        struct sockaddr_in *sin;
        struct in_aliasreq ifra;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &ip, &ip_len) == FAILURE) {
                RETURN_NULL();
        }

        memset(&ifra, 0, sizeof(ifra));
        strlcpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name));
        sin =  &ifra.ifra_addr;
        sin->sin_family = AF_INET;
        sin->sin_len = sizeof(*sin);
        if (inet_pton(AF_INET, ip, &sin->sin_addr) <= 0)
                RETURN_FALSE;

        if (ioctl(PFSENSE_G(inets), SIOCDIFADDR, &ifra) < 0) {
                array_init(return_value);
                add_assoc_string(return_value, "error", "Could not delete interface address", 1);
        } else
                RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_rename) {
	char *ifname, *newifname;
	int ifname_len, newifname_len;
	struct ifreq ifr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &newifname, &newifname_len) == FAILURE) {
                RETURN_NULL();
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t) newifname;
	if (ioctl(PFSENSE_G(s), SIOCSIFNAME, (caddr_t) &ifr) < 0) {
		array_init(return_value);
		add_assoc_string(return_value, "error", "Could not rename interface", 1);
	} else
		RETURN_TRUE;
}

PHP_FUNCTION(pfSense_ngctl_name) {
	char *ifname, *newifname;
	int ifname_len, newifname_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &newifname, &newifname_len) == FAILURE) {
                RETURN_NULL();
	}

	/* Send message */
	if (NgNameNode(PFSENSE_G(csock), ifname, newifname) < 0)
		RETURN_NULL();

	RETURN_TRUE;
}

PHP_FUNCTION(pfSense_ngctl_attach) {
        char *ifname, *newifname;
        int ifname_len, newifname_len;
        struct ngm_name name;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &newifname, &newifname_len) == FAILURE) {
                RETURN_NULL();
        }

        snprintf(name.name, sizeof(name.name), "%s", newifname);
        /* Send message */
        if (NgSendMsg(PFSENSE_G(csock), ifname, NGM_GENERIC_COOKIE,
            NGM_ETHER_ATTACH, &name, sizeof(name)) < 0)
                RETURN_NULL();

        RETURN_TRUE;
}

PHP_FUNCTION(pfSense_ngctl_detach) {
        char *ifname, *newifname;
        int ifname_len, newifname_len;
        struct ngm_name name;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &ifname, &ifname_len, &newifname, &newifname_len) == FAILURE) {
                RETURN_NULL();
        }

        snprintf(name.name, sizeof(name.name), "%s", newifname);
        /* Send message */
        if (NgSendMsg(PFSENSE_G(csock), ifname, NGM_ETHER_COOKIE,
            NGM_ETHER_DETACH, &name, sizeof(name)) < 0)
                RETURN_NULL();

        RETURN_TRUE;
}

PHP_FUNCTION(pfSense_vlan_create) {
	char *ifname = NULL;
	char *parentifname = NULL;
	int ifname_len, parent_len;
	int tag; 
	struct ifreq ifr;
	struct vlanreq params;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ssl", &ifname, &ifname_len, &parentifname, &parent_len, &tag) == FAILURE) {
                RETURN_NULL();
        }

	memset(&ifr, 0, sizeof(ifr));
	memset(&params, 0, sizeof(params));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	strlcpy(params.vlr_parent, parentifname, sizeof(params.vlr_parent));
	params.vlr_tag = (u_short) tag;
	ifr.ifr_data = (caddr_t) &params;
	if (ioctl(PFSENSE_G(s), SIOCSETVLAN, (caddr_t) &ifr) < 0)
		RETURN_NULL();

	RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_mtu) {
	char *ifname;
	int ifname_len;
	int mtu;
	struct ifreq ifr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &ifname, &ifname_len, &mtu) == FAILURE) {
                RETURN_NULL();
        }
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_mtu = mtu;
	if (ioctl(PFSENSE_G(s), SIOCSIFMTU, (caddr_t)&ifr) < 0)
		RETURN_NULL();
	RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_flags) {
	struct ifreq ifr;
	char *ifname;
	int flags, ifname_len, value;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &ifname, &ifname_len, &value) == FAILURE) {
                RETURN_NULL();
        }

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(PFSENSE_G(s), SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		RETURN_NULL();
	}
	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else
		flags |= value; 
	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(PFSENSE_G(s), SIOCSIFFLAGS, (caddr_t)&ifr) < 0)
		RETURN_NULL();
	RETURN_TRUE;
}

PHP_FUNCTION(pfSense_interface_capabilities) {
	struct ifreq ifr;
	char *ifname;
	int flags, ifname_len, value;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &ifname, &ifname_len, &value) == FAILURE) {
                RETURN_NULL();
        }

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(PFSENSE_G(s), SIOCGIFCAP, (caddr_t)&ifr) < 0) {
		RETURN_NULL();
	}
	flags = ifr.ifr_curcap;
	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else
		flags |= value; 
	flags &= ifr.ifr_reqcap;
	ifr.ifr_reqcap = flags;
	if (ioctl(PFSENSE_G(s), SIOCSIFCAP, (caddr_t)&ifr) < 0)
		RETURN_NULL();
	RETURN_TRUE;

}

PHP_FUNCTION(pfSense_get_interface_info)
{
	struct ifaddrs *ifdata, *mb;
	struct if_data *tmpd;
        struct sockaddr_in *tmp;
        struct sockaddr_dl *tmpdl;
        char outputbuf[128];
        char *ifname;
        int ifname_len;
	struct pfr_buffer b;
	struct pfi_kif *p;
        int i = 0, error = 0;
	int dev;
	char *pf_status;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ifname, &ifname_len) == FAILURE) {
                RETURN_NULL();
        }

	if ((dev = open("/dev/pf", O_RDWR)) < 0)
		RETURN NULL;

        array_init(return_value);

        getifaddrs(&ifdata);
        if (ifdata == NULL) {
                //printf("No data found\n");
		RETURN NULL;
        }

        for(mb = ifdata; mb != NULL; mb = mb->ifa_next) {
                if (mb == NULL)
                        continue;
		if (ifname_len != strlen(mb->ifa_name))
                        continue;
                if (strncmp(ifname, mb->ifa_name, ifname_len) != 0)
                        continue;
                switch (mb->ifa_addr->sa_family) {
                case AF_LINK:

                        tmpd = (struct if_data *)mb->ifa_data;
			add_assoc_long(return_value, "inerrs", tmpd->ifi_ierrors);
			add_assoc_long(return_value, "outerrs", tmpd->ifi_oerrors);
			add_assoc_long(return_value, "collisions", tmpd->ifi_collisions);
			add_assoc_long(return_value, "inmcasts", tmpd->ifi_imcasts);
			add_assoc_long(return_value, "outmcasts", tmpd->ifi_omcasts);
			add_assoc_long(return_value, "unsuppproto", tmpd->ifi_noproto);
			add_assoc_long(return_value, "mtu", tmpd->ifi_mtu);

                        break;
                }
        }
        freeifaddrs(ifdata);

	bzero(&b, sizeof(b));
	b.pfrb_type = PFRB_IFACES;
	for (;;) {
		pfr_buf_grow(&b, b.pfrb_size);
		b.pfrb_size = b.pfrb_msize;
		if (pfi_get_ifaces(dev, ifname, b.pfrb_caddr, &b.pfrb_size)) {
			error = 1;
			break;
		}
		if (b.pfrb_size <= b.pfrb_msize)
			break;
		i++;
	}
	if (error == 0) {
                add_assoc_string(return_value, "interface", p->pfik_name, 1);

#define PAF_INET 0
#define PPF_IN 0
#define PPF_OUT 1
                add_assoc_long(return_value, "inpktspass", (unsigned long long)p->pfik_packets[PAF_INET][PPF_IN][PF_PASS]);
                add_assoc_long(return_value, "outpktspass", (unsigned long long)p->pfik_packets[PAF_INET][PPF_OUT][PF_PASS]);
                add_assoc_long(return_value, "inbytespass", (unsigned long long)p->pfik_bytes[PAF_INET][PPF_IN][PF_PASS]);
                add_assoc_long(return_value, "outbytespass", (unsigned long long)p->pfik_bytes[PAF_INET][PPF_OUT][PF_PASS]);

                add_assoc_long(return_value, "inpktsblock", (unsigned long long)p->pfik_packets[PAF_INET][PPF_IN][PF_DROP]);
                add_assoc_long(return_value, "outpktsblock", (unsigned long long)p->pfik_packets[PAF_INET][PPF_OUT][PF_DROP]);
                add_assoc_long(return_value, "inbytesblock", (unsigned long long)p->pfik_bytes[PAF_INET][PPF_IN][PF_DROP]);
                add_assoc_long(return_value, "outbytesblock", (unsigned long long)p->pfik_bytes[PAF_INET][PPF_OUT][PF_DROP]);

                add_assoc_long(return_value, "inbytes", (unsigned long long)(p->pfik_bytes[PAF_INET][PPF_IN][PF_DROP] + p->pfik_bytes[PAF_INET][PPF_IN][PF_PASS]));
                add_assoc_long(return_value, "outbytes", (unsigned long long)(p->pfik_bytes[PAF_INET][PPF_OUT][PF_DROP] + p->pfik_bytes[PAF_INET][PPF_OUT][PF_PASS]));
                add_assoc_long(return_value, "inpkts", (unsigned long long)(p->pfik_packets[PAF_INET][PPF_IN][PF_DROP] + p->pfik_packets[PAF_INET][PPF_IN][PF_PASS]));
                add_assoc_long(return_value, "outpkts", (unsigned long long)(p->pfik_packets[PAF_INET][PPF_OUT][PF_DROP] + p->pfik_packets[PAF_INET][PPF_OUT][PF_PASS]));
#undef PPF_IN
#undef PPF_OUT
#undef PAF_INET
        }
	pfr_buf_clear(&b);
	close(dev);
}

PHP_FUNCTION(pfSense_get_interface_stats)
{
	struct ifmibdata ifmd;
	struct if_data *tmpd;
        char outputbuf[128];
        char *ifname;
        int ifname_len, error;
	int name[6];
	size_t len;
	unsigned int ifidx;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &ifname, &ifname_len) == FAILURE) {
                RETURN_NULL();
        }

	ifidx = if_nametoindex(ifname);
	if (ifidx == 0)
		RETURN_NULL();

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[4] = ifidx;
	name[5] = IFDATA_GENERAL;

	len = sizeof(ifmd);

	if (sysctl(name, 6, &ifmd, &len, (void *)0, 0) < 0)
		RETURN_NULL();
	
	tmpd = &ifmd.ifmd_data;

        array_init(return_value);
	add_assoc_long(return_value, "inpkts", (long)tmpd->ifi_ipackets);
	add_assoc_long(return_value, "inbytes", (long)tmpd->ifi_ibytes);
	add_assoc_long(return_value, "outpkts", (long)tmpd->ifi_opackets);
	add_assoc_long(return_value, "outbytes", (long)tmpd->ifi_obytes);
	add_assoc_long(return_value, "inerrs", (long)tmpd->ifi_ierrors);
	add_assoc_long(return_value, "outerrs", (long)tmpd->ifi_oerrors);
	add_assoc_long(return_value, "collisions", (long)tmpd->ifi_collisions);
	add_assoc_long(return_value, "inmcasts", (long)tmpd->ifi_imcasts);
	add_assoc_long(return_value, "outmcasts", (long)tmpd->ifi_omcasts);
	add_assoc_long(return_value, "unsuppproto", (long)tmpd->ifi_noproto);
	add_assoc_long(return_value, "mtu", (long)tmpd->ifi_mtu);
}

PHP_FUNCTION(pfSense_get_pf_stats) {
	struct pf_status status;
	time_t runtime;
	unsigned sec, min, hrs, day = runtime;
	char statline[80];
	char buf[PF_MD5_DIGEST_LENGTH * 2 + 1];
	static const char hex[] = "0123456789abcdef";
	int i;
	int dev;

	array_init(return_value);

	if ((dev = open("/dev/pf", O_RDWR)) < 0) {
		add_assoc_string(return_value, "error", strerror(errno), 1);
	} else {


	bzero(&status, sizeof(status));
        if (ioctl(dev, DIOCGETSTATUS, &status)) {
		add_assoc_string(return_value, "error", strerror(errno), 1);
	} else {
                add_assoc_long(return_value, "rulesmatch", (unsigned long long)status.counters[PFRES_MATCH]);
                add_assoc_long(return_value, "pullhdrfail", (unsigned long long)status.counters[PFRES_BADOFF]);
                add_assoc_long(return_value, "fragments", (unsigned long long)status.counters[PFRES_FRAG]);
                add_assoc_long(return_value, "shortpacket", (unsigned long long)status.counters[PFRES_SHORT]);
                add_assoc_long(return_value, "normalizedrop", (unsigned long long)status.counters[PFRES_NORM]);
                add_assoc_long(return_value, "nomemory", (unsigned long long)status.counters[PFRES_MEMORY]);
                add_assoc_long(return_value, "badtimestamp", (unsigned long long)status.counters[PFRES_TS]);
                add_assoc_long(return_value, "congestion", (unsigned long long)status.counters[PFRES_CONGEST]);
                add_assoc_long(return_value, "ipoptions", (unsigned long long)status.counters[PFRES_IPOPTIONS]);
                add_assoc_long(return_value, "protocksumbad", (unsigned long long)status.counters[PFRES_PROTCKSUM]);
                add_assoc_long(return_value, "statesbad", (unsigned long long)status.counters[PFRES_BADSTATE]);
                add_assoc_long(return_value, "stateinsertions", (unsigned long long)status.counters[PFRES_STATEINS]);
                add_assoc_long(return_value, "maxstatesdrop", (unsigned long long)status.counters[PFRES_MAXSTATES]);
                add_assoc_long(return_value, "srclimitdrop", (unsigned long long)status.counters[PFRES_SRCLIMIT]);
                add_assoc_long(return_value, "synproxydrop", (unsigned long long)status.counters[PFRES_SYNPROXY]);

                add_assoc_long(return_value, "maxstatesreached", (unsigned long long)status.lcounters[LCNT_STATES]);
                add_assoc_long(return_value, "maxsrcstatesreached", (unsigned long long)status.lcounters[LCNT_SRCSTATES]);
                add_assoc_long(return_value, "maxsrcnodesreached", (unsigned long long)status.lcounters[LCNT_SRCNODES]);
                add_assoc_long(return_value, "maxsrcconnreached", (unsigned long long)status.lcounters[LCNT_SRCCONN]);
                add_assoc_long(return_value, "maxsrcconnratereached", (unsigned long long)status.lcounters[LCNT_SRCCONNRATE]);
                add_assoc_long(return_value, "overloadtable", (unsigned long long)status.lcounters[LCNT_OVERLOAD_TABLE]);
                add_assoc_long(return_value, "overloadflush", (unsigned long long)status.lcounters[LCNT_OVERLOAD_FLUSH]);

                add_assoc_long(return_value, "statesearch", (unsigned long long)status.fcounters[FCNT_STATE_SEARCH]);
                add_assoc_long(return_value, "stateinsert", (unsigned long long)status.fcounters[FCNT_STATE_INSERT]);
                add_assoc_long(return_value, "stateremovals", (unsigned long long)status.fcounters[FCNT_STATE_REMOVALS]);

                add_assoc_long(return_value, "srcnodessearch", (unsigned long long)status.scounters[SCNT_SRC_NODE_SEARCH]);
                add_assoc_long(return_value, "srcnodesinsert", (unsigned long long)status.scounters[SCNT_SRC_NODE_INSERT]);
                add_assoc_long(return_value, "srcnodesremovals", (unsigned long long)status.scounters[SCNT_SRC_NODE_REMOVALS]);

                add_assoc_long(return_value, "stateid", (unsigned long long)status.stateid);

                add_assoc_long(return_value, "running", status.running);
                add_assoc_long(return_value, "states", status.states);
                add_assoc_long(return_value, "srcnodes", status.src_nodes);

                add_assoc_long(return_value, "hostid", ntohl(status.hostid));
		for (i = 0; i < PF_MD5_DIGEST_LENGTH; i++) {
			buf[i + i] = hex[status.pf_chksum[i] >> 4];
			buf[i + i + 1] = hex[status.pf_chksum[i] & 0x0f];
		}
		buf[i + i] = '\0';
		add_assoc_string(return_value, "pfchecksum", buf, 1);
		printf("Checksum: 0x%s\n\n", buf);

		switch(status.debug) {
		case PF_DEBUG_NONE:
			add_assoc_string(return_value, "debuglevel", "none", 1);
			break;
		case PF_DEBUG_URGENT:
			add_assoc_string(return_value, "debuglevel", "urgent", 1);
			break;
		case PF_DEBUG_MISC:
			add_assoc_string(return_value, "debuglevel", "misc", 1);
			break;
		case PF_DEBUG_NOISY:
			add_assoc_string(return_value, "debuglevel", "noisy", 1);
			break;
		default:
			add_assoc_string(return_value, "debuglevel", "unknown", 1);
			break;
		}

		runtime = time(NULL) - status.since;
		if (status.since) {
			sec = day % 60;
			day /= 60;
			min = day % 60;
			day /= 60;
			hrs = day % 24;
			day /= 24;
			snprintf(statline, sizeof(statline),
		    		"Running: for %u days %.2u:%.2u:%.2u",
		    		day, hrs, min, sec);
			add_assoc_string(return_value, "uptime", statline, 1);
		}
	}
	close(dev);
	}
}

#define PATH_LOCKFILENAME     "/var/spool/lock/LCK..%s"
#define MAX_FILENAME              1000
#define MAX_OPEN_DELAY    2
#define MODEM_DEFAULT_SPEED              115200

static int
UuLock(const char *ttyname)
{
	//return 0;
	int   fd, pid;
	char  tbuf[sizeof(PATH_LOCKFILENAME) + MAX_FILENAME];
	char  pid_buf[64];

	snprintf(tbuf, sizeof(tbuf), PATH_LOCKFILENAME, ttyname);
	if ((fd = open(tbuf, O_RDWR|O_CREAT|O_EXCL, 0664)) < 0) {
		/* File is already locked; Check to see if the process
		 * holding the lock still exists */
		if ((fd = open(tbuf, O_RDWR, 0)) < 0)
			return(-1);

		if (read(fd, pid_buf, sizeof(pid_buf)) <= 0) {
			(void)close(fd);
			return(-1);
		}

		pid = atoi(pid_buf);

		if (kill(pid, 0) == 0 || errno != ESRCH) {
			(void)close(fd);  /* process is still running */
			return(-1);
		}

		/* The process that locked the file isn't running, so we'll lock it */
		if (lseek(fd, (off_t) 0, L_SET) < 0) {
			(void)close(fd);
			return(-1);
		}
	}

	/* Finish the locking process */
	sprintf(pid_buf, "%10u\n", (int) getpid());
	if (write(fd, pid_buf, strlen(pid_buf)) != strlen(pid_buf)) {
		(void)close(fd);
		(void)unlink(tbuf);
		return(-1);
	}

	(void)close(fd);
	return(0);
}

static int
UuUnlock(const char *ttyname)
{
	//return 0;
	char  tbuf[sizeof(PATH_LOCKFILENAME) + MAX_FILENAME];

	(void) sprintf(tbuf, PATH_LOCKFILENAME, ttyname);
	return(unlink(tbuf));
}

static int
ExclusiveOpenDevice(const char *pathname)
{
	int           fd, locked = 0;
	const char    *ttyname = NULL;
	time_t        startTime;

	/* Lock device UUCP style, if it resides in /dev */
	if (!strncmp(pathname, "/dev/", 5)) {
		ttyname = pathname + 5;
		if (UuLock(ttyname) < 0)
			return(-1);
		locked = 1;
	}

	/* Open it, but give up after so many interruptions */
	for (startTime = time(NULL);
	    (fd = open(pathname, O_RDWR, 0)) < 0
	    && time(NULL) < startTime + MAX_OPEN_DELAY; )
		if (errno != EINTR) {
			if (locked)
				UuUnlock(ttyname);
			return(-1);
		}

	/* Did we succeed? */
	if (fd < 0) {
		if (locked)
			UuUnlock(ttyname);
		return(-1);
	}
	//(void) fcntl(fd, F_SETFD, 1);

	/* Done */
	return(fd);
}

static void
ExclusiveCloseDevice(int fd, const char *pathname)
{
	int           rtn = -1;
	const char    *ttyname;
	time_t        startTime;

	/* Close file(s) */
	for (startTime = time(NULL);
	    time(NULL) < startTime + MAX_OPEN_DELAY &&
	    (rtn = close(fd)) < 0; )
		if (errno != EINTR)
			break;

	/* Did we succeed? */
	if ((rtn < 0) && (errno == EINTR))
		return;

	/* Remove lock */
	if (!strncmp(pathname, "/dev/", 5)) {
		ttyname = pathname + 5;
		UuUnlock(ttyname);
	}
}

PHP_FUNCTION(pfSense_sync) {
	sync();
}

PHP_FUNCTION(pfSense_get_modem_devices) {
	struct termios		attr;
	struct pollfd		pfd;
	glob_t			g;
	char			buf[2048] = { 0 };
	char			*path;
	int			nw = 0, i, fd;
	zend_bool		show_info = 0;
	int			poll_timeout = 700;

	if (ZEND_NUM_ARGS() > 2) {
		php_printf("Maximum two parameter can be passed\n");
		RETURN_NULL();
	}
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|bl", &show_info, &poll_timeout) == FAILURE) {
                RETURN_NULL();
        }

	array_init(return_value);

	bzero(&g, sizeof g);
	glob("/dev/cua*", 0, NULL, &g);
        glob("/dev/modem*", GLOB_APPEND, NULL, &g);

	if (g.gl_pathc > 0)
	for (i = 0; g.gl_pathv[i] != NULL; i++) {
		path = g.gl_pathv[i];
		if (strstr(path, "lock") || strstr(path, "init"))
			continue;
		if (show_info)
			php_printf("Found modem device: %s\n", path);
		/* Open & lock serial port */
		if ((fd = ExclusiveOpenDevice(path)) < 0) {
			if (show_info)
				php_printf("Could not open the device exlusively\n");
			add_assoc_string(return_value, path, path, 1);
			continue;
		}

		/* Set non-blocking I/O  */
		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
			goto errormodem;

		/* Set serial port raw mode, baud rate, hardware flow control, etc. */
		if (tcgetattr(fd, &attr) < 0)
			goto errormodem;

		cfmakeraw(&attr);

		attr.c_cflag &= ~(CSIZE|PARENB|PARODD);
		attr.c_cflag |= (CS8|CREAD|CLOCAL|HUPCL|CCTS_OFLOW|CRTS_IFLOW);
		attr.c_iflag &= ~(IXANY|IMAXBEL|ISTRIP|IXON|IXOFF|BRKINT|ICRNL|INLCR);
		attr.c_iflag |= (IGNBRK|IGNPAR);
		attr.c_oflag &= ~OPOST;
		attr.c_lflag = 0;

		cfsetspeed(&attr, (speed_t) MODEM_DEFAULT_SPEED);

		if (tcsetattr(fd, TCSANOW, &attr) < 0)
			goto errormodem;

		/* OK */
tryagain:
		if ((nw = write(fd, "AT OK\r\n", strlen("AT OK\r\n"))) < 0) {
			if (errno == EAGAIN) {
				if (show_info)
					php_printf("\tRetrying write\n");
				goto tryagain;
			}

			if (show_info)
				php_printf("\tError ocurred\n");
			goto errormodem;
		}

tryagain2:
		if (show_info)
			php_printf("\tTrying to read data\n");
		bzero(buf, sizeof buf);
		bzero(&pfd, sizeof pfd);
		pfd.fd = fd;
		pfd.events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;
		if ((nw = poll(&pfd, 1, poll_timeout)) > 0) {
			if ((nw = read(fd, buf, sizeof(buf))) < 0) {
				if (errno == EAGAIN) {
					if (show_info)
						php_printf("\tTrying again after errno = EAGAIN\n");
					goto tryagain2;
				}
				if (show_info)
					php_printf("\tError ocurred on 1st read\n");
				goto errormodem;
			}

			buf[2047] = '\0';
			if (show_info)
				php_printf("\tRead %s\n", buf);
			//if (strnstr(buf, "OK", sizeof(buf))) {
			if (nw > 0) {
				/*
				write(fd, "ATI3\r\n", strlen("ATI3\r\n"));
				bzero(buf, sizeof buf);
                		bzero(&pfd, sizeof pfd);
                		pfd.fd = fd;
                		pfd.events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLHUP;

                		if (poll(&pfd, 1, 200) > 0) {
					read(fd, buf, sizeof(buf));
				buf[2047] = '\0';
				if (show_info)
					php_printf("\tRead %s\n", buf);
				}
				*/
				add_assoc_string(return_value, path, path, 1);
			}
		} else if (show_info)
			php_printf("\ttimedout or interrupted: %d\n", nw);

errormodem:
		if (show_info)
			php_printf("\tClosing device %s\n", path);
		ExclusiveCloseDevice(fd, path);
	}
}

PHP_FUNCTION(pfSense_get_os_hw_data) {
	int i, mib[4], idata;
	size_t len;	
	char *data;

	array_init(return_value);

	mib[0] = CTL_HW;
	mib[1] = HW_MACHINE;
	if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
		data = malloc(len);
		if (data != NULL) {
			if (!sysctl(mib, 2, data, &len, NULL, 0)) {
				add_assoc_string(return_value, "hwmachine", data, 1);
				free(data);
			}
		}
	}

	mib[0] = CTL_HW;
        mib[1] = HW_MODEL;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "hwmodel", data, 1);
                                free(data);
                        }
		}
        }

	mib[0] = CTL_HW;
        mib[1] = HW_MACHINE_ARCH;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "hwarch", data, 1);
                                free(data);
                        }
                }
        }

	mib[0] = CTL_HW;
        mib[1] = HW_NCPU;
	len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
		add_assoc_long(return_value, "ncpus", idata);

	mib[0] = CTL_HW;
        mib[1] = HW_PHYSMEM;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "physmem", idata);

	mib[0] = CTL_HW;
        mib[1] = HW_USERMEM;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "usermem", idata);

	mib[0] = CTL_HW;
        mib[1] = HW_REALMEM;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "realmem", idata);
}

PHP_FUNCTION(pfSense_get_os_kern_data) {
        int i, mib[4], idata;
        size_t len;
        char *data;
	struct timeval bootime;

	array_init(return_value);

        mib[0] = CTL_KERN;
        mib[1] = KERN_HOSTUUID;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "hostuuid", data, 1);
                                free(data);
                        }
                }
        }

        mib[0] = CTL_KERN;
        mib[1] = KERN_HOSTNAME;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "hostname", data, 1);
                                free(data);
                        }
                }
        }

        mib[0] = CTL_KERN;
        mib[1] = KERN_OSRELEASE;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "osrelease", data, 1);
                                free(data);
                        }
                }
        }

	mib[0] = CTL_KERN;
        mib[1] = KERN_VERSION;
        if (!sysctl(mib, 2, NULL, &len, NULL, 0)) {
                data = malloc(len);
                if (data != NULL) {
                        if (!sysctl(mib, 2, data, &len, NULL, 0)) {
                                add_assoc_string(return_value, "oskernel_version", data, 1);
                                free(data);
                        }
                }
        }

        mib[0] = CTL_KERN;
        mib[1] = KERN_BOOTTIME;
        len = sizeof(bootime);
        if (!sysctl(mib, 2, &bootime, &len, NULL, 0))
                add_assoc_string(return_value, "boottime", ctime(&bootime.tv_sec), 1);

        mib[0] = CTL_KERN;
        mib[1] = KERN_HOSTID;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "hostid", idata);

        mib[0] = CTL_KERN;
        mib[1] = KERN_OSRELDATE;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "osreleasedate", idata);

	mib[0] = CTL_KERN;
        mib[1] = KERN_OSREV;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "osrevision", idata);

	mib[0] = CTL_KERN;
        mib[1] = KERN_SECURELVL;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "ossecurelevel", idata);

	mib[0] = CTL_KERN;
        mib[1] = KERN_OSRELDATE;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "osreleasedate", idata);

	mib[0] = CTL_KERN;
        mib[1] = KERN_OSRELDATE;
        len = sizeof(idata);
        if (!sysctl(mib, 2, &idata, &len, NULL, 0))
                add_assoc_long(return_value, "osreleasedate", idata);
}
