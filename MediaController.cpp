
//////////////////////////////////////////////////////////////////////////
//
// MediaController.cpp : CMediaController class implementation.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

#include <new>
#include "MF_ASFParser.h"

///////////////////////////////////////////////////////////////////////
//  Name: CreateInstance
//  Description:  Static class method to create the CMediaController object.
//
//  ppDecoder: Receives an AddRef's pointer to the CMediaController object.
//            The caller must release the pointer.
/////////////////////////////////////////////////////////////////////////

HRESULT CMediaController::CreateInstance(CMediaController **ppMediaController)
{
    HRESULT hr = S_OK;

    //Instantiate the class
    CMediaController *pMediaController = new CMediaController(&hr);

    if (!pMediaController)
    {
        return E_OUTOFMEMORY;
    }

    //Return the pointer to the caller
    if (SUCCEEDED(hr))
    {
        *ppMediaController = pMediaController;
        (*ppMediaController)->AddRef();
    }

    SafeRelease(&pMediaController);
    return hr;
}

// ----- Public Methods -----------------------------------------------
//////////////////////////////////////////////////////////////////////////
//  Name: CMediaController
//  Description: Constructor
//
/////////////////////////////////////////////////////////////////////////

CMediaController::CMediaController(HRESULT* hr)
:
m_nRefCount (1),
m_gdiplusToken (0),
m_pAudioTestSample (NULL),
m_pBitmap (NULL),
m_hWaveOut (NULL),
m_fHasTestMedia (FALSE),
m_fAudioDeviceBusy (FALSE)
{
    //Load the GDI+ platform, this will be used for displaying the bitmap
    Gdiplus::Status status;
    GdiplusStartupInput gdiplusStartupInput;

    status = GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    if (status == Ok)
    {
        *hr = S_OK;
    }
    else
    {
        *hr = E_FAIL;
    }
};

// ----- Public Methods -----------------------------------------------
//////////////////////////////////////////////////////////////////////////
//  Name: CMediaController
//  Description: Destructor
//
/////////////////////////////////////////////////////////////////////////

CMediaController::~CMediaController()
{
    //Release resources
    Reset();

    //Shutdown GDI+
    if (m_gdiplusToken!=0)
    {
        GdiplusShutdown(m_gdiplusToken);
    }
}

/////////////////////////////////////////////////////////////////////
// Name: CreateBitmapForKeyFrame
//
// Creates a Bitmap object from pixel data
//
// pPixelData: Pixel data for the key frame.
// pMediaType:  Pointer to the media type of the stream.
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::CreateBitmapForKeyFrame(BYTE* pPixelData, IMFMediaType* pMediaType)
{
    if(!pPixelData || !pMediaType)
    {
        return E_INVALIDARG;
    }

    INT32 stride = 0;

    //Get the Frame size and stride through Media Type attributes

    HRESULT hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m_Width, &m_Height);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pMediaType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride);
    if (FAILED(hr))
    {
        goto done;
    }

    delete m_pBitmap;

    //Create the bitmap with the given size
    m_pBitmap = ::new (std::nothrow) Bitmap(m_Width, m_Height, (INT32)stride, PixelFormat32bppRGB, pPixelData);

    if(!m_pBitmap)
    {
        hr = E_OUTOFMEMORY;
    }
    else
    {
        //Bitmap was created, set the flag
        m_fHasTestMedia = TRUE;
    }

done:
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: GetBitmapDimensions
//
// Gets the width and heigh of the Bitmap object.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::GetBitmapDimensions(UINT32 *pWidth, UINT32 *pHeight)
{
    if (m_pBitmap == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    if (!pWidth || !pHeight)
    {
        return E_POINTER;
    }

    *pWidth = m_Width;
    *pHeight = m_Height;

    return S_OK;
}



/////////////////////////////////////////////////////////////////////
// Name: DrawKeyFrame
//
// Draws the Bitmap object on the specified Window.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::DrawKeyFrame(HWND hWnd)
{
    if (!m_pBitmap)
    {
        return E_FAIL;
    }

    Graphics* grph = new Graphics(hWnd);

    if (!grph)
    {
        return E_FAIL;
    }

    Status st = grph->DrawImage( m_pBitmap,  0, 0);

    delete grph;

    return S_OK;
}


/////////////////////////////////////////////////////////////////////
// Name: AddToAudioTestSample
//
// Adds the audio sample buffers to a test sample that the class maintains.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::AddToAudioTestSample (IMFSample *pSample)
{
    if(!pSample)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    IMFMediaBuffer* pBuffer = NULL;

    if (! m_pAudioTestSample)
    {
        hr = MFCreateSample(&m_pAudioTestSample);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(hr))
    {
        goto done;
    }

    hr =  m_pAudioTestSample->AddBuffer(pBuffer);
    if (FAILED(hr))
    {
        goto done;
    }

    m_fHasTestMedia = TRUE;

done:
    SafeRelease(&pBuffer);
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: Reset
//
// Resets the audio test sample by removing all the buffers from it.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::Reset()
{
    HRESULT hr = S_OK;

    delete m_pBitmap;
    m_pBitmap = NULL;

    SafeRelease(&m_pAudioTestSample);

    if(SUCCEEDED(hr))
    {
        m_fHasTestMedia = FALSE;
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: CloseAudioDevice
//
// Closes the default waveOut device, if open.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::CloseAudioDevice()
{
    if (m_hWaveOut != NULL)
    {
        if (waveOutClose(m_hWaveOut) !=  MMSYSERR_NOERROR)
        {
            return E_FAIL;
        }
        m_hWaveOut = NULL;
    }

    return S_OK;
}

/////////////////////////////////////////////////////////////////////
// Name: OpenAudioDevice
//
// Opens the default waveOut device.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::OpenAudioDevice(IMFMediaType* pMediaType)
{
    if (!pMediaType)
    {
        return E_INVALIDARG;
    }

    WAVEFORMATEX*  pWavOutputFormat = NULL;
    UINT32 cbWavOutputFormatSize = 0;

    //Create the WAVEFORMATEX structure from the media type
    HRESULT hr = MFCreateWaveFormatExFromMFMediaType(pMediaType, &pWavOutputFormat, &cbWavOutputFormatSize);
    if (FAILED(hr))
    {
        goto done;
    }

    //Query if the format is supported
    MMRESULT mmr = waveOutOpen(
        NULL,
        WAVE_MAPPER,        // select the device for me
        pWavOutputFormat,   // format
        0,
        0,
        WAVE_FORMAT_QUERY   // Query if the format is OK.
        );

    if (mmr == MMSYSERR_NOERROR)
    {
        hr = S_OK;
    }
    else
    {
        hr = MF_E_INVALIDMEDIATYPE;
        goto done;
    }

    //if the format is supported, open the audio device
    //make sure the device is not in use
    hr = CloseAudioDevice();
    if (FAILED(hr))
    {
        goto done;
    }

    // Create the thread that will handle messages from the waveOut device.
    DWORD dwThreadId;

    m_hThread = CreateThread( NULL, 0, WaveOutThreadProc, (void*)this, NULL, &dwThreadId );

    if( NULL == m_hThread )
    {
        hr =( HRESULT_FROM_WIN32( GetLastError() ) );
        goto done;
    }

    // Open the device.
    mmr = waveOutOpen(
        &m_hWaveOut,            // receives the handle to the device
        WAVE_MAPPER,            // select the device for me
        pWavOutputFormat,                 // format
        (DWORD)dwThreadId,      // thread ID to get waveOut messages
        (DWORD_PTR)this,        // instance data
        CALLBACK_THREAD
        );

    if (mmr != MMSYSERR_NOERROR)
    {
        hr = E_FAIL;
    }


done:
    CoTaskMemFree(pWavOutputFormat);
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: PlayAudio
//
// Delivers an audio buffer to the waveOut device.
//
/////////////////////////////////////////////////////////////////////

HRESULT CMediaController::PlayAudio()
{

    if (! m_hWaveOut || ! m_pAudioTestSample)
    {
        return E_FAIL;
    }

    //Check if the device is busy
    if (m_fAudioDeviceBusy)
    {
        return E_ACCESSDENIED;
    }

    MMRESULT mmt;

    //WAVEHDR pWaveHeader;
    IMFMediaBuffer* pAudioBuffer = NULL;

    BYTE        *pData = NULL;
    DWORD       cbData = 0;

    //Get all the buffers in the test sample in one buffer
    HRESULT hr =  m_pAudioTestSample->ConvertToContiguousBuffer(&pAudioBuffer);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get a pointer to the buffer.
    hr = pAudioBuffer->Lock( &pData, NULL, &cbData );
    if (FAILED(hr))
    {
        goto done;
    }

    // Prepare the header for playing.
    m_WaveHeader.lpData = (LPSTR)pData;
    m_WaveHeader.dwBufferLength = cbData;
    m_WaveHeader.dwBytesRecorded = cbData;
    m_WaveHeader.dwUser = (DWORD_PTR)pAudioBuffer; // Store the sample pointer as user data. This will be released in the MM_WOM_DONE handler
    m_WaveHeader.dwLoops = 0;
    m_WaveHeader.dwFlags = 0;

    mmt = waveOutPrepareHeader( m_hWaveOut, &m_WaveHeader, sizeof( WAVEHDR ) );

    if (mmt == MMSYSERR_NOERROR)
    {
        // Send the sample to the waveOut device.
        mmt = waveOutWrite( m_hWaveOut, &m_WaveHeader, sizeof( WAVEHDR ) );

        hr = S_OK;
    }

    if (mmt != MMSYSERR_NOERROR)
    {
        SafeRelease(&pAudioBuffer);
        hr = E_FAIL;
    }
    else
    {
        hr = S_OK;

        //Set the device to busy
        m_fAudioDeviceBusy = TRUE;
    }

done:
    if (FAILED(hr))
    {
        pAudioBuffer->Unlock();
        SafeRelease(&pAudioBuffer);
    }
    return hr;
}

//-----------------------------------------------------------------------------
// Name: WaveOutThreadProc
// Desc: ThreadProc for the worker thread that handles waveOut messages.
//
// Note: This is a static method. It calls through to a member function.
//-----------------------------------------------------------------------------

DWORD WINAPI CMediaController::WaveOutThreadProc( LPVOID lpParameter )
{
    CMediaController* pThis = ( CMediaController* )lpParameter;

    // Redirect the processing to a non-static member function.

    pThis->DoWaveOutThread();

    return( 0 );
}

//-----------------------------------------------------------------------------
// Name: DoWaveOutThread
// Desc: Implements the ThreadProc (see WaveOutThreadProc)
//-----------------------------------------------------------------------------

void CMediaController::DoWaveOutThread()
{
    MSG         uMsg;

    while( 0 != GetMessage( &uMsg, NULL, 0, 0 ) )
    {
        switch( uMsg.message )
        {
        case MM_WOM_DONE:  // waveOut has finished using an audio buffer.
            {
                WAVEHDR *pwh = (WAVEHDR*)uMsg.lParam;

                // (1) Unprepare the wave header.
                MMRESULT mmr = waveOutUnprepareHeader(m_hWaveOut, pwh, sizeof(WAVEHDR));

                // (2) Release the buffer pointer.
                IMFMediaBuffer *pBuffer= (IMFMediaBuffer*)pwh->dwUser;
                pBuffer->Unlock();
                pBuffer->Release();

                // (3) Reset the WAVEHDR structure
                ZeroMemory(pwh, sizeof(WAVEHDR));

                // (4) Set the busy flag
                m_fAudioDeviceBusy = FALSE;
            }
            break;

        case MM_WOM_CLOSE:  // the waveOut device has closed.
            // Tell the thread to quit:
            PostQuitMessage( 0 );   // This causes GetMessage to return 0, so we exit the loop
            break;
        }
    }
}