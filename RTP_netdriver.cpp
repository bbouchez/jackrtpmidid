/* 
 *  File:   RTP_netdriver.cpp
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

#include "RTP_netdriver.h"
#include <unistd.h>

void SystemWaitMS (unsigned int MSTime)
{
    usleep(MSTime*1000);
}  // SystemWaitMS
//---------------------------------------------------------------------------

bool CreateUDPSocket (TSOCKTYPE* sock, unsigned short NumPort, bool shouldReuse)
// NumPort = port number to listen to
{
    long nRet;
    sockaddr_in6 AdrRecv;
    int optval;
    int errcode;

    // Create UDP/IP socket
    *sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (*sock==INVALID_SOCKET) return false;

    if (NumPort==0) return true;  // Do not bind the socket to a specific listening port
	
    if (shouldReuse)
    {
        optval=1;
	errcode=setsockopt(*sock, SOL_SOCKET, SO_REUSEPORT, (char*)&optval, sizeof(optval));
	if (errcode!=0)
	{
            CloseSocket(sock);
            return false;
	}
    }

    // Create local sink address
    memset (&AdrRecv, 0, sizeof(sockaddr_in6));
    AdrRecv.sin6_family=AF_INET6;
    AdrRecv.sin6_port=htons(NumPort);
    AdrRecv.sin6_addr=IN6ADDR_ANY_INIT;

    nRet=bind(*sock, (const sockaddr*)&AdrRecv, sizeof(AdrRecv));
    if (nRet==-1)
    {
        CloseSocket(sock);
        *sock=INVALID_SOCKET;
        return false;
    }

    return true;  // No error
}  // CreateUDPSocket
//---------------------------------------------------------------------------

void CloseSocket (TSOCKTYPE* sock)
{
    if (*sock!=INVALID_SOCKET)
    {
        shutdown (*sock, 2);
        close (*sock);
        *sock=INVALID_SOCKET;
    }
}  // CloseSocket
// ----------------------------------------------------------------------------------

bool DataAvail (TSOCKTYPE sock, unsigned int WaitTimeMS)
{
    fd_set readfds;
    timeval timeout;
    long result;

    if (sock==INVALID_SOCKET) return false;

    FD_ZERO(&readfds);
    FD_SET(sock,&readfds);

    timeout.tv_usec=WaitTimeMS*1000;
    timeout.tv_sec=0;

    result=select(sock+1, &readfds, 0, 0, &timeout);
    if (result<0) return false;		// An error occured while processing 
    if (result==0) return false;        // This is used when WaitTimeMS is used : it means that no data has arrived
    if (FD_ISSET(sock, &readfds)!=0) return true;

    return false;
}  // DataAvail
//---------------------------------------------------------------------------

bool are_ipv6_equal(struct in6_addr ipA, struct in6_addr ipB)
{
    for(int i = 0; i < 16; i++)
        if (ipA.s6_addr[i] != ipB.s6_addr[i])
            return false;
    return true;
}  // are_ipv6_equal
//---------------------------------------------------------------------------