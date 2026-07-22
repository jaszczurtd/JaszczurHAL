#
# Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
# SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#
# This file is part of the lwIP TCP/IP stack.
#
# Author: Adam Dunkels <adam@sics.se>
#

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=$(LWIPDIR)/core/init.c.upstream \
	$(LWIPDIR)/core/def.c.upstream \
	$(LWIPDIR)/core/dns.c.upstream \
	$(LWIPDIR)/core/inet_chksum.c.upstream \
	$(LWIPDIR)/core/ip.c.upstream \
	$(LWIPDIR)/core/mem.c.upstream \
	$(LWIPDIR)/core/memp.c.upstream \
	$(LWIPDIR)/core/netif.c.upstream \
	$(LWIPDIR)/core/pbuf.c.upstream \
	$(LWIPDIR)/core/raw.c.upstream \
	$(LWIPDIR)/core/stats.c.upstream \
	$(LWIPDIR)/core/sys.c.upstream \
	$(LWIPDIR)/core/altcp.c.upstream \
	$(LWIPDIR)/core/altcp_alloc.c.upstream \
	$(LWIPDIR)/core/altcp_tcp.c.upstream \
	$(LWIPDIR)/core/tcp.c.upstream \
	$(LWIPDIR)/core/tcp_in.c.upstream \
	$(LWIPDIR)/core/tcp_out.c.upstream \
	$(LWIPDIR)/core/timeouts.c.upstream \
	$(LWIPDIR)/core/udp.c.upstream

CORE4FILES=$(LWIPDIR)/core/ipv4/acd.c.upstream \
	$(LWIPDIR)/core/ipv4/autoip.c.upstream \
	$(LWIPDIR)/core/ipv4/dhcp.c.upstream \
	$(LWIPDIR)/core/ipv4/etharp.c.upstream \
	$(LWIPDIR)/core/ipv4/icmp.c.upstream \
	$(LWIPDIR)/core/ipv4/igmp.c.upstream \
	$(LWIPDIR)/core/ipv4/ip4_frag.c.upstream \
	$(LWIPDIR)/core/ipv4/ip4.c.upstream \
	$(LWIPDIR)/core/ipv4/ip4_addr.c.upstream

CORE6FILES=$(LWIPDIR)/core/ipv6/dhcp6.c.upstream \
	$(LWIPDIR)/core/ipv6/ethip6.c.upstream \
	$(LWIPDIR)/core/ipv6/icmp6.c.upstream \
	$(LWIPDIR)/core/ipv6/inet6.c.upstream \
	$(LWIPDIR)/core/ipv6/ip6.c.upstream \
	$(LWIPDIR)/core/ipv6/ip6_addr.c.upstream \
	$(LWIPDIR)/core/ipv6/ip6_frag.c.upstream \
	$(LWIPDIR)/core/ipv6/mld6.c.upstream \
	$(LWIPDIR)/core/ipv6/nd6.c.upstream

# APIFILES: The files which implement the sequential and socket APIs.
APIFILES=$(LWIPDIR)/api/api_lib.c.upstream \
	$(LWIPDIR)/api/api_msg.c.upstream \
	$(LWIPDIR)/api/err.c.upstream \
	$(LWIPDIR)/api/if_api.c.upstream \
	$(LWIPDIR)/api/netbuf.c.upstream \
	$(LWIPDIR)/api/netdb.c.upstream \
	$(LWIPDIR)/api/netifapi.c.upstream \
	$(LWIPDIR)/api/sockets.c.upstream \
	$(LWIPDIR)/api/tcpip.c.upstream

# NETIFFILES: Files implementing various generic network interface functions
NETIFFILES=$(LWIPDIR)/netif/ethernet.c.upstream \
	$(LWIPDIR)/netif/bridgeif.c.upstream \
	$(LWIPDIR)/netif/bridgeif_fdb.c.upstream \
	$(LWIPDIR)/netif/slipif.c.upstream

# SIXLOWPAN: 6LoWPAN
SIXLOWPAN=$(LWIPDIR)/netif/lowpan6_common.c.upstream \
        $(LWIPDIR)/netif/lowpan6.c.upstream \
	$(LWIPDIR)/netif/lowpan6_ble.c.upstream \
	$(LWIPDIR)/netif/zepif.c.upstream

# PPPFILES: PPP
PPPFILES=$(LWIPDIR)/netif/ppp/auth.c.upstream \
	$(LWIPDIR)/netif/ppp/ccp.c.upstream \
	$(LWIPDIR)/netif/ppp/chap-md5.c.upstream \
	$(LWIPDIR)/netif/ppp/chap_ms.c.upstream \
	$(LWIPDIR)/netif/ppp/chap-new.c.upstream \
	$(LWIPDIR)/netif/ppp/demand.c.upstream \
	$(LWIPDIR)/netif/ppp/eap.c.upstream \
	$(LWIPDIR)/netif/ppp/ecp.c.upstream \
	$(LWIPDIR)/netif/ppp/eui64.c.upstream \
	$(LWIPDIR)/netif/ppp/fsm.c.upstream \
	$(LWIPDIR)/netif/ppp/ipcp.c.upstream \
	$(LWIPDIR)/netif/ppp/ipv6cp.c.upstream \
	$(LWIPDIR)/netif/ppp/lcp.c.upstream \
	$(LWIPDIR)/netif/ppp/magic.c.upstream \
	$(LWIPDIR)/netif/ppp/mppe.c.upstream \
	$(LWIPDIR)/netif/ppp/multilink.c.upstream \
	$(LWIPDIR)/netif/ppp/ppp.c.upstream \
	$(LWIPDIR)/netif/ppp/pppapi.c.upstream \
	$(LWIPDIR)/netif/ppp/pppcrypt.c.upstream \
	$(LWIPDIR)/netif/ppp/pppoe.c.upstream \
	$(LWIPDIR)/netif/ppp/pppol2tp.c.upstream \
	$(LWIPDIR)/netif/ppp/pppos.c.upstream \
	$(LWIPDIR)/netif/ppp/upap.c.upstream \
	$(LWIPDIR)/netif/ppp/utils.c.upstream \
	$(LWIPDIR)/netif/ppp/vj.c.upstream \
	$(LWIPDIR)/netif/ppp/polarssl/arc4.c.upstream \
	$(LWIPDIR)/netif/ppp/polarssl/des.c.upstream \
	$(LWIPDIR)/netif/ppp/polarssl/md4.c.upstream \
	$(LWIPDIR)/netif/ppp/polarssl/md5.c.upstream \
	$(LWIPDIR)/netif/ppp/polarssl/sha1.c.upstream

# LWIPNOAPPSFILES: All LWIP files without apps
LWIPNOAPPSFILES=$(COREFILES) \
	$(CORE4FILES) \
	$(CORE6FILES) \
	$(APIFILES) \
	$(NETIFFILES) \
	$(PPPFILES) \
	$(SIXLOWPAN)

# SNMPFILES: SNMPv2c agent
SNMPFILES=$(LWIPDIR)/apps/snmp/snmp_asn1.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_core.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_icmp.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_interfaces.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_ip.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_snmp.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_system.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_tcp.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_mib2_udp.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_snmpv2_framework.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_snmpv2_usm.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_msg.c.upstream \
	$(LWIPDIR)/apps/snmp/snmpv3.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_netconn.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_pbuf_stream.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_raw.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_scalar.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_table.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_threadsync.c.upstream \
	$(LWIPDIR)/apps/snmp/snmp_traps.c.upstream

# HTTPFILES: HTTP server + client
HTTPFILES=$(LWIPDIR)/apps/http/altcp_proxyconnect.c.upstream \
	$(LWIPDIR)/apps/http/fs.c.upstream \
	$(LWIPDIR)/apps/http/http_client.c.upstream \
	$(LWIPDIR)/apps/http/httpd.c.upstream

# MAKEFSDATA: MAKEFSDATA HTTP server host utility
MAKEFSDATAFILES=$(LWIPDIR)/apps/http/makefsdata/makefsdata.c.upstream

# LWIPERFFILES: IPERF server
LWIPERFFILES=$(LWIPDIR)/apps/lwiperf/lwiperf.c.upstream

# SMTPFILES: SMTP client
SMTPFILES=$(LWIPDIR)/apps/smtp/smtp.c.upstream

# SNTPFILES: SNTP client
SNTPFILES=$(LWIPDIR)/apps/sntp/sntp.c.upstream

# MDNSFILES: MDNS responder
MDNSFILES=$(LWIPDIR)/apps/mdns/mdns.c.upstream \
	$(LWIPDIR)/apps/mdns/mdns_out.c.upstream \
	$(LWIPDIR)/apps/mdns/mdns_domain.c.upstream

# NETBIOSNSFILES: NetBIOS name server
NETBIOSNSFILES=$(LWIPDIR)/apps/netbiosns/netbiosns.c.upstream

# TFTPFILES: TFTP client/server files
TFTPFILES=$(LWIPDIR)/apps/tftp/tftp.c.upstream

# MQTTFILES: MQTT client files
MQTTFILES=$(LWIPDIR)/apps/mqtt/mqtt.c.upstream

# MBEDTLS_FILES: MBEDTLS related files of lwIP rep
MBEDTLS_FILES=$(LWIPDIR)/apps/altcp_tls/altcp_tls_mbedtls.c.upstream \
	$(LWIPDIR)/apps/altcp_tls/altcp_tls_mbedtls_mem.c.upstream \
	$(LWIPDIR)/apps/snmp/snmpv3_mbedtls.c.upstream

# LWIPAPPFILES: All LWIP APPs
LWIPAPPFILES=$(SNMPFILES) \
	$(HTTPFILES) \
	$(LWIPERFFILES) \
	$(SMTPFILES) \
	$(SNTPFILES) \
	$(MDNSFILES) \
	$(NETBIOSNSFILES) \
	$(TFTPFILES) \
	$(MQTTFILES) \
	$(MBEDTLS_FILES)
