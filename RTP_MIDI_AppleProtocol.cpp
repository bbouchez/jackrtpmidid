/*
 *  RTP_MIDI_AppleProtocol.cpp
 *  Generic class for RTP_MIDI session initiator/listener
 *  Methods to generate Apple session protocol messages
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

#include "RTP_MIDI.h"
#include <stdio.h>

void CRTP_MIDI::SendInvitation (bool DestControl)
{
	// DestControl = true : destination is control port (data port otherwise)
	TSessionPacket Invit;
	sockaddr_in AdrEmit;
	int NameLen;

	NameLen=strlen((char*)&this->SessionName[0]);
	
	Invit.Reserved1=0xFF;
	Invit.Reserved2=0xFF;
	Invit.CommandH='I';
	Invit.CommandL='N';
	Invit.ProtocolVersion=htonl(2);
	Invit.InitiatorToken=htonl(Token);
	Invit.SSRC=htonl(SSRC);

	if (NameLen>0)  
	{
		strcpy ((char*)&Invit.Name[0], (char*)&this->SessionName[0]);
		Invit.Name[NameLen]=0x00;
		NameLen+=1;
	}
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	if (DestControl)
	{
		AdrEmit.sin_port=htons(RemoteControl);
		sendto(ControlSocket, (const char*)&Invit, sizeof(TSessionPacketNoName)+NameLen, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
	else
	{
		AdrEmit.sin_port=htons(RemoteData);
		sendto(DataSocket, (const char*)&Invit, sizeof(TSessionPacketNoName)+NameLen, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
} // CRTP_MIDI::SendInvitation
//---------------------------------------------------------------------------

void CRTP_MIDI::SendBYCommand (void)
{
	TSessionPacketNoName PacketBY;
	sockaddr_in AdrEmit;
	
	PacketBY.Reserved1=0xFF;
	PacketBY.Reserved2=0xFF;
	PacketBY.CommandH='B';
	PacketBY.CommandL='Y';
	PacketBY.ProtocolVersion=htonl(2);
	PacketBY.InitiatorToken=htonl(InitiatorToken);
	PacketBY.SSRC=htonl(SSRC);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	//AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(RemoteControl);
	sendto(ControlSocket, (const char*)&PacketBY, sizeof(TSessionPacketNoName), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
} // CRTP_MIDI::SendBYCommand
//---------------------------------------------------------------------------

void CRTP_MIDI::SendInvitationReply (bool OnControl, bool Accept, char* Name)
{
	TSessionPacket Reply;
	int Taille;
	sockaddr_in AdrEmit;
	
	Reply.Reserved1=0xFF;
	Reply.Reserved2=0xFF;
	if (Accept)
	{
		Reply.CommandH='O';
		Reply.CommandL='K';
	}
	else
	{
		Reply.CommandH='N';
		Reply.CommandL='O';
	}
	Reply.ProtocolVersion=htonl(2);
	Reply.InitiatorToken=htonl(InitiatorToken);
	Reply.SSRC=htonl(SSRC);
	
	/*
	 if (Name<>nil) then begin
	 strcopy(@Reply.Name[0], Name);
	 Taille:=sizeof(TSessionPacketNoName)+strlen(Name)+1;
	 end
	 else */
	Taille=sizeof(TSessionPacketNoName);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	//AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	if (OnControl)
	{
        AdrEmit.sin_addr.s_addr=htonl(InvitationOnCtrlSenderIP);
		AdrEmit.sin_port=htons(RemoteControl);
		sendto(ControlSocket, (const char*)&Reply, Taille, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
	else
	{
        AdrEmit.sin_addr.s_addr=htonl(InvitationOnDataSenderIP);
		AdrEmit.sin_port=htons(RemoteData);
		sendto(DataSocket, (const char*)&Reply, Taille, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
	}
}  // CRTP_MIDI::SendInvitationReply
//---------------------------------------------------------------------------

void CRTP_MIDI::SendSyncPacket (char Count, unsigned int TS1H, unsigned int TS1L, unsigned int TS2H, unsigned int TS2L, unsigned int TS3H, unsigned int TS3L)
{
	TSyncPacket Sync;
	sockaddr_in AdrEmit;
	
	Sync.Reserved1=0xFF;
	Sync.Reserved2=0xFF;
	Sync.CommandH='C';
	Sync.CommandL='K';
	Sync.SSRC=htonl(SSRC);
	Sync.Count=Count;
	Sync.Unused[0]=0;
	Sync.Unused[1]=0;
	Sync.Unused[2]=0;
	Sync.TS1H=htonl(TS1H);
	Sync.TS1L=htonl(TS1L);
	Sync.TS2H=htonl(TS2H);
	Sync.TS2L=htonl(TS2L);
	Sync.TS3H=htonl(TS3H);
	Sync.TS3L=htonl(TS3L);
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(RemoteData);
	sendto(DataSocket, (const char*)&Sync, sizeof(TSyncPacket), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
}  // CRTP_MIDI::SendSyncPacket
//---------------------------------------------------------------------------

void CRTP_MIDI::SendFeedbackPacket (unsigned short LastNumber)
{
	TFeedbackPacket Feed;
	sockaddr_in AdrEmit;
	
	Feed.Reserved1=0xFF;
	Feed.Reserved2=0xFF;
	Feed.CommandH='R';
	Feed.CommandL='S';
	Feed.SSRC=htonl(SSRC);
	Feed.SequenceNumber=htons(LastNumber);
	Feed.Unused=0;
	
	memset (&AdrEmit, 0, sizeof(sockaddr_in));
	AdrEmit.sin_family=AF_INET;
	//AdrEmit.sin_addr.s_addr=htonl(RemoteIP);
	AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
	AdrEmit.sin_port=htons(RemoteControl);
	sendto(ControlSocket, (const char*)&Feed, sizeof(TFeedbackPacket), 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
}  // CRTP_MIDI::SendFeedbackPacket
//---------------------------------------------------------------------------

