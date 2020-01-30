/*
 *  RTP_MIDI.h
 *  Generic class for RTP_MIDI session initiator/listener
 *
 *  Created by Benoit BOUCHEZ on 05/12/11.
 *  Copyright 2011 Benoit Bouchez. All rights reserved.
 *
 *  Licensing terms
 *  This file and the rtpmidid project are licensed under GNU LGPL licensing terms
 *  Use of this source code in commercial applications and/or products without
 *  written agreement of the author is STRICTLY FORBIDDEN
 *  Refer to LICENSE.TXT file for details
 */

//---------------------------------------------------------------------------
#ifndef __RTP_MIDI_H__
#define __RTP_MIDI_H__
//---------------------------------------------------------------------------

#include "RTP_netdriver.h"
#include "MIDI_FIFO.h"

#define LONG_B_BIT 0x8000
#define LONG_J_BIT 0x4000
#define LONG_Z_BIT 0x2000
#define LONG_P_BIT 0x1000

#define SHORT_J_BIT 0x40
#define SHORT_Z_BIT 0x20
#define SHORT_P_BIT 0x10

#define MAX_SESSION_NAME_LEN	64

// Max size for one RTP payload
#define MAX_RTP_LOAD 1024
// Max size for a single fragmented SYSEX
#define SYSEX_FRAGMENT_SIZE		512

#define DEFAULT_RTP_ADDRESS 0xC0A800FD
#define DEFAULT_RTP_DATA_PORT 5004
#define DEFAULT_RTP_CTRL_PORT 5003

// Session status
#define SESSION_CLOSED			0	// No action
#define SESSION_CLOSE			1	// Session should close in emergency
#define SESSION_INVITE_CONTROL          2	// Sending invitation on control port
#define SESSION_INVITE_DATA		3	// Sending invitation on data port
#define SESSION_WAIT_INVITE		4	// Wait to be invited by remote station
#define SESSION_CLOCK_SYNC0		5	// Send first synchro message and wait answer (CK0)
#define SESSION_CLOCK_SYNC1		6       // Wait for CK1 message from remote node
#define SESSION_CLOCK_SYNC2		7	// Send second synchro message (CK2)
#define SESSION_OPENED			8	// Session is opened, just generate background traffic now

#pragma pack (push, 1)
typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int ProtocolVersion;
  unsigned int InitiatorToken;
  unsigned int SSRC;
  unsigned char Name [1024];
} TSessionPacket;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int ProtocolVersion;
  unsigned int InitiatorToken;
  unsigned int SSRC;
} TSessionPacketNoName;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int SSRC;
  unsigned char Count;
  unsigned char Unused [3];       // 0
  unsigned int TS1H;    // Timestamp 1
  unsigned int TS1L;
  unsigned int TS2H;    // Timestamp 2
  unsigned int TS2L;
  unsigned int TS3H;    // Timestamp 3
  unsigned int TS3L;
} TSyncPacket;

typedef struct {
  unsigned short ControlWord;   // Version/padding/extension/CSRCCount/Marker/PT
  unsigned short SequenceNumber;
  unsigned int Timestamp;
  unsigned int SSRC;
} TRTPHeader;

typedef struct {
  unsigned char Reserved1;       // 0xFF
  unsigned char Reserved2;       // 0xFF
  unsigned char CommandH;
  unsigned char CommandL;
  unsigned int SSRC;
  unsigned short SequenceNumber;
  unsigned short Unused;
} TFeedbackPacket;

typedef struct {
  unsigned char Control;     // B/J/Z/P/Len(4)
                        // 0 : header = 1 byte
                        // 0 : no journalling section / 1 : journalling section after MIDI list
                        // 0 : no deltatime for first MIDI command / 1 : deltatime before MIDI command
                        // Phantom status
                        // MIDI list length = 15 bytes max
  unsigned char MIDIList [15];
} TShortMIDIPayload;

typedef struct {
  unsigned short Control;             // B/J/Z/P/Len(12)
                                // 1 : header = 2 bytes
                                // 0 : no journalling section / 1 : journalling section after MIDI list
                                // 0 : no deltatime for first MIDI command / 1 : deltatime before MIDI command
                                // Phantom status
                                // MIDI list length = 4095 bytes max
  unsigned char MIDIList[MAX_RTP_LOAD];
} TLongMIDIPayload;

typedef struct {
  // Header RTP
  unsigned char Code1;
  unsigned char Code2;
  unsigned short SequenceNumber;
  unsigned int Timestamp;
  unsigned int SSRC;
} TRTP_Header;

typedef struct {
  TRTP_Header Header;
  TLongMIDIPayload Payload;
} TLongMIDIRTPMsg;

typedef struct {
  TRTP_Header Header;
  TShortMIDIPayload Payload;
} TShortMIDIRTPMsg;
#pragma pack (pop)

// This callback is called from realtime thread. Processing time in the callback shall be kept to a minimum
typedef unsigned int (*TRTPDataCallback) (void* UserInstance, unsigned int DataSize, unsigned char* DataBlock, unsigned int DeltaTime);

class CRTP_MIDI
{
public:
    unsigned int LocalClock;           // Timestamp counter following session initiator
    
    //! \param CharQ pointer to a FIFO containing pre-formed RTP-MIDI message (each MIDI message shall be preceded with a deltatime)
    //! \param SYXOutBuffer pointer to an array containing a SYSEX message to send at the next thread call. No new message shall be put in the array until the variable pointed by SYXOutSize is reset by the realtime thread
    //! \param SYXOutSize pointer to a variable indicating the size of message pointed by SYXOutBuffer. Set to 0 if the SYSEX transmission is not used.
    //! \param SYXInSize size of incoming SYSEX buffer (maximum size of input SYSEX message to be returned to application)
    //! \param CallbackFunc pointer a function which will be called each time a packet is received from RTP-MIDI. Set 0 to disable callback
    //! \param UserInstance value which will be passed in the callback function
	CRTP_MIDI(	  TMIDI_FIFO_CHAR* CharQ,
			  unsigned char* SYXOutBuffer, 
			  unsigned int* SYXOutSize, 
			  unsigned int SYXInSize, 
			  TRTPDataCallback CallbackFunc, 
			  void* UserInstance);
	~CRTP_MIDI(void);

	//! Record a session name. Shall be called before InitiateSession.
	void setSessionName (char* Name);
	
	//! Activate network resources and starts communication (tries to open session) with remote node
	// \return 0=session being initiated -1=can not create control socket -2=can not create data socket
	int InitiateSession(unsigned int DestIP,
						unsigned short DestCtrlPort, 
						unsigned short DestDataPort, 
						unsigned short LocalCtrlPort, 
						unsigned short LocalDataPort,
						bool IsInitiator);
	void CloseSession(void);
	
	//! Main processing function to call from high priority thread
	void RunSession(void);
	
	//! Returns the session status
	/*!
	 0 : session is closed
	 1 : inviting remote node
	 2 : synchronization in progress
	 3 : session opened (MIDI data can be exchanged)
	 */
	int getSessionStatus (void);

private:
	// Callback data
	TRTPDataCallback RTPCallback;	// Callback for incoming RTP-MIDI message
	void* ClientInstance;

	unsigned char SessionName [MAX_SESSION_NAME_LEN];
	
	unsigned int RemoteIP;			// Address of remote computer (0 if module is used as session listener)
	unsigned short RemoteControl;	// Remote control port number (0 if module is used as session listener)
	unsigned short RemoteData;		// Remote data port number (0 if module is used as session listener)
	unsigned short LocalControl;	// Local control port number
	unsigned short LocalData;		// Local data port number
    
    unsigned int InvitationOnCtrlSenderIP;      // IP address of sender of invitation received on control port
    unsigned int InvitationOnDataSenderIP;      // IP address of sender of invitation received on data port
    unsigned int SessionPartnerIP;              // IP address of session partner (only valid if session is opened)
    unsigned int CheckerIP;                     // IP address of device checking genuine KB software
   
	TSOCKTYPE ControlSocket;
	TSOCKTYPE DataSocket;

	bool SocketLocked;
	unsigned int SSRC;
	unsigned int Token;
	unsigned short RTPSequence;
	unsigned short LastRTPCounter;		// Last packet counter received from session partner
	unsigned short LastFeedbackCounter;	// Last packet counter sent back in the RS packet
	int SessionState;
	unsigned int InviteCount;		// Number of invitation messages sent
	unsigned int InitiatorToken;
	bool IsInitiatorNode;
	int TimeOutRemote;				// Counter to detect loss of remote node (reset when CK2 is received)
	unsigned int SyncSequenceCounter;		// Count how may sync sequences have been sent after invitation

	bool TimerRunning;				// Event timer is running
	bool TimerEvent;				// Event is signalled
	unsigned int EventTime;                     // System time to which event will be signalled
	
	unsigned int TimeCounter;		// Counter in 100us used for clock synchronization
	
	TMIDI_FIFO_CHAR* RTPStreamQueue;	// Streaming MIDI messages (typically coming from the Editor when used with VSTizer) with precomputed RTP deltatime
	
	unsigned int* SysexSize;		// Pointer to variable containing SYSEX data size to transmit
	unsigned char* SysexBuffer;
	
	bool BlocSYSEX_RTP;			// Marks that we are placing a SYSEX event in an RTP block
	unsigned int InterFragmentTimer;	// Number of milliseconds before we can send the next RTP block
	unsigned int TransmittedSYSEXInFragment;		// Number of SYSEX data already transmitted in fragments

	// Decoding variables for incoming RTP message
	bool SYSEX_RTPActif;			// We are receiving a SYSEX message from network
	unsigned char FullInMidiMsg[3];
	bool IncomingThirdByte;
	unsigned char RTPRunningStatus;	// Running status from network to client
	
	// Members to decode SYSEX data coming from network
	unsigned int InSYSEXBufferSize;		// Size of SYSEX defragmentation buffer
	bool SegmentSYSEXInput;				// SYSEX message is segmented across multiple RTP messages
	unsigned char* InSYSEXBuffer;
	unsigned int InSYSEXBufferPtr;		// Number of SYSEX bytes received
	bool InSYSEXOverflow;				// Received SYSEX message can not fit in the local buffer
        
	void CloseSockets(void);
	void SendInvitation (bool DestControl);

	//! Sends an answer to an invitation
	// Accept=true : invitation accepted
	// Accept=false : invitation rejected
	// Name==NULL : Name is not transmitted in the answer
	void SendInvitationReply (bool OnControl, bool Accept, char* Name);
	void SendSyncPacket (char Count, unsigned int TS1H, unsigned int TS1L, unsigned int TS2H, unsigned int TS2L, unsigned int TS3H, unsigned int TS3L);
	void SendBYCommand (void);

	// Sends a RS packet (synchronization/flush of RTP journal)
	void SendFeedbackPacket (unsigned short LastNumber);

	void PrepareTimerEvent (unsigned int TimeToWait);

	//! Create a MIDI code in Windows format
	unsigned int PrepareCodeMIDI (char data1, char data2, char data3);
	
	//! Extracts and return delta time stored in network buffer
	/*! 
	 \param : BufPtr = pointer sur octets a lire dans le tampon RTP
	 \param : ByteCtr = number of byte read (updated by function). Must contain the position of first byte to read at call
	 */
	unsigned int GetDeltaTime(unsigned char* BufPtr, int* ByteCtr);

	//! Fill the payload area of RTP buffer with MIDI data to send to the network
	//! \return Number of bytes put in payload (0 = no data to be sent)
	//! \param PurgeOnly if set, this method runs in a specific mode to purge queues from VST
	int GeneratePayload (unsigned char* MIDIList, bool FlushOnly);
 
	//! Prepare a RTP_MIDI for sending on the network
	//* Returns the size of generated message. Value 0 means no MIDI data to send */
	int PrepareMessage (TLongMIDIRTPMsg* Buffer, unsigned int TimeStamp, bool FlushOnly);

	//! Analyze incoming RTP frame from network 
	/*! Buffer = buffer containing RTP message received */
	void ProcessIncomingRTP (unsigned char* Buffer);

	//! Read and decode next MIDI event in RTP reception buffer and send it to callback
	void GenerateMIDIEvent(unsigned char* Buffer, int* ByteCtr, int TailleBloc, unsigned int DeltaTime);
	
	//! Initializes local SYSEX buffer
	void initRTP_SYSEXBuffer(void);
	
	//! Store a byte in the SYSEX buffer
	void storeRTP_SYSEXData (unsigned char SysexData);
	
	//! Send the SYSEX buffer to client
	void sendRTP_SYSEXBuffer (unsigned int DeltaTime);
	
	//! Send the MIDI message to client (mex 3 bytes)
	void sendMIDIToClient (unsigned int NumBytes, unsigned int DeltaTime);
};

#endif