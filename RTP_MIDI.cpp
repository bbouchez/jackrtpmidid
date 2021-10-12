/*
 *  RTP_MIDI.cpp
 *  Generic class for RTP_MIDI session initiator/listener
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
 
 /*
 Release notes :
 12/10/2021 :
  - bug corrected in test : "=" was used in place of "=="  -> else if ((ReceptionBuffer[2]='C')...
 */

#include "RTP_MIDI.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool VerboseRTP=false;

CRTP_MIDI::CRTP_MIDI(TMIDI_FIFO_CHAR* CharQ, unsigned char* SYXOutBuffer, unsigned int* SYXOutSize, unsigned int SYXInSize, TRTPDataCallback CallbackFunc, void* UserInstance)
{
    RemoteIP=DEFAULT_RTP_ADDRESS;
    RemoteControl=DEFAULT_RTP_CTRL_PORT;
    RemoteData=DEFAULT_RTP_DATA_PORT;
    LocalControl=DEFAULT_RTP_CTRL_PORT;
    LocalData=DEFAULT_RTP_DATA_PORT;

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

    IsInitiatorNode=true;
    TimeOutRemote=4;

    SysexSize=SYXOutSize;
    SysexBuffer=SYXOutBuffer;
    InterFragmentTimer=0;
    TransmittedSYSEXInFragment=0;

    InSYSEXBufferSize=SYXInSize;
    InSYSEXBuffer=new unsigned char [InSYSEXBufferSize];

    RTPStreamQueue=CharQ;

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
    TimerEvent=false;		// Signal no event
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
    LocalControl=LocalCtrlPort;
    LocalData=LocalDataPort;

    Token=rand()*0xFFFFFFFF;
    SSRC=rand()*0xFFFFFFFF;
    RTPSequence=0;
    LastRTPCounter=0;
    LastFeedbackCounter=0;
    SyncSequenceCounter=0;

    // Close the control and data sockets, just in case...
    CloseSockets();

    // Open the two UDP sockets (we let the OS give us the local port number)
    SocketOK=CreateUDPSocket (&ControlSocket, LocalControl, false);
    if (SocketOK==false) CreateError=-1;
    SocketOK=CreateUDPSocket (&DataSocket, LocalData, false);
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
    socklen_t fromlen;

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
    unsigned int TS1H;
    unsigned int TS1L;
    unsigned int TS2H;
    unsigned int TS2L;
    unsigned int TS3H;
    unsigned int TS3L;

    ssize_t ErrCode;

    TLongMIDIRTPMsg LRTPMessage;
    sockaddr_in AdrEmit;
    int RTPOutSize;

    // Computing time using the thread is not perfect, we should use OS time related data
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

    // If no resync from remote node after 2 minutes, close session
    if ((TimeOutRemote=0)&&(SessionState==SESSION_OPENED))
    {
	SessionState=SESSION_WAIT_INVITE;
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
        {    // Check if message is sent from configured partner
            if ((htonl(SenderData.sin_addr.s_addr)==RemoteIP)||(RemoteIP==0))
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
            if ((htonl(SenderData.sin_addr.s_addr)==RemoteIP)||(RemoteIP==0))
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
                            if (VerboseRTP) printf ("CK0 received %x%x\n", htonl(SyncPacket->TS1H), htonl(SyncPacket->TS1L));
                            TS1H=htonl(SyncPacket->TS1H);
                            TS1L=htonl(SyncPacket->TS1L);
                        }
                        else if (SyncPacket->Count==1)
                        {
                            ClockSync1Received=true;
                            if (VerboseRTP) printf ("CK1 received %x%x - %x%x\n", htonl(SyncPacket->TS1H), htonl(SyncPacket->TS1L), htonl(SyncPacket->TS2H), htonl(SyncPacket->TS2L));
                            TS1H=htonl(SyncPacket->TS1H);
                            TS1L=htonl(SyncPacket->TS1L);
                            TS2H=htonl(SyncPacket->TS2H);
                            TS2L=htonl(SyncPacket->TS2L);
                        }
                        else if (SyncPacket->Count==2)
                        {
                            ClockSync2Received=true;
                            if (VerboseRTP) printf ("CK2 received %x%x - %x%x - %x%x\n", htonl(SyncPacket->TS1H), htonl(SyncPacket->TS1L), htonl(SyncPacket->TS2H), htonl(SyncPacket->TS2L), htonl(SyncPacket->TS3H), htonl(SyncPacket->TS3L));
                            TS1H=htonl(SyncPacket->TS1H);
                            TS1L=htonl(SyncPacket->TS1L);
                            TS2H=htonl(SyncPacket->TS2H);
                            TS2L=htonl(SyncPacket->TS2L);
                            TS3H=htonl(SyncPacket->TS3H);
                            TS3L=htonl(SyncPacket->TS3L);
                        }
                    }  // Received a CK
                }  // Received Apple session header
            }
        }  // nRet > 0
    }  // Data received on data socket

    if (VerboseRTP)
    {
        if (InvitationReceivedOnCtrl) printf ("Invitation received on Control port\n");
        if (InvitationAcceptedOnCtrl) printf ("Invitation accepted on Control port\n");
        if (InvitationRefusedOnCtrl) printf ("Invitation refused on Control port\n");

        if (InvitationReceivedOnData) printf ("Invitation received on Data port\n");
        if (InvitationAcceptedOnData) printf ("Invitation accepted on Data port\n");
        if (InvitationRefusedOnData) printf ("Invitation refused on Data port\n");

        if (ByeReceivedOnCtrl) printf ("BY received on Control port\n");
        if (ByeReceivedOnData) printf ("BY received on Data port\n");
    }

    // *** Non state related answers ***
    if (ClockSync0Received)
    {
        SendSyncPacket(1, TS1H, TS1L, 0, TimeCounter, 0, 0);
        if (VerboseRTP) printf ("CK1 sent\n");
    }

    if ((ClockSync1Received)&&(SessionState==SESSION_OPENED))
    {
        SendSyncPacket(2, TS1H, TS1L, TS2H, TS2L, 0, TimeCounter);
        if (VerboseRTP) printf ("CK2 sent (Session opened)\n");
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
            SessionPartnerIP=InvitationOnDataSenderIP;      // TODO : only of session is accepted and if we are Session Listener
        }
    }

    // If we detect that remote node disconnects, restart the state machine accordingly to Session Initiator / Session Listener status
    if ((ByeReceivedOnData)||(ByeReceivedOnCtrl))
    {
        TimerRunning=false;     // Stop any timed event
        if (IsInitiatorNode)
        {
            SessionState=SESSION_INVITE_CONTROL;
            PrepareTimerEvent(1000);
        }
        else
        {
            SessionState=SESSION_CLOSED; // TODO : only if sender IP is the one with which we have a connection
            SessionPartnerIP=0;
        }
    }

    // *** State machine manager ***
    if (SessionState==SESSION_CLOSED) return;

    // NOTE : we must process the SESSION_OPENED state, since it is used to send RTP-MIDI blocks
    // The other states are flushing the MIDI queue from client by default
    if (SessionState==SESSION_OPENED)
    {
    	// Check if there is not a delay request (to avoid Kiss-Box output buffer overflow)
    	if (InterFragmentTimer>0)
    	{
            InterFragmentTimer--;
        }

        // Check if any data waiting to be sent to network and no time still needed
        if (InterFragmentTimer==0) RTPOutSize=PrepareMessage(&LRTPMessage, TimeCounter, false);
        else RTPOutSize=0;

        //if (RTPOutSize!=0) printf ("%d\n", RTPOutSize);

        if (RTPOutSize>0)
        {
            RTPSequence++;  // Increment for next message
            // Send message on network
            memset (&AdrEmit, 0, sizeof(sockaddr_in));
            AdrEmit.sin_family=AF_INET;
            AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);    // V0.6 : use SessionPartnerIP, not Remote IP
            AdrEmit.sin_port=htons(RemoteData);
            ErrCode=sendto(DataSocket, (const char*)&LRTPMessage, RTPOutSize, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
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

                if (TimeOutRemote>0) TimeOutRemote-=1;
            }  // TimerEvent
        }  // Timer not running
        return;
    }  // Session opened

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
                    SessionState=SESSION_INVITE_CONTROL;
                    PrepareTimerEvent(1000);
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
	return;
    }

    if (SessionState==SESSION_WAIT_INVITE) return;

    if (SessionState==SESSION_CLOCK_SYNC0)
    {
	SendSyncPacket(0, 0, TimeCounter, 0, 0, 0, 0);
	SessionState=SESSION_CLOCK_SYNC1;
        if (VerboseRTP)	printf ("Sent clock 0 from state SESSION_CLOCK_SYNC0\n");
	return;
    }

    if (SessionState==SESSION_CLOCK_SYNC1)
    {
	if (VerboseRTP) printf ("Received clock 1 from state SESSION_CLOCK_SYNC1\n");
        if (ClockSync1Received) SessionState=SESSION_CLOCK_SYNC2;
	return;
    }

    if (SessionState==SESSION_CLOCK_SYNC2)
    {
	SendSyncPacket(2, TS1H, TS1L, TS2H, TS2L, 0, TimeCounter);
	SessionState=SESSION_OPENED;
	if (VerboseRTP) printf ("Sent clock 2 from state SESSION_CLOCK_SYNC2\n");
	return;
    }
}  // CRTP_MIDI::RunSession
//---------------------------------------------------------------------------

int CRTP_MIDI::GeneratePayload (unsigned char* MIDIList, bool FlushOnly)
{
    unsigned int MIDIBlockEnd;			// Index of last MIDI byte in FIFO to transmit
    bool FullPayload;				// RTP payload block full of data
    unsigned int StatusByte;
    unsigned int CodeMIDI;
    int CtrBytePayload;				// Counter of RTP payload bytes
    unsigned int TempPtr;
    unsigned int Count;
    unsigned int SysexLen;
    unsigned int RemainingBytes;

    CtrBytePayload=0;
    FullPayload=false;

    // Check if there is any SYSEX data waiting to be sent from Downloader or Uploader
    // If so, initialize the buffer
    // TODO : check if it is not interesting to exit immediately when the RTP buffer is filled with SYSEX
    if (SysexSize!=0)
    {
	SysexLen=*SysexSize;
	if (SysexLen!=0)
	{
            // Check if data can fit in a single RTP packet
            if (SysexLen>MAX_RTP_LOAD-1)		// - 1 since we have a null delta time in front of the message
            {  // SYSEX message does not fit in a single RTP-MIDI packet : start fragmentation
		if (TransmittedSYSEXInFragment==0)
		{  // First fragment
                    MIDIList[CtrBytePayload]=0x00;  // Deltatime
                    CtrBytePayload+=1;
                    for (Count=0; Count<SYSEX_FRAGMENT_SIZE; Count++)
                        MIDIList[CtrBytePayload+Count]=SysexBuffer[Count];
                    CtrBytePayload+=SYSEX_FRAGMENT_SIZE;

                    // Add trailing 0xF0 for fragment
                    MIDIList[CtrBytePayload]=0xF0;
                    CtrBytePayload+=1;

                    // Count first block of data
                    TransmittedSYSEXInFragment=SYSEX_FRAGMENT_SIZE;

                    // 512 bytes take 131ms to be transmitted : do not send more data for 131 ms
                    InterFragmentTimer=131;

                    return CtrBytePayload;
		}  // First SYSEX fragment
		else
		{  // Next SYSEX fragment
                    // Create header
                    MIDIList[CtrBytePayload]=0x00;  // Deltatime
                    CtrBytePayload+=1;
                    MIDIList[CtrBytePayload]=0xF7;  // Add leading 0xF7 for next segments
                    CtrBytePayload+=1;

                    // Count how many bytes still need to be transmitted
                    SysexLen-=TransmittedSYSEXInFragment;
                    if (SysexLen>SYSEX_FRAGMENT_SIZE) RemainingBytes=SYSEX_FRAGMENT_SIZE;
                    else RemainingBytes=SysexLen;

                    for (Count=0; Count<RemainingBytes; Count++)
                        MIDIList[CtrBytePayload+Count]=SysexBuffer[Count+TransmittedSYSEXInFragment];
                    CtrBytePayload+=RemainingBytes;
                    TransmittedSYSEXInFragment+=RemainingBytes;

                    // Add trailing 0xF0 for fragment
                    if (SysexLen-RemainingBytes>0)
                    {  // Not the last segment : add a trailing 0xF0
                        MIDIList[CtrBytePayload]=0xF0;
			CtrBytePayload+=1;
                    }

                    // 512 bytes take 131ms to be transmitted : do not send more data for 131 ms
                    InterFragmentTimer=131;

                    // Signal end of transmission if all bytes are sent
                    if (TransmittedSYSEXInFragment>=(*SysexSize))
                    {
                        TransmittedSYSEXInFragment=0;		// Prepare for next transmission
			*SysexSize=0;
                    }

                    return CtrBytePayload;
		}
            }
            else
            {  // SYSEX message fits in a single RTP-MIDI packet
		// Fill RTP packet with SYSEX data
                MIDIList[CtrBytePayload]=0x00;  // Deltatime
		for (Count=0; Count<(*SysexSize); Count++)
                    MIDIList[CtrBytePayload+Count+1]=SysexBuffer[Count];
		CtrBytePayload+=SysexLen;
		CtrBytePayload+=1;

		// Reset the marker to inform client that it can transmit a new set of SYSEX data
		*SysexSize=0;
            }
	}
    }  // SYSEX data to transmit

    // TODO : ******* check that message is not full for each of coming steps before putting new data *********

    // Check if we have data in the RTP stream
    if (RTPStreamQueue)
    {
        MIDIBlockEnd=RTPStreamQueue->WritePtr;			// Snapshot of current position of last MIDI message
        if (MIDIBlockEnd!=RTPStreamQueue->ReadPtr)
        {
            while ((RTPStreamQueue->ReadPtr!=MIDIBlockEnd)&&(!FullPayload))
            {
                //printf ("%x ", FromEditorQ->FIFO[FromEditorQ->ReadPtr]);		// FOR DEBUG
                // TODO : make sure that there is enough room in the FIFO for the complete message
                TempPtr=RTPStreamQueue->ReadPtr;
                MIDIList[CtrBytePayload]=RTPStreamQueue->FIFO[TempPtr];
                CtrBytePayload+=1;

                TempPtr+=1;
                if (TempPtr>=MIDI_CHAR_FIFO_SIZE)
                    TempPtr=0;
                RTPStreamQueue->ReadPtr=TempPtr;
            }
        }
    }

    return CtrBytePayload;
}  // CRTP_MIDI::GeneratePayload
//--------------------------------------------------------------------------

int CRTP_MIDI::PrepareMessage (TLongMIDIRTPMsg* Buffer, unsigned int TimeStamp, bool FlushOnly)
{
    unsigned int TailleMIDI;

    TailleMIDI=GeneratePayload(&Buffer->Payload.MIDIList[0], FlushOnly);
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
