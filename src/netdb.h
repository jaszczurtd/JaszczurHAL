#pragma once

/**
 * @file netdb.h
 * @brief Minimal IPv4 getaddrinfo declarations for JaszczurHAL BSD sockets.
 */

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AI_PASSIVE
#define AI_PASSIVE 0x01
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME 0x02
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x04
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0x20
#endif
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x400
#endif

#ifndef EAI_BADFLAGS
#define EAI_BADFLAGS -1
#endif
#ifndef EAI_NONAME
#define EAI_NONAME -2
#endif
#ifndef EAI_AGAIN
#define EAI_AGAIN -3
#endif
#ifndef EAI_FAIL
#define EAI_FAIL -4
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY -6
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY -10
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE -8
#endif
#ifndef EAI_SOCKTYPE
#define EAI_SOCKTYPE -7
#endif
#ifndef EAI_SYSTEM
#define EAI_SYSTEM -11
#endif

struct addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  socklen_t ai_addrlen;
  struct sockaddr *ai_addr;
  char *ai_canonname;
  struct addrinfo *ai_next;
};

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);

#ifdef __cplusplus
}
#endif
