/*
 *  Network.h
 *  Cross Platform SDK
 *  Low level network access functions
 *
 *  Created by Benoit BOUCHEZ (BEB)
 *
 *  Copyright Benoit BOUCHEZ.
 *
 *  License
 *  This file is licensed under GNU LGPL licensing terms
 *  with an exception stating that rtpmidid code can be used within
 *  proprietary software products without needing to publish related product
 *  source code.
 *
 */
//---------------------------------------------------------------------------

#ifndef __network_H__
#define __network_H__

#if defined (__TARGET_MAC__)
#include <sys/socket.h>
#include <netinet/in.h>
#define INVALID_SOCKET			-1
#endif

#ifdef __TARGET_LINUX__
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#define INVALID_SOCKET          -1
#endif

#if defined (__TARGET_WIN__)
// If winsock.h is used, link with wsock32.lib
// If winsock2.h i used, link with ws2_32.lib
#ifdef __USE_WINSOCK__
#include <winsock.h>
#else
#include <winsock2.h>
#endif
#endif

#if defined (__TARGET_WIN__)
typedef SOCKET TSOCKTYPE;
#endif
#if defined (__TARGET_MAC__)
typedef int TSOCKTYPE;
#endif
#if defined (__TARGET_LINUX__)
typedef int TSOCKTYPE;
#endif

#if defined (__TARGET_WIN__)
bool OpenNetwork (void);
void CloseNetwork (void);
#endif
bool CreateUDPSocket (TSOCKTYPE* sock, unsigned short NumPort, bool shouldReuse);
#if defined (__TARGET_WIN__)
bool ConnectSocket (TSOCKTYPE* sock, unsigned short NumPort, unsigned long IPAddr, HWND hwnd, unsigned int MsgId, unsigned int TempoConnect);
#endif
bool ConnectTCPSocket (TSOCKTYPE* sock, unsigned short NumPort, unsigned long IPAddr, unsigned int TimeOut);
bool DataAvail (TSOCKTYPE sock, unsigned int WaitTimeMS);
void CloseSocket (TSOCKTYPE* sock);

#endif
