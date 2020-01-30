/*
 *  MIDI_FIFO.h
 *  VSTizer
 *  Simple FIFO structure
 *
 *  Created by Benoit BOUCHEZ on 16/03/12.
 *  Copyright 2012 BEBDigitalAudio. All rights reserved.
 *
 */

#ifndef __MIDI_FIFO_H__
#define __MIDI_FIFO_H__

#define MIDI_FIFO_SIZE	256
#define MIDI_CHAR_FIFO_SIZE		256

typedef struct {
	unsigned int FIFO[MIDI_FIFO_SIZE];
	unsigned int ReadPtr;
	unsigned int WritePtr;
} TMIDI_FIFO_INT;

typedef struct {
	unsigned char FIFO[MIDI_CHAR_FIFO_SIZE];
	unsigned int ReadPtr;
	unsigned int WritePtr;
} TMIDI_FIFO_CHAR;

#endif