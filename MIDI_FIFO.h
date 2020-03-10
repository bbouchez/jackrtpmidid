/*
 *  MIDI_FIFO.h
 *  Simple FIFO structure for MIDI communication
 *  Author: Benoit BOUCHEZ (BEB
 *
 *  Created by Benoit BOUCHEZ on 16/03/12.
 *  Copyright 2012 BEBDigitalAudio. All rights reserved.
 *
 *  Licensing terms
 *  This file and the rtpmidid project are licensed under GNU LGPL licensing 
 *  terms with an exception stating that rtpmidid code can be used within
 *  proprietary software products without needing to publish related product
 *  source code.
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