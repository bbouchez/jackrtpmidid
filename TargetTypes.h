/*
 *  TargetTypes.h
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

/* Versions

28/12/2009:
 - first version

03/01/2010:
 - Windows platform : TPARENTHANDLE changed to HWND / End of line corrected

 27/01/2010:
 - all platforms : THINSTANCE type added

 12/03/2020:
 - added LINUX platform by __TARGET_LINUX__

*/

#if defined (__TARGET_WIN__)
#if defined (__BORLANDC__)
#include <vcl.h>
#else
#include <windows.h>
#endif

typedef HWND TPARENTHANDLE;
typedef HINSTANCE THINSTANCE;

#elif defined (__TARGET_MAC__)
typedef void* TPARENTHANDLE;
typedef void* THINSTANCE;

#elif defined (__TARGET_LINUX__)
typedef void* TPARENTHANDLE;
typedef void* THINSTANCE;
#endif
