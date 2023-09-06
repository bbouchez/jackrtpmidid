/*
 *  XPlatformUtils.cpp
 *  Cross Platform SDK
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

/* Release notes
 31/12/2009:
 - inclusion of release notes

03/01/2010:
  - header modified
  - inclusion of TargetTypes.h
  - string utilities moved to strutils.h

26/06/2011:
  - add reference to time.h (for nanosleep) on Mac targets

25/05/2020
  - add support for Linux targets (use __TARGET_LINUX__ define)
 */

#include "XPlatformUtils.h"
#include <string.h>

#if defined (__TARGET_MAC__)
#include <unistd.h>
#include <time.h>
#endif

#if defined (__TARGET_LINUX__)
#include <unistd.h>
#include <time.h>
#endif

unsigned long InvertLong (unsigned long input)
// Reverse byte order in a long
{
  unsigned char Temp [4];
  unsigned long result;
  unsigned char Local;

  memcpy (&Temp[0], &input, sizeof(unsigned long));
  Local=Temp[0];
  Temp[0]=Temp[3];
  Temp[3]=Local;
  Local=Temp[1];
  Temp[1]=Temp[2];
  Temp[2]=Local;
  memcpy (&result, &Temp[0], 4);

  //result=((unsigned long)Temp[0]<<24)||((unsigned long)Temp[1]<<16)||((unsigned long)Temp[2]<<8)||(unsigned long)Temp[3];
  return result;
}  // InvertLong
//---------------------------------------------------------------------------

float InvertFloat (float input)
{  // Reverse byte order in a float
  float Result;
  unsigned char Temp [4];
  unsigned char Local;

  memcpy (&Temp[0], &input, sizeof(float));
  Local=Temp[0];
  Temp[0]=Temp[3];
  Temp[3]=Local;
  Local=Temp[1];
  Temp[1]=Temp[2];
  Temp[2]=Local;

  memcpy (&Result, &Temp[0], 4);
  return Result;
}  // InvertFloat
//---------------------------------------------------------------------------

void SystemWaitMS (unsigned int MSTime)
{
#if defined (__TARGET_MAC__)
  struct timespec rqtp;
  struct timespec rmtp;

  rqtp.tv_sec=0;
  rqtp.tv_nsec=MSTime*1000000;
  nanosleep(&rqtp, &rmtp);
#endif
#if defined (__TARGET_WIN__)
  Sleep (MSTime);
#endif
#if defined (__TARGET_LINUX__)
  usleep (MSTime * 1000);
#endif
}  // SystemWaitMS
//---------------------------------------------------------------------------


