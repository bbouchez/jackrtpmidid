/*
 * File:   JackRTPMIDID.cpp
 * JACK RTP-MIDI daemon for Zynthian
 * Author: Benoit BOUCHEZ (BEB)
 *
 * Created on 13 octobre 2019, 10:09
 *
 * MIT License
 *
 * Copyright (c) 2019-2024 bbouchez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

 /*
 Release notes

 V0.5 - 21/03/2020
 - added SIGINT signal handler for proper program termination
 - removed commented code sections from older tests

 V0.6 - 22/03/2020
 - bug corrected in RTP_MIDI.cpp : transmission from Zynthian was not working (wrong destination IP address)

 V0.7 - 03/09/2023
  - updated RTP-MIDI library (with many minor bugs corrected)
  - verbosity option removed (only for debug)
  - project reorganized to use BEB's libraries used in other projects
  - code framework prepared to add NetUMP (MIDI 2.0 on Ethernet) support

V0.8 - 04/01/2024
  - moved to MIT license
  - project modified to use BEB SDK and RTP-MIDI cross platform libraries
        https://github.com/bbouchez/BEBSDK
        https://github.com/bbouchez/RTP-MIDI

V0.9 - 17/02/2024
  - update to "cleaned" RTP-MIDI class

V1.0 - 07/07/2024
  - added support for a second connexion (allow the Zynthian to be controlled by sequencer and keyboard at the same time)
  - main loop replaced by a high priority thread to get best possible timing even with high CPU load
 */

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include "RTP_MIDI.h"
#include "SystemSleep.h"
#include "CThread.h"

jack_port_t *input_port;
jack_port_t *output_port;
bool break_request=false;
CThread* RTThread=0;

CRTP_MIDI* RTPMIDIHandler1=0;
CRTP_MIDI* RTPMIDIHandler2=0;
TMIDI_FIFO_CHAR MIDI2JACK;
TMIDI_FIFO_CHAR JACK2RTP;

// High priority realtime thread for RTP-MIDI communication
void* RTThreadFunc (CThread* Control)
{
    while (Control->ShouldStop==false)
    {
        if (RTPMIDIHandler1)
            RTPMIDIHandler1->RunSession();
        if (RTPMIDIHandler2)
            RTPMIDIHandler2->RunSession();
        SystemSleepMillis(1);
    }
    Control->IsStopped=true;
    pthread_exit(NULL);
    return 0;
}  // RTThreadFunc
//-----------------------------------------------------------------------------

// Function called when the RTP engine receives a valid MIDI message
// Stores received MIDI bytes into FIFO to JACK (generates a MIDI stream)
void RTPMIDICallback (void* Instance, unsigned int DataSize, unsigned char* DataBlock, unsigned int DeltaTime)
{
    unsigned int CurrentOutPtr;
    unsigned int TempInPtr;
    unsigned int ByteCount;
    bool Overflow=false;

    CurrentOutPtr=MIDI2JACK.ReadPtr;		// Make a snapshot to avoid change during next loop
    TempInPtr=MIDI2JACK.WritePtr;			// Local copy to be updated only when full MIDI message is transferred

    for (ByteCount=0; ByteCount<DataSize; ByteCount++)
    {
        MIDI2JACK.FIFO[TempInPtr]=DataBlock[ByteCount];
        TempInPtr+=1;
        if (TempInPtr>=MIDI_CHAR_FIFO_SIZE) TempInPtr=0;
        if (TempInPtr==CurrentOutPtr)
        {
                Overflow=true;
                break;
        }
    }

    if (Overflow==false)
    {  // Update input pointer only if we have been able to store all data
        MIDI2JACK.WritePtr=TempInPtr;
    }
}  // RTPMIDICallback
//-----------------------------------------------------------------------------

// Callback function called when there is an audio block to process
int jack_process(jack_nframes_t nframes, void *arg)
{
    unsigned int i;
    void* in_port_buf = jack_port_get_buffer(input_port, nframes);
    void* out_port_buf = jack_port_get_buffer(output_port, nframes);
    jack_midi_event_t in_event;
    jack_nframes_t event_count = jack_midi_get_event_count(in_port_buf);
    jack_midi_data_t* Buffer;
    unsigned int TempRead, LastBufferPos, TempWrite;
    unsigned char RunningStatus;
    unsigned int NumBytesToRead;
    unsigned char SYSEXBuffer[512];
    unsigned int SYSEXSize;
    unsigned char SYSEXByte;
    size_t NumBytesInEvent;
    unsigned int ByteCounter;

    jack_midi_clear_buffer(out_port_buf);    // Recommended to call this at the beginning of process cycle

    // Check if we have MIDI data waiting in the FIFO from RTP-MIDI to be sent
    if (MIDI2JACK.ReadPtr!=MIDI2JACK.WritePtr)
    {
        // Read FIFO and generate JACK events for each MIDI message in the FIFO
        TempRead=MIDI2JACK.ReadPtr;     // Local snapshot to avoid RTP-MIDI thread to see pointer moving while we parse the buffer
        LastBufferPos=MIDI2JACK.WritePtr;

        while (TempRead!=LastBufferPos)
        {
            // We can assume safely that there will be no incomplete MIDI message in the queue
            // as RTP-MIDI thread only transfers full MIDI messages. No need to check here
            // if a message in the queue is truncated

            // Identify message length from first byte
            RunningStatus=MIDI2JACK.FIFO[TempRead];

            if (RunningStatus==0xF0)
            {  // Specific SYSEX processing
                SYSEXBuffer[0]=0xF0;
                SYSEXSize=0;

                // Read SYSEX size by searching 0xF7
                do {
                    SYSEXByte=MIDI2JACK.FIFO[TempRead];
                    TempRead+=1;
                    if (TempRead>=MIDI_CHAR_FIFO_SIZE) TempRead=0;  // Could be a mask for faster update

                    SYSEXBuffer[SYSEXSize]=SYSEXByte;
                    SYSEXSize++;
                } while ((SYSEXSize<512)&&(SYSEXByte!=0xF7));

                // If SYSEX is too big or 0xF7 not found, reject the message
                if (SYSEXByte==0xF7)
                {
                    // Allocate JACK buffer
                    Buffer=jack_midi_event_reserve (out_port_buf, 0, SYSEXSize);
                    if (Buffer!=0)
                    {  // Copy SYSEX message in the buffer
                        memcpy (Buffer, &SYSEXBuffer[0], SYSEXSize);
                    }
                }
            }
            else
            {  // Non SYSEX
                if ((RunningStatus>=0x80) && (RunningStatus<=0xBF)) NumBytesToRead=3;
                else if ((RunningStatus>=0xC0) && (RunningStatus<=0xDF)) NumBytesToRead=2;
                else if ((RunningStatus>=0xE0) && (RunningStatus<=0xEF)) NumBytesToRead=3;
                else if ((RunningStatus==0xF1)||(RunningStatus==0xF3)) NumBytesToRead=2;
                else if (RunningStatus==0xF2) NumBytesToRead=3;
                else NumBytesToRead=1;

                // Generate the message in JACK buffer
                Buffer=jack_midi_event_reserve (out_port_buf, 0, NumBytesToRead);
                if (Buffer!=0)
                {
                    Buffer[0]=RunningStatus;
                    TempRead+=1;
                    if (TempRead>=MIDI_CHAR_FIFO_SIZE) TempRead=0;  // Could be a mask for faster update

                    if (NumBytesToRead>1)       // Read first byte as we are alreay pointing it
                    {
                        Buffer[1]=MIDI2JACK.FIFO[TempRead];
                        TempRead+=1;
                        if (TempRead>=MIDI_CHAR_FIFO_SIZE) TempRead=0;  // Could be a mask for faster update
                    }

                    if (NumBytesToRead==3)      // Read second byte (avoid loop for standard MIDI messages)
                    {
                        Buffer[2]=MIDI2JACK.FIFO[TempRead];
                        TempRead+=1;
                        if (TempRead>=MIDI_CHAR_FIFO_SIZE) TempRead=0;  // Could be a mask for faster update
                    }
                }  // Buffer to JACK allocated
            }  // Non SYSEX message
        }  // loop over all events in the queue

        // Update read pointer only when we have parsed the whole buffer
        MIDI2JACK.ReadPtr=TempRead;
    }  // MIDI data available from RTP-MIDI queue

    // Generate RTP-MIDI payload (with null delta-time) for each event sent by JACK
    if(event_count >= 1)
    {
        // Make a snapshot of current FIFO position
        TempWrite=JACK2RTP.WritePtr;

        for(i=0; i<event_count; i++)
        {
            jack_midi_event_get(&in_event, in_port_buf, i);
            NumBytesInEvent=in_event.size;

            // Try to store the event in the queue
            // Add leading null delta-time
            JACK2RTP.FIFO[TempWrite++]=0x00;
            if (TempWrite>=MIDI_CHAR_FIFO_SIZE) TempWrite=0;
            if (TempWrite==JACK2RTP.ReadPtr) return 0;      // FIFO is full discard the message
            // TODO : maybe we can save the TempWrite for the last valid message and send what has been stored successfully

            // Copy all the bytes from the JACK MIDI message to the queue
            for (ByteCounter=0; ByteCounter<NumBytesInEvent; ByteCounter++)
            {
                JACK2RTP.FIFO[TempWrite++]=in_event.buffer[ByteCounter];
                if (TempWrite>=MIDI_CHAR_FIFO_SIZE) TempWrite=0;
                if (TempWrite==JACK2RTP.ReadPtr) return 0;      // FIFO is full discard the message
            }
        }

        // Update FIFO pointer when all events to send have been read
        JACK2RTP.WritePtr=TempWrite;
    }

    return 0;
}  // jack_process
// ----------------------------------------------------

/* Callback function called when jack server is shut down */
void jack_shutdown(void *arg)
{
    printf ("JACK has shut down\n");
    break_request=true;
}  // jack_shutdown
// ----------------------------------------------------

void sig_handler (int signo)
{
    if (signo == SIGINT)
    {
        break_request=true;
    }
}  // sig_handler
// ----------------------------------------------------

int main(int argc, char** argv)
{
    int Ret;
    int MaxPrio;
    jack_client_t *client;

    printf ("JACK <-> RTP-MIDI bridge V1.0 for Zynthian\n");
    printf ("Copyright 2019/2024 Benoit BOUCHEZ (BEB)\n");
    printf ("Please report any issue to BEB on https:\\discourse.zynthian.org\n");

    break_request=false;
    signal (SIGINT, sig_handler);

    MIDI2JACK.ReadPtr=0;
    MIDI2JACK.WritePtr=0;

    JACK2RTP.ReadPtr=0;
    JACK2RTP.WritePtr=0;

    if ((client = jack_client_open ("jackrtpmidid", JackNullOption, NULL)) == 0)
    {
        fprintf(stderr, "jackrtpmidid : JACK server not running\n");
        return 1;
    }

    RTPMIDIHandler1 = new CRTP_MIDI (2048, &RTPMIDICallback, 0);
    if (RTPMIDIHandler1)
    {
        RTPMIDIHandler1->setSessionName((char*)"Zynthian RTP-MIDI 1");
        Ret=RTPMIDIHandler1->InitiateSession (0, 5004, 5005, 5004, 5005, false);
        if (Ret==-1) fprintf (stderr, "jackrtpmidid : can not create control socket for session 1\n");
        else if (Ret==-2) fprintf (stderr, "jackrtpmidid : can not create data socket for session 1\n");
        if (Ret!=0)
        {
            delete RTPMIDIHandler1;
            RTPMIDIHandler1 = 0;
        }
    }

    RTPMIDIHandler2 = new CRTP_MIDI (2048, &RTPMIDICallback, 0);    // We can use the same callback for the two handlers, as they run in the same thread
    if (RTPMIDIHandler2)
    {
        RTPMIDIHandler2->setSessionName((char*)"Zynthian RTP-MIDI 2");
        Ret=RTPMIDIHandler2->InitiateSession (0, 5006, 5007, 5006, 5007, false);
        if (Ret==-1) fprintf (stderr, "jackrtpmidid : can not create control socket for session 2\n");
        else if (Ret==-2) fprintf (stderr, "jackrtpmidid : can not create data socket for session 2\n");
        if (Ret!=0)
        {
            delete RTPMIDIHandler2;
            RTPMIDIHandler2 = 0;
        }
    }

    // Register the various callbacks needed by a JACK application
    jack_set_process_callback (client, jack_process, 0);
    jack_on_shutdown (client, jack_shutdown, 0);

    input_port = jack_port_register (client, "rtpmidi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    output_port = jack_port_register (client, "rtpmidi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

    if (jack_activate (client))
    {
            fprintf(stderr, "jackrtpmidid : cannot activate client");
            return -1;
    }

    // Start realtime thread
    MaxPrio = sched_get_priority_max(SCHED_FIFO);
    RTThread = new CThread((ThreadFuncType*)RTThreadFunc, MaxPrio, 0);
    if (RTThread == 0)
    {
        fprintf (stderr, "Can not create realtime communication thread");
        return -2;
    }

    /* run until interrupted */
    while(break_request==false)
    {
        SystemSleepMillis(100);
    }
    printf ("Program termination requested by user\n");

    // Stop realtime communication thread
    if (RTThread)
    {
        printf ("Stopping realtime thread...\n");
        RTThread->StopThread(500);
        delete RTThread;
        RTThread = 0;
    }

    // Clean everything before we exit
    jack_client_close(client);

    if (RTPMIDIHandler1)
    {
        printf ("Closing RTP-MIDI handler for session 1...\n");
        RTPMIDIHandler1->CloseSession();
        delete RTPMIDIHandler1;
        RTPMIDIHandler1=0;
    }

    if (RTPMIDIHandler2)
    {
        printf ("Closing RTP-MIDI handler for session 2...\n");
        RTPMIDIHandler2->CloseSession();
        delete RTPMIDIHandler2;
        RTPMIDIHandler2=0;
    }

    printf ("Done...\n");

    return (EXIT_SUCCESS);
}  // main
// ----------------------------------------------------
