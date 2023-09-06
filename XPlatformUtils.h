/*
 *  XPlatformUtils.h
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
 */

//---------------------------------------------------------------------------
#ifndef __XPLATFORMUTILS_H__
#define __XPLATFORMUTILS_H__
//---------------------------------------------------------------------------

#include "TargetTypes.h"

// PPC <-> x86 conversion
#ifdef __cplusplus
extern "C" {
#endif
unsigned long InvertLong (unsigned long input);
float InvertFloat (float input);

//! Makes system wait for x milliseconds
void SystemWaitMS (unsigned int MSTime);

#ifdef __cplusplus
}
#endif

#endif
