/*
 *  Network.cpp
 *  Cross Platform SDK
 *  Low level network access functions
 *
 *  Created by Benoit BOUCHEZ (BEB)
 *
 *  Copyright Benoit BOUCHEZ
 *
 *  License
 *  This file is licensed under GNU LGPL licensing terms
 *  terms with an exception stating that rtpmidid code can be used within
 *  proprietary software products without needing to publish related product
 *  source code.
 *
 */
//---------------------------------------------------------------------------

/* Release notes
 02/01/2010:
 - inclusion of release notes
 - integration with XPlatformUtils
 - bug corrected in WaitAnswer (division of timeout by 100)

 26/01/2010:
 - header files inclusion modified for Mac (moved from network.h to network.cpp)

 25/02/2010
 - function CreateUDPSocket : NumPort=0 deactivates the binding mechanism

 02/06/2011
 - bug corrected in ConnectTCPSocket for Mac : select was not transmitting the number of sockets to check (but *sock+1....)
 - same bug corrected in DataAvail function for Mac

04/08/2011
 - select() function call on Mac (in ConnectTCPSocket and DataAvail) corrected to use FD_ISSET, not the function result (function result just says
  how much descriptor were set, and if there was an error. Unix guides recommend to use FD_ISSET

14/09/2011
 - socket is now set to invalid if bind is not sucessful and closed in CreateUDPSocket
 - added support to reuse a socket port in CreateUDPSocket

03/10/2011
  - bug corrected in dataAvail for Mac (protection code from Windows copied to Mac platform) : if socket is invalid, the socket descriptor is null (BAD_ACCESS in FD_SET macro)

10/10/2011
 - bug corrected in ConnectTCPSocket for Mac (similar error than dataAvail to check if socket is writeable - see change 04/08/2011)

28/10/2011
 - code modified in ConnectTCPSocket to allow compilation on Windows target (change from 10/10/2011 makes one undefined variable for Windows)
 - REUSE_ADDR option correctly activated for Windows platform in CreateUDPSocket

21/01/2012
 - in ConnectTCPSocket for Mac, first parameter of select() set to 1, rather than TCPSocket+1 (otherwise, the socket says that it is writeable and makes a SIGPIPE exception !)
 Tested on 14/02/2012 with KissBox Editor : does not work properly (socket is never seen as opened). Code reverted to TCPSocket+1 : no crash anymore

03/07/2012
 - DataAvail function modified after checking various example on select function
	- FD_ISSET macro is now used to detect if a socket has received something
	- timeout option is now available (in ms) after detecting that the KissBox Editor was not behaving properly in the WaitAnswer function
	- code rearranged more properly

12/08/2012
 - ConnectTCPSocket modified for Mac : if the TCP socket fails (device exists but does not accept the connection), a SIGPIPE signal is triggered (normal behaviour on Mac)
 Based on "Using TCP with sockets" document from David Mazières, two things must be done :
	- ignore the SIGPIPE signal (such a signal kills the process !)

07/10/2012
 - added #include <signal.h> in .cpp file to prevent errors in old projects

 12/03/2020
 - added support for LINUX (using __TARGET_LINUX__ define)

 03/06/2023
 - evolution to Winsock2 (includes winsock2.h and link with ws2_32.lib). If __USE_WINSOCK__ is defined, library is linked to "old" winsock

 19/07/2023
  - removed functions used only by KissBox Editor to make this module available as open source
 */

#ifdef __BORLANDC__
#include <vcl.h>
#pragma hdrstop

#pragma package(smart_init)
#endif

#include "network.h"
#include "XPlatformUtils.h"
#ifdef __TARGET_MAC__
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#endif
#ifdef __TARGET_LINUX__
#include <unistd.h>
#endif

#include <stdio.h>

#define REQUESTED_WINSOCK_VER	0x101

#if defined (__TARGET_WIN__)
bool OpenNetwork (void)
// Initialize Winsock and check version
{
	int err;
	WSADATA wsaData;

	memset(&wsaData, 0, sizeof(WSAData));
	err=WSAStartup(REQUESTED_WINSOCK_VER, &wsaData);
	if (err!=0) return false;
	if (wsaData.wVersion==REQUESTED_WINSOCK_VER) return true;
	else
	{
		WSACleanup();
		return false;
	}
}  // OpenNetwork
//---------------------------------------------------------------------------

void CloseNetwork (void)
// Free all Windows socket ressources allocated to the application
{
	WSACleanup();
}  // CloseNetwork
//---------------------------------------------------------------------------
#endif

bool CreateUDPSocket (TSOCKTYPE* sock, unsigned short NumPort, bool shouldReuse)
// NumPort = port number to listen to
{
	long nRet;
	sockaddr_in AdrRecv;
	int optval;
	int errcode;

	// Create UDP/IP socket
	*sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // IPPROTO_UDP is 0 in IPTCOM
	if (*sock==INVALID_SOCKET) return false;

	if (NumPort==0) return true;  // Do not bind the socket to a specific listening port

	if (shouldReuse)
	{
		optval=1;
#if defined (__TARGET_MAC__)
		errcode=setsockopt(*sock, SOL_SOCKET, SO_REUSEPORT, (char*)&optval, sizeof(optval));
		if (errcode!=0)
		{
			CloseSocket(sock);
			return false;
		}
#endif
#if defined (__TARGET_WIN__)
		errcode=setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
		if (errcode<0)
		{
			CloseSocket(sock);
			return false;
		}
#endif
#if defined (__TARGET_LINUX__)
		errcode=setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
		if (errcode<0)
		{
			CloseSocket(sock);
			return false;
		}
#endif
	}

	// Create local sink address
	memset (&AdrRecv, 0, sizeof(sockaddr_in));
	AdrRecv.sin_family=AF_INET;
	AdrRecv.sin_port=htons(NumPort);
	AdrRecv.sin_addr.s_addr=htonl(INADDR_ANY);

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

#if defined (__TARGET_WIN__)
bool ConnectSocket (TSOCKTYPE* sock, unsigned short NumPort, unsigned long IPAddr, HWND hwnd, unsigned int MsgId, unsigned int TempoConnect)
// Try to establish socket between editor and KissBox
// hwnd=0 et MsgId=0 : using blocking mode
{
  unsigned long nRet;
  sockaddr_in saServer;

  // Create TCP/IP socket
  *sock=socket(AF_INET, SOCK_STREAM, 0);
  if (*sock==INVALID_SOCKET) return false;

  // Init server address
  memset (&saServer, 0, sizeof(sockaddr_in));
  saServer.sin_family=AF_INET;
  saServer.sin_port=htons(NumPort);
  saServer.sin_addr.s_addr=htonl(IPAddr);

  if ((hwnd!=0)&&(MsgId!=0)) {
    // Trying to use asynchronous mode
	nRet=WSAAsyncSelect(*sock, hwnd, MsgId, FD_CONNECT);
    if (nRet==(unsigned long)SOCKET_ERROR) {
      closesocket(*sock);
      *sock=INVALID_SOCKET;
      return false;
    }
  }

  // Try to connect
  nRet=connect(*sock, (const struct sockaddr*)&saServer, sizeof(saServer));
  Sleep(TempoConnect);
  if (nRet==INVALID_SOCKET){
  	closesocket(*sock);
    *sock=INVALID_SOCKET;
  	return false;
  }

  return true;
}  // ConnectSocket
//---------------------------------------------------------------------------
#endif

bool ConnectTCPSocket (TSOCKTYPE* sock, unsigned short NumPort, unsigned long IPAddr, unsigned int TimeOut)
{
  struct sockaddr_in saServer;
  int nRet;
  unsigned int TimeCount;
  fd_set Writefds;
  timeval timeout;
#if defined (__TARGET_WIN__)
  unsigned long Mode;
#endif
#if defined (__TARGET_MAC__)
	int Flags;
	TSOCKTYPE	TCPSocket;
	struct sockaddr_storage peer_addr;
	socklen_t len;
	int OpenResult;
#endif
	bool SocketWriteable;

// Value for time out is not in milliseconds on WIN platform
// We scale it to get approx. same results of time on both MAC and WIN
#if defined (__TARGET_WIN__)
	TimeOut=TimeOut/10;
#endif

	// Create TCP socket
	*sock=socket(AF_INET, SOCK_STREAM, 0);
	if (*sock==INVALID_SOCKET) return false;

	//Init server address (distant address)
	memset (&saServer, 0, sizeof(saServer));
	saServer.sin_family=AF_INET;
	saServer.sin_port=htons(NumPort);
	saServer.sin_addr.s_addr=htonl(IPAddr);

#if defined (__TARGET_MAC__)
	// Declare socket as non blocking
	Flags=fcntl(*sock, F_GETFL, 0);
	Flags|=O_NONBLOCK;
	fcntl(*sock, F_SETFL, Flags);
#endif
#if defined (__TARGET_WIN__)
	Mode=1;  // 0 : blocking, 1 : non blocking
	nRet=ioctlsocket(*sock, FIONBIO, &Mode);
	if (nRet!=0)
	{
		CloseSocket(sock);
		return false;
	}
#endif

	// Disable Nagle algorithm
	// Don't care about result. If Nagle is not disabled, KissBox Editor will work however
	//nRet = setsockopt(*sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));

	// Try to connect to server
	nRet=connect(*sock, (const struct sockaddr*)&saServer, sizeof(saServer));
	/* NOTE
	 On Mac, we get a -1 return value (error). errno indicates then a EINPROGRESS (36)
	 */

  // Wait for connection (check if socket becomes writable before timeout)
#if defined (__TARGET_MAC__)
	TCPSocket=*sock;
#endif
	SocketWriteable=false;
	TimeCount=0;
	do
	{
#if defined (__TARGET_MAC__)
		FD_ZERO(&Writefds);
		FD_SET(TCPSocket, &Writefds);

		timeout.tv_usec=0;
		timeout.tv_sec=0;

		nRet=select(TCPSocket+1, 0, &Writefds, 0, &timeout);
		//nRet=select(1, 0, &Writefds, 0, &timeout);		// Is it 1 or TCPSocket+1 ?????
		usleep (1000);
		if (nRet>=1)
			SocketWriteable=true;
#endif
#if defined (__TARGET_WIN__)
		timeout.tv_sec=0;
		timeout.tv_usec=0;

		Writefds.fd_count=1;
		Writefds.fd_array[0]=*sock;

		nRet=select(0, 0, &Writefds, 0, &timeout);
		Sleep (1);
		if (nRet!=0) SocketWriteable=true;
#endif
		TimeCount++;
	} while ((TimeCount<TimeOut)&&(SocketWriteable==false));

#if defined (__TARGET_MAC__)
	// In case the socket becomes writeable, we must check that it can accept data to send to avoid false SIGPIPE signal

	signal(SIGPIPE, SIG_IGN);		// Ignore the SIGPIPE signal, otherwise it kills the process if an attempt to write on the socket is done while connection is being established
	// In fact, the socket becomes writeable as soon as there is device responding on the other side, even if the socket is not completely opened

	len = sizeof (peer_addr);
	OpenResult=getpeername(TCPSocket, (struct sockaddr*)&peer_addr, &len);
	if (OpenResult==-1)
	{
		CloseSocket(sock);
		return false;		// Socket is not writeable
	}
#endif

	if (TimeCount>=TimeOut)
	{
		CloseSocket(sock);
		return false;
	}

	return true;
}  // ConnectTCPSocket
// ----------------------------------------------------------------------------------

bool DataAvail (TSOCKTYPE sock, unsigned int WaitTimeMS)
{
  fd_set readfds;
  timeval timeout;
  long result;

#if defined (__TARGET_WIN__)
  if (sock==INVALID_SOCKET) return false;

  timeout.tv_sec=0;  // Non blocking mode (select will return immediately)
  timeout.tv_usec=WaitTimeMS*1000;

  readfds.fd_count=1;
  readfds.fd_array[0]=sock;

  result=select(0, &readfds, 0, 0, &timeout);
  if (result>=1) return true;
#endif

#if defined (__TARGET_MAC__)
	if (sock==INVALID_SOCKET) return false;

	FD_ZERO(&readfds);
	FD_SET(sock,&readfds);

	timeout.tv_usec=WaitTimeMS*1000;
	timeout.tv_sec=0;

	result=select(sock+1, &readfds, 0, 0, &timeout);
	if (result<0) return false;		// An error occured while processing
	if (result==0) return false;	// This is used when WaitTimeMS is used : it means that no data has arrived
	//if (result==1) return true;
	if (FD_ISSET(sock, &readfds)!=0) return true;
#endif

#if defined (__TARGET_LINUX__)
    if (sock==INVALID_SOCKET) return false;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    timeout.tv_usec=WaitTimeMS*1000;
    timeout.tv_sec=0;

    result=select(sock+1, &readfds, 0, 0, &timeout);
    if (result<0) return false;     // An error occured while processing
    if (result==0) return false;    // When WaitTimeMS is used : no data arrived before timeout
    if (FD_ISSET(sock, &readfds)!=0) return true;
#endif

	return false;
}  // DataAvail
//---------------------------------------------------------------------------

void CloseSocket (TSOCKTYPE* sock)
{
  if (*sock!=INVALID_SOCKET)
  {
    shutdown (*sock, 2);
#if defined (__TARGET_WIN__)
    closesocket(*sock);
#endif
#if defined (__TARGET_MAC__)
    close (*sock);
#endif
#if defined (__TARGET_LINUX__)
    close (*sock);
#endif
    *sock=INVALID_SOCKET;
  }
}  // CloseSocket
// ----------------------------------------------------------------------------------



