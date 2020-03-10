/* 
 *  File:   RTP_netdriver.h
 *  Author: Benoit BOUCHEZ (BEB)
 *
 *  Linux specific low level network functions for RTP-MIDI
 * 
 *  Created by Benoit BOUCHEZ on 05/12/11.
 *  Copyright 2011 Benoit Bouchez. All rights reserved.
 *
 *  Licensing terms
 *  This file and the rtpmidid project are licensed under GNU LGPL licensing 
 *  terms with an exception stating that rtpmidid code can be used within
 *  proprietary software products without needing to publish related product
 *  source code.
 */

#ifndef __RTP_NETDRIVER_H__
#define __RTP_NETDRIVER_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define INVALID_SOCKET			-1

typedef int TSOCKTYPE;

//! Makes system wait for x milliseconds
void SystemWaitMS (unsigned int MSTime);

//! Returns true if there is a datagram received on a socket and waiting to be read
bool DataAvail (TSOCKTYPE sock, unsigned int WaitTimeMS);

//! Create a UDP socket
bool CreateUDPSocket (TSOCKTYPE* sock, unsigned short NumPort, bool shouldReuse);

//! Close a socket created by CreateUDPSocket
void CloseSocket (TSOCKTYPE* sock);

#endif /* __RTP_NETDRIVER_H__ */

