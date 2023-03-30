#ifndef CHAIOS_LWIP_OPTS_H
#define CHAIOS_LWIP_OPTS_H

#include <chaikrnl.h>
#include <scheduler.h>

//#define LWIP_NETCONN 0
#define LWIP_PROVIDE_ERRNO 1
#define LWIP_NETCONN 1
#define LWIP_NETIF_API 1
#define LWIP_TCPIP_CORE_LOCKING 0
#define LWIP_NETIF_LINK_CALLBACK  1
#define TCPIP_THREAD_PRIO THREAD_PRIORITY_NORMAL
#define DEFAULT_THREAD_PRIO THREAD_PRIORITY_NORMAL
#define IP_FORWARD 1
#define LWIP_IPV6 1
#define LWIP_IPV6_FORWARD 1
#define ETHARP_TCPIP_ETHINPUT 1
#define LWIP_DHCP 1

//#define DHCP_DEBUG LWIP_DBG_ON
//#define API_LIB_DEBUG LWIP_DBG_ON
//#define IP_DEBUG LWIP_DBG_ON
//#define UDP_DEBUG LWIP_DBG_ON
//#define NETIF_DEBUG LWIP_DBG_ON

//This can be zero on 32 bit
#define IPV6_FRAG_COPYHEADER 1

#define MEM_LIBC_MALLOC 1
#define mem_clib_malloc kmalloc
#define mem_clib_free kfree
#define mem_clib_calloc kcalloc


#define LWIP_NETIF_HOSTNAME 1

#ifdef LWIP_DLL
#define CHAILWIP_FUNC DLLIMPORT
#else
#define CHAILWIP_FUNC DLLEXPORT
#endif

#define LWIP_DEBUG 1
//#define ETHARP_DEBUG LWIP_DBG_ON
//#define MEM_DEBUG LWIP_DBG_ON
//#define PBUF_DEBUG LWIP_DBG_ON
#endif