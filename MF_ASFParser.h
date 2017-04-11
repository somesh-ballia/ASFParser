//////////////////////////////////////////////////////////////////////////
//
// Common.h : Global header.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

// Windows Header Files:
#include <windows.h>
#include <cderr.h>
#include <assert.h>
#include <tchar.h>
#include <commdlg.h> // OpenFile dialog
#include <strsafe.h>
#include <commctrl.h> //Common controls
#include <windowsx.h> // Windows helper macros
#include <shlwapi.h>

// Media Foundation Header Files:
#include <mfapi.h>
#include <mfobjects.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcontainer.h>
#include <wmcodecdsp.h>

//Video rendering through GDI+
#include <gdiplus.h>
using namespace Gdiplus;

//WAV play through
#include <mmsystem.h>

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

#include "MediaController.h"
#include "Decoder.h"
#include "ASFManager.h"


//Constants
#define MAX_STRING_SIZE         260
#define TEST_AUDIO_DURATION     50000000
#define STREAMING               1
#define NOT_STREAMING           2
#define MIN_ASF_HEADER_SIZE ( MFASF_MIN_HEADER_BYTES + sizeof( WORD ) + sizeof (DWORD))


