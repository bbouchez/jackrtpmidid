/*
 *  RTP_MIDI.cpp
 *  Generic class for RTP_MIDI session initiator/listener
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

/*
 Release notes

 7/12/2013
 - all long changed to int. Long prevents correct working on 64 bits platform

 03/03/2014
 - modication in RunSession : messages are accepted if they come from associated remote address or if remote address is 0 (used for session listener mode)

 04/04/2014
 - modifications to allow usage with iOS sessions (session names, higher number of sync phases at startup, etc..) after debugging with iOS devices

 03/10/2021
  - two bugs corrected ('=' in place of '==' in tests)
  - removed everything specific to VST plugins
  - removed KissBox hardware identification (useless outside of VSTizers)

10/10/2021
  - added SendMIDIMessage method for mutliple thread support
  - removed parameters for RunSession as it conflicts with the internal buffering model for possible multithreading
  - bug corrected : missing reload of TimeOutRemote in RunSession when we are session initiator

10/04/2023
  - Added automatic port numbering (N+1) when local port is set to 0 (rather two random port numbers) in InitiateSession
  - Remove LocalCtrlPort and LocalDataPort members (not used anywhere)
  
24/06/2023
  - Moved TS1L, TS1H, TS2L, TS2H, TS3L, TS3H from stack to class members (SessionState==SESSION_CLOCK_SYNC2 uses them after they have defined from a previous realtime call)
 */

#include "RTP_MIDI.h"
#include <string.h>
#include <stdlib.h>
#include "XPlatformUtils.h"
#ifdef SHOW_RTP_INFO
#include <stdio.h>
#endif
#if defined (__TARGET_WIN__)
#include <winsock.h>
#include "SystemMessageBox.h"
#endif
#ifdef __TARGET_MAC__
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#endif

CRTP_MIDI::CRTP_MIDI(unsigned int SYXInSize, TRTPMIDIDataCallback CallbackFunc, void* UserInstance)
{
	RemoteIP=DEFAULT_RTP_ADDRESS;
	RemoteControl=DEFAULT_RTP_CTRL_PORT;
	RemoteData=DEFAULT_RTP_DATA_PORT;

	DataSocket=INVALID_SOCKET;
	ControlSocket=INVALID_SOCKET;
	SessionState=SESSION_CLOSED;

    InvitationOnCtrlSenderIP=0;      // IP address of sender of invitation received on control port
    InvitationOnDataSenderIP=0;      // IP address of sender of invitation received on data port
    SessionPartnerIP=0;
	strcpy ((char*)&this->SessionName[0], "");

	SocketLocked=true;
	TimerRunning=false;
	TimerEvent=false;
	EventTime=0;

	InviteCount=0;
	TimeCounter=0;
	SyncSequenceCounter=0;
	MeasuredLatency = 0xFFFFFFFF;		// Mark as latency not known for now

	IsInitiatorNode=true;
	TimeOutRemote=4;
	ConnectionLost = false;
	PeerClosedSession = false;

	InterFragmentTimer=0;
	TransmittedSYSEXInFragment=0;

	InSYSEXBufferSize=SYXInSize;
	InSYSEXBuffer=new unsigned char [InSYSEXBufferSize];

	RTPStreamQueue.ReadPtr = 0;
	RTPStreamQueue.WritePtr = 0;

	initRTP_SYSEXBuffer();

	RTPCallback=CallbackFunc;
	ClientInstance=UserInstance;
}  // CRTP_MIDI::CRTP_MIDI
//---------------------------------------------------------------------------

CRTP_MIDI::~CRTP_MIDI(void)
{
	CloseSession();
	CloseSockets();

	if (InSYSEXBuffer!=0) delete InSYSEXBuffer;
}  // CRTP_MIDI::~CRTP_MIDI
//---------------------------------------------------------------------------

void CRTP_MIDI::CloseSockets(void)
{
	// Close the UDP sockets
	if (ControlSocket!=INVALID_SOCKET)
		CloseSocket(&ControlSocket);
	if (DataSocket!=INVALID_SOCKET)
		CloseSocket(&DataSocket);
}  // CRTP_MIDI::CloseSockets
//---------------------------------------------------------------------------

void CRTP_MIDI::PrepareTimerEvent (unsigned int TimeToWait)
{
	TimerRunning=false;		// Lock the timer until preparation is done
	TimerEvent=false;			// Signal no event
	EventTime=TimeToWait;
	TimerRunning=true;		// Restart the timer
}  // CRTP_MIDI::PrepareTimerEvent
//---------------------------------------------------------------------------

int CRTP_MIDI::InitiateSession(unsigned int DestIP,
							   unsigned short DestCtrlPort,
							   unsigned short DestDataPort,
							   unsigned short LocalCtrlPort,
							   unsigned short LocalDataPort,
							   bool IsInitiator)
{
    int CreateError=0;
	bool SocketOK;

	RemoteIP=DestIP;
	RemoteControl=DestCtrlPort;
	RemoteData=DestDataPort;

	Token=rand()*0xFFFFFFFF;
	SSRC=rand()*0xFFFFFFFF;
	RTPSequence=0;
	LastRTPCounter=0;
	LastFeedbackCounter=0;
	SyncSequenceCounter=0;

	// Close the control and data sockets, just in case...
	CloseSockets();

	// Open the two UDP sockets (we let the OS give us the local port number)
	SocketOK=CreateUDPSocket (&ControlSocket, LocalCtrlPort, false);
	if (SocketOK==false) CreateError=-1;
	SocketOK=CreateUDPSocket (&DataSocket, LocalDataPort, false);
	if (SocketOK==false) CreateError=-2;

    if (CreateError!=0)
	{
		CloseSockets();
	}
	else
	{
		// Sockets are opened, we start the session
		SYSEX_RTPActif=false;
		SegmentSYSEXInput=false;
		ConnectionLost = false;
		InviteCount=0;
		TimeOutRemote=16;		// 120 seconds -> Five sync sequences every 1.5 seconds then sync sequence every 10 seconds = 11 + 5
		IncomingThirdByte=false;
		IsInitiatorNode=IsInitiator;
		if (IsInitiator==false)
		{  // Do not invite, wait from remote node to start session
			SessionState=SESSION_WAIT_INVITE;
		}
		else
		{ // Initiate session by inviting remote node
			SessionState=SESSION_INVITE_CONTROL;
            SessionPartnerIP=RemoteIP;
		}
		SocketLocked=false;		// Must be last instruction after session initialization
		PrepareTimerEvent(1000);
	}

	return CreateError;
}  // CRTP_MIDI::InitiateSession
//---------------------------------------------------------------------------

void CRTP_MIDI::CloseSession (void)
{
	if (SessionState==SESSION_OPENED)
	{
		SessionState=SESSION_CLOSED;
		SendBYCommand();
		SystemWaitMS(50);		// Give time to send the message
	}
}  // CRTP_MIDI::CloseSession
//---------------------------------------------------------------------------

void CRTP_MIDI::RunSession(void)
{
	int RecvSize;
	int nRet;
#if defined (__TARGET_MAC__)
  socklen_t fromlen;
#endif
#if defined (__TARGET_LINUX__)
  socklen_t fromlen;
#endif
#if defined (__TARGET_WIN__)
  int fromlen;
#endif
	sockaddr_in SenderData;
	unsigned char ReceptionBuffer[1024];
	bool InvitationReceivedOnCtrl;
	bool InvitationReceivedOnData;
	bool InvitationAcceptedOnCtrl;
	bool InvitationAcceptedOnData;
	bool InvitationRefusedOnCtrl;
	bool InvitationRefusedOnData;

	bool ClockSync0Received;
	bool ClockSync1Received;
	bool ClockSync2Received;

	bool ByeReceivedOnCtrl;
	bool ByeReceivedOnData;

	TSessionPacket* SessionPacket;
	TSyncPacket* SyncPacket;

	TLongMIDIRTPMsg LRTPMessage;
	sockaddr_in AdrEmit;
	int RTPOutSize;

	// Computing time using the thread is not perfect, we should use OS time related data
	// timeGetTime can be used on Windows, but no direct equivalent in Mac
	// TODO : enhance implementation using system time
	TimeCounter+=10;
    LocalClock+=10;

	// Do not process if communication layers are not ready
	if (SocketLocked) return;

	// Check if timer elapsed
	if (TimerRunning)
	{
		if (EventTime>0)
			EventTime--;
		if (EventTime==0)
		{
			TimerRunning=false;
			TimerEvent=true;
		}
	}

	// If no resync from remote node after 2 minutes and we are session initiator, then try to invite again the remote device
	if ((TimeOutRemote==0)&&(SessionState==SESSION_OPENED))
	{
		ConnectionLost = true;
		if (IsInitiatorNode)
		{
			TimeOutRemote=4;
			RestartSession();
		}
		else
		{  // If we are not session initiator, just wait to be invited again
			SessionState=SESSION_WAIT_INVITE;
		}
	}

	// Init state decoder
	InvitationReceivedOnCtrl=false;
	InvitationReceivedOnData=false;
	InvitationAcceptedOnCtrl=false;
	InvitationAcceptedOnData=false;
	InvitationRefusedOnCtrl=false;
	InvitationRefusedOnData=false;
	ByeReceivedOnCtrl=false;
	ByeReceivedOnData=false;

	ClockSync0Received=false;
	ClockSync1Received=false;
	ClockSync2Received=false;

	// Check if something has been received on control socket
	if (DataAvail(ControlSocket, 0))
	{
		fromlen=sizeof(sockaddr_in);
		RecvSize=(int)recvfrom(ControlSocket, (char*)&ReceptionBuffer, sizeof(ReceptionBuffer), 0, (sockaddr*)&SenderData, &fromlen);

		if (RecvSize>0)
		{  // Check if message is sent from configured partner
#if defined (__TARGET_WIN__)
			if ((htonl(SenderData.sin_addr.S_un.S_addr)==RemoteIP)||(RemoteIP==0))
#endif
#if defined (__TARGET_MAC__)
			if ((htonl(SenderData.sin_addr.s_addr)==RemoteIP)||(RemoteIP==0))
#endif
			{

				// Check if this is an Apple session message
				// This socket can react on two messages : INvitation and OK/NO (after inviting)
				if ((ReceptionBuffer[0]==0xFF)&&(ReceptionBuffer[1]==0xFF))
				{
					if ((ReceptionBuffer[2]=='I')&&(ReceptionBuffer[3]=='N'))
					{
						SessionPacket=(TSessionPacket*)&ReceptionBuffer[0];
						InitiatorToken=htonl(SessionPacket->InitiatorToken);
						InvitationReceivedOnCtrl=true;
                        // Store IP address and port from requestor, so we can send back the answer to correct destination
                        // Note that we can have to answer to any requestor, so we do not use the programmed destination address even if we are session initiator
                        InvitationOnCtrlSenderIP=htonl(SenderData.sin_addr.s_addr);
						RemoteControl=htons(SenderData.sin_port);
					}
					else if ((ReceptionBuffer[2]=='O')&&(ReceptionBuffer[3]=='K')) InvitationAcceptedOnCtrl=true;
					else if ((ReceptionBuffer[2]=='N')&&(ReceptionBuffer[3]=='O')) InvitationRefusedOnCtrl=true;
					else if ((ReceptionBuffer[2]=='B')&&(ReceptionBuffer[3]=='Y')) ByeReceivedOnCtrl=true;
				}
			}
		}
	}  // Reception on control socket

	// Check if something has been received on data socket
	if (DataAvail(DataSocket, 0))
	{
		fromlen=sizeof(sockaddr_in);
		nRet=(int)recvfrom(DataSocket, (char*)&ReceptionBuffer, sizeof(ReceptionBuffer), 0, (sockaddr*)&SenderData, &fromlen);

		if (nRet>0)
		{
#if defined (__TARGET_WIN__)
			if ((htonl(SenderData.sin_addr.S_un.S_addr)==RemoteIP)||(RemoteIP==0))
#endif
#if defined (__TARGET_MAC__)
				if ((htonl(SenderData.sin_addr.s_addr)==RemoteIP)||(RemoteIP==0))
#endif
			{
				// Check if this is a RTP message
				if (SessionState==SESSION_OPENED)
				{
					if ((ReceptionBuffer[0]==0x80)&&(ReceptionBuffer[1]==0x61))
					{
						ProcessIncomingRTP(&ReceptionBuffer[0]);
					}
				}
				// Check if this is an Apple session message
				if ((ReceptionBuffer[0]==0xFF)&&(ReceptionBuffer[1]==0xFF))
				{
					if ((ReceptionBuffer[2]=='I')&&(ReceptionBuffer[3]=='N'))
					{
						SessionPacket=(TSessionPacket*)&ReceptionBuffer[0];
						InitiatorToken=htonl(SessionPacket->InitiatorToken);
						InvitationReceivedOnData=true;
						RemoteData=htons(SenderData.sin_port);
                        InvitationOnDataSenderIP=htonl(SenderData.sin_addr.s_addr);
					}
					else if ((ReceptionBuffer[2]=='O')&&(ReceptionBuffer[3]=='K')) InvitationAcceptedOnData=true;
					else if ((ReceptionBuffer[2]=='N')&&(ReceptionBuffer[3]=='O')) InvitationRefusedOnData=true;
					else if ((ReceptionBuffer[2]=='B')&&(ReceptionBuffer[3]=='Y')) ByeReceivedOnData=true;

					// Check for synchronization message
					else if ((ReceptionBuffer[2]=='C')&&(ReceptionBuffer[3]=='K'))
					{
						SyncPacket=(TSyncPacket*)&ReceptionBuffer[0];
						if (SyncPacket->Count==0)
						{
							ClockSync0Received=true;
#ifdef SHOW_RTP_INFO
                            fprintf (stdout, "Clock sync 0 received %u %u\n", htonl(SyncPacket->TS1H), htonl(SyncPacket->TS1L));
#endif
							TS1H=htonl(SyncPacket->TS1H);
							TS1L=htonl(SyncPacket->TS1L);
						}
						else if (SyncPacket->Count==1)
						{
							ClockSync1Received=true;
#ifdef SHOW_RTP_INFO
                            fprintf (stdout, "Clock sync 1 received %u %u - %u %u\n", htonl(SyncPacket->TS1H), htonl(SyncPacket->TS1L), htonl(SyncPacket->TS2H), htonl(SyncPacket->TS2L));
#endif
							TS1H=htonl(SyncPacket->TS1H);
							TS1L=htonl(SyncPacket->TS1L);
							TS2H=htonl(SyncPacket->TS2H);
							TS2L=htonl(SyncPacket->TS2L);

							// This message is an answer to our first sync message
							// Latency is the current time (at which answer has been received) minus time at which CK0 was sent
							// We pass here only if we are session initiator
							MeasuredLatency = TimeCounter - TS1L;
						}
						else if (SyncPacket->Count==2)
						{
							ClockSync2Received=true;
							TS1H=htonl(SyncPacket->TS1H);
							TS1L=htonl(SyncPacket->TS1L);
							TS2H=htonl(SyncPacket->TS2H);
							TS2L=htonl(SyncPacket->TS2L);
							TS3H=htonl(SyncPacket->TS3H);
							TS3L=htonl(SyncPacket->TS3L);

							// We pass here if we are session listener
							MeasuredLatency = TimeCounter - TS2L;
						}
					}
				}
			}
		}
	}  // Data received on data socket

#ifdef SHOW_RTP_INFO
	if (InvitationReceivedOnCtrl) fprintf (stdout, "Invitation received on Control port\n");
	if (InvitationAcceptedOnCtrl) fprintf (stdout, "Invitation accepted on Control port\n");
	if (InvitationRefusedOnCtrl) fprintf (stdout, "Invitation refused on Control port\n");

	if (InvitationReceivedOnData) fprintf (stdout, "Invitation received on Data port\n");
	if (InvitationAcceptedOnData) fprintf (stdout, "Invitation accepted on Data port\n");
	if (InvitationRefusedOnData) fprintf (stdout, "Invitation refused on Data port\n");

	if (ByeReceivedOnCtrl) fprintf (stdout, "BY received on Control port\n");
	if (ByeReceivedOnData) fprintf (stdout, "BY received on Data port\n");
#endif

	// *** Non state related answers ***
	if (ClockSync0Received)
	{
		SendSyncPacket(1, TS1H, TS1L, 0, TimeCounter, 0, 0);
#ifdef SHOW_RTP_INFO
		printf ("Sent clock 1 from non state related\n");
#endif
	}

	if ((ClockSync1Received)&&(SessionState==SESSION_OPENED))
	{
		TimeOutRemote = 4;				// Bug corrected 12/10/2021
		SendSyncPacket(2, TS1H, TS1L, TS2H, TS2L, 0, TimeCounter);
#ifdef SHOW_RTP_INFO
		printf ("Sent clock 2 from state SESSION_OPENED\n");
#endif
	}

	if (ClockSync2Received)
	{
		TimeOutRemote=4;
		SessionState=SESSION_OPENED;
		//PrepareTimerEvent(30000);
	}

	if (InvitationReceivedOnCtrl)
	{
        // TODO : send a NO if session is already opened with another remote entity
		SendInvitationReply(true, true, 0);
	}

	if (InvitationReceivedOnData)
	{
        // TODO : send a NO if session is already opened with another remote entity
		SendInvitationReply(false, true, 0);
        if (IsInitiatorNode==false)
        {
            SessionPartnerIP=InvitationOnDataSenderIP;      // TODO : only if session is accepted and if we are Session Listener
        }
	}

    // If we detect that remote node disconnects, restart the state machine accordingly to Session Initiator / Session Listener status
	// TODO : only if sender IP is the one with which we have a connection
	if ((ByeReceivedOnData)||(ByeReceivedOnCtrl))
	{
        TimerRunning=false;     // Stop any timed event
		if (IsInitiatorNode==false) 
		{
			SessionState=SESSION_WAIT_INVITE;
		}
		else
		{
			SessionState=SESSION_CLOSED; 
		}
		PeerClosedSession = true;
        SessionPartnerIP=0;
	}

	// *** State machine manager ***
	if (SessionState==SESSION_CLOSED)
	{
		return;
	}

	// NOTE : we must process the SESSION_OPENED state, since it is used to send RTP-MIDI blocks
	if (SessionState==SESSION_OPENED)
	{
		// Check if there is not a delay request (to avoid Kiss-Box output buffer overflow)
		if (InterFragmentTimer>0)
		{
			InterFragmentTimer--;
		}

		// Check if any data waiting to be sent to network and no time still needed
		if (InterFragmentTimer==0) RTPOutSize=PrepareMessage(&LRTPMessage, TimeCounter);
		else RTPOutSize=0;
		if (RTPOutSize>0)
		{
			RTPSequence++;  // Increment for next message
			// Send message on network
			memset (&AdrEmit, 0, sizeof(sockaddr_in));
			AdrEmit.sin_family=AF_INET;
			AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
			AdrEmit.sin_port=htons(RemoteData);
			sendto(DataSocket, (const char*)&LRTPMessage, RTPOutSize, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
		}

		// Resynchronize clock with remote node every 30 seconds if we are initiator
		if (TimerRunning==false)
		{
			if (TimerEvent)
			{
				// Send a RS packet if we have received something meanwhile (do not send the RS if nothing has been received, it crashes the Apple driver)
				if (LastRTPCounter!=LastFeedbackCounter)
				{
					SendFeedbackPacket(LastRTPCounter);
					LastFeedbackCounter=LastRTPCounter;
				}

				if (IsInitiatorNode==true)
				{  // Restart a synchronization sequence if we are session initiator
					SendSyncPacket(0, 0, TimeCounter, 0, 0, 0, 0);
				}

				// We send first a sync sequence 5 times every 1.5 seconds, then one sync sequence every 10 seconds
				if (SyncSequenceCounter<=5)
				{
					PrepareTimerEvent(1500);
					SyncSequenceCounter+=1;
				}
				else
				{
					PrepareTimerEvent(10000);
				}
				if (TimeOutRemote>0)
					TimeOutRemote-=1;
#ifdef SHOW_RTP_INFO
				//printf ("TimeOutRemote = %d\n", TimeOutRemote);
				printf ("Sent clock 0 from state SESSION_OPENED\n");
#endif
			}
		}
		return;
	}

	// We are inviting remote node on control port
	if (SessionState==SESSION_INVITE_CONTROL)
	{
		SyncSequenceCounter=0;
		if (InvitationAcceptedOnCtrl)
		{
			SessionState=SESSION_INVITE_DATA;
			this->SendInvitation(false);
			PrepareTimerEvent(100);
			return;
		}
		if (TimerRunning==false)
		{
			if (TimerEvent)
			{  // Previous attempt has timed out
				// Keep inviting until we get an answer
				/*
				if (InviteCount>12)
				{  // No answer received from remote station after 12 attempts : stop invitation and go into SESSION_WAIT_INVITE
					SessionState=SESSION_WAIT_INVITE;
					return;
				}
				else
				*/
				{
					this->SendInvitation(true);
					PrepareTimerEvent(1000);  // Wait one second before sending a new invitation
					InviteCount++;
					return;
				}
			}
		}
		else {  /* We wait for an event : nothing to do */ }
		return;
	}

	// We are inviting remote node on data port
	if (SessionState==SESSION_INVITE_DATA)
	{
		if (InvitationAcceptedOnData)
		{
			SessionState=SESSION_CLOCK_SYNC0;
			return;
		}
		if (TimerRunning==false)
		{
			if (TimerEvent)
			{  // Previous attempt has timed out
				if (InviteCount>12)
				{  // No answer received from remote station after 12 attempts : stop invitation and go back to SESSION_INVITE_CONTROL
					RestartSession();
					return;
				}
				else
				{
					this->SendInvitation(false);
					PrepareTimerEvent(1000);  // Wait one second before sending a new invitation
					InviteCount++;
					return;
				}
			}
		}
		else {  /* We wait for an event : nothing to do */ }
		return;
	}

	if (SessionState==SESSION_WAIT_INVITE)
	{
		return;
	}

	if (SessionState==SESSION_CLOCK_SYNC0)
	{
		SendSyncPacket(0, 0, TimeCounter, 0, 0, 0, 0);
		SessionState=SESSION_CLOCK_SYNC1;
#ifdef SHOW_RTP_INFO
		printf ("Sent clock 0 from state SESSION_CLOCK_SYNC0\n");
#endif
		return;
	}

	if (SessionState==SESSION_CLOCK_SYNC1)
	{
#ifdef SHOW_RTP_INFO
		printf ("Received clock 1 from state SESSION_CLOCK_SYNC1\n");
#endif
		if (ClockSync1Received) SessionState=SESSION_CLOCK_SYNC2;
		return;
	}

	if (SessionState==SESSION_CLOCK_SYNC2)
	{
		SendSyncPacket(2, TS1H, TS1L, TS2H, TS2L, 0, TimeCounter);
		SessionState=SESSION_OPENED;
#ifdef SHOW_RTP_INFO
		printf ("Sent clock 2 from state SESSION_CLOCK_SYNC2\n");
#endif
		return;
	}
}  // CRTP_MIDI::RunSession
//---------------------------------------------------------------------------

int CRTP_MIDI::GeneratePayload (unsigned char* MIDIList)
{
	unsigned int MIDIBlockEnd;			// Index of last MIDI byte in FIFO to transmit
	bool FullPayload;					// RTP payload block full of data
	unsigned char StatusByte;
	unsigned int CodeMIDI;
	int CtrBytePayload;					// Counter of RTP payload bytes
	unsigned int TempPtr;
	unsigned int Count;
	unsigned int SysexLen;
	unsigned int RemainingBytes;

	CtrBytePayload=0;
	FullPayload=false;

	// Check if we have data in the RTP stream queue
    MIDIBlockEnd=RTPStreamQueue.WritePtr;			// Snapshot of current position of last MIDI message
    if (MIDIBlockEnd!=RTPStreamQueue.ReadPtr)
    {
		while ((RTPStreamQueue.ReadPtr!=MIDIBlockEnd)&&(!FullPayload))
        {
			// TODO : make sure that there is enough room in the RTP-MIDI message buffer
            TempPtr=RTPStreamQueue.ReadPtr;
            MIDIList[CtrBytePayload]=RTPStreamQueue.FIFO[TempPtr];
            CtrBytePayload+=1;

            TempPtr+=1;
            if (TempPtr>=MIDI_CHAR_FIFO_SIZE)
				TempPtr=0;
            RTPStreamQueue.ReadPtr=TempPtr;		// Update pointer only after checking we did not loopback
        }
    }

	return CtrBytePayload;
}  // CRTP_MIDI::GeneratePayload
//--------------------------------------------------------------------------

int CRTP_MIDI::PrepareMessage (TLongMIDIRTPMsg* Buffer, unsigned int TimeStamp)
{
	unsigned int TailleMIDI;

	TailleMIDI=GeneratePayload(&Buffer->Payload.MIDIList[0]);
	if (TailleMIDI==0) return 0;  // No MIDI data to transmit

	// Write directly value rather than bit coding
	// Version=2, Padding=0, Extension=0, CSRCCount=0, Marker=1, PayloadType=0x11
	Buffer->Header.Code1=0x80;
	Buffer->Header.Code2=0x61;

	// Long MIDI list : B=1
	// Deltatime before first byte : Z=1
	// Phantom = 0 (status byte always included)
	Buffer->Payload.Control=htons((unsigned short)TailleMIDI|LONG_B_BIT|LONG_Z_BIT);

	Buffer->Header.SequenceNumber=htons(RTPSequence);
	Buffer->Header.Timestamp=htonl(TimeStamp);
	Buffer->Header.SSRC=htonl(SSRC);
	return TailleMIDI+sizeof(TRTP_Header)+2;  // 2 = size of control word
}  // CRTP_MIDI::PrepareMessage
//--------------------------------------------------------------------------

int CRTP_MIDI::getSessionStatus (void)
{
	if (SessionState==SESSION_CLOSED) return 0;
	if (SessionState==SESSION_OPENED) return 3;
	if ((SessionState==SESSION_INVITE_DATA)||(SessionState==SESSION_INVITE_CONTROL)) return 1;
	return 2;
}  // CRTP_MIDI::getSessionStatus
//--------------------------------------------------------------------------

void CRTP_MIDI::setSessionName (char* Name)
{
	if (strlen(Name)>MAX_SESSION_NAME_LEN-1) return;
	strcpy ((char*)&this->SessionName[0], Name);
}  // CRTPMIDI::setSessionName
//--------------------------------------------------------------------------

bool CRTP_MIDI::SendRTPMIDIBlock (unsigned int BlockSize, unsigned char* MIDIData)
{
	// TODO : add a semaphore in case this function is called from another thread
	unsigned int TmpWrite;
	unsigned int ByteCounter;

	if (BlockSize == 0) return true;
	if (SessionState!=SESSION_OPENED) return false;		// Avoid filling the FIFO when nothing can be sent

	// Try to copy the whole block in FIFO
	TmpWrite = RTPStreamQueue.WritePtr;

	for (ByteCounter=0; ByteCounter<BlockSize; ByteCounter++)
	{
		RTPStreamQueue.FIFO[TmpWrite] = MIDIData[ByteCounter];
		TmpWrite++;
		if (TmpWrite>=MIDI_CHAR_FIFO_SIZE)
			TmpWrite = 0;

		// Check if FIFO is not full
		if (TmpWrite == RTPStreamQueue.ReadPtr) return false;
	}

	// Update write pointer only when the whole block has been copied
	RTPStreamQueue.WritePtr = TmpWrite;

	return true;
}  // CRTP_MIDI::SendRTPMIDIBlock
//--------------------------------------------------------------------------

unsigned int CRTP_MIDI::GetLatency (void)
{
	if (SessionState != SESSION_OPENED) return 0xFFFFFFFF;

	return MeasuredLatency;
}  // CRTP_MIDI::GetLatency
//--------------------------------------------------------------------------

void CRTP_MIDI::RestartSession (void)
{
	if (this->IsInitiatorNode == false) return;
	//if (this->SessionState != SESSION_CLOSED) return;

	SYSEX_RTPActif=false;
	SegmentSYSEXInput=false;
	InviteCount=0;
	TimeOutRemote=16;		// 120 seconds -> Five sync sequences every 1.5 seconds then sync sequence every 10 seconds = 11 + 5
	IncomingThirdByte=false;
	SessionState=SESSION_INVITE_CONTROL;
    PrepareTimerEvent(1000);
}  // CRTP_MIDI::RestartSession
//--------------------------------------------------------------------------

bool CRTP_MIDI::ReadAndResetConnectionLost (void)
{
	if (ConnectionLost==false) return false;

	ConnectionLost=false;
	return true;
}
//--------------------------------------------------------------------------

bool CRTP_MIDI::RemotePeerClosedSession (void)
{
	bool ReadValue;

	ReadValue = PeerClosedSession;
	PeerClosedSession = false;

	return ReadValue;
}  // CRTP_MIDI::RemotePeerClosedSession
//--------------------------------------------------------------------------

