//////////////////////////////////////////////////////////////////////////
//
// Decoder.cpp : CDecoder class implementation.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////


#include "MF_ASFParser.h"

///////////////////////////////////////////////////////////////////////
//  Name: CreateInstance
//  Description:  Static class method to create the CDecoder object.
//
//  ppDecoder: Receives an AddRef's pointer to the CDecoder object.
//            The caller must release the pointer.
/////////////////////////////////////////////////////////////////////////

HRESULT CDecoder::CreateInstance(CDecoder **ppDecoder)
{
    CDecoder *pDecoder = new CDecoder();

    if (!pDecoder)
    {
        return E_OUTOFMEMORY;
    }

    *ppDecoder = pDecoder;

    return S_OK;
}

// ----- Public Methods -----------------------------------------------
//////////////////////////////////////////////////////////////////////////
//  Name: CDecoder
//  Description: Constructor
//
/////////////////////////////////////////////////////////////////////////

CDecoder::CDecoder()
: m_nRefCount (1),
m_pMFT (NULL),
m_dwInputID (0),
m_dwOutputID (0),
m_DecoderState (0),
m_pMediaController (NULL)
{

};

// ----- Public Methods -----------------------------------------------
//////////////////////////////////////////////////////////////////////////
//  Name: CDecoder
//  Description: Destructor
//
/////////////////////////////////////////////////////////////////////////

CDecoder::~CDecoder()
{
    (void)UnLoad();
}

/////////////////////////////////////////////////////////////////////
// Name: Initialize
//
// Initializes the MFT with decoder object specified by the CLSID.
//
// pclsid: Path name of the file
// pMediaType:  Pointer to the media type of the stream that the
//              the MFT will decode.
/////////////////////////////////////////////////////////////////////

HRESULT CDecoder::Initialize(CLSID clsid,
                             IMFMediaType *pMediaType)
{

    if (!pMediaType || clsid == GUID_NULL)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    //Unload the existing MFT.
    if (m_pMFT)
    {
        hr = UnLoad();
        if (FAILED(hr))
        {
            goto done;
        }
    }

    //Create the MFT decoder
    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pMFT));
    if (FAILED(hr))
    {
        goto done;
    }

    //Create the media controller that will work with uncompressed data that the decoder generates
    if (!m_pMediaController)
    {
        hr = CMediaController::CreateInstance(&m_pMediaController);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    hr =  ConfigureDecoder(pMediaType);


done:
    if (FAILED(hr))
    {
        hr = UnLoad();
    }
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: UnLoad
//
// Unloads the MFT.
//
/////////////////////////////////////////////////////////////////////

HRESULT CDecoder::UnLoad()
{
    HRESULT hr = S_OK;

    if (m_pMFT)
    {
        if (m_pMediaController)
        {
            hr = m_pMediaController->Reset();
        }
        SafeRelease(&m_pMFT);
    }
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: ConfigureDecoder
//
// Configures the MFT with the currently loaded decoder.
//
// pMediaType:  Pointer to the media type of the stream that will the
//              input type of the decoder.
/////////////////////////////////////////////////////////////////////

HRESULT CDecoder::ConfigureDecoder(IMFMediaType *pMediaType)
{
    if (!pMediaType)
    {
        return E_INVALIDARG;
    }

    if (! m_pMFT)
    {
        return MF_E_NOT_INITIALIZED;
    }

    HRESULT hr = S_OK, hrRes = S_OK;

    GUID guidMajorType = GUID_NULL, guidSubType = GUID_NULL;

    IMFMediaType* pOutputType = NULL;


    //Because this is a decoder transform, the number of input=output=1
    //Get the input and output stream ids. This is different from the stream numbers

    hr = m_pMFT->GetStreamIDs( 1, &m_dwInputID, 1, &m_dwOutputID );

    //Set the input type to the one that is received

    if (SUCCEEDED(hr) || hr == E_NOTIMPL)
    {
        hr = m_pMFT->SetInputType( m_dwInputID, pMediaType, 0 );
        if (FAILED(hr))
        {
            goto done;
        }
    }

    if (SUCCEEDED(hr))
    {
        //Loop through the available output type until we find:
        //For audio media type: PCM audio
        //For video media type: uncompressed RGB32
        for ( DWORD dwTypeIndex = 0; (hrRes != MF_E_NO_MORE_TYPES) ; dwTypeIndex++ )
        {
            hrRes =  m_pMFT->GetOutputAvailableType(
                                                m_dwOutputID,
                                                dwTypeIndex,
                                                &pOutputType);

            if (pOutputType && SUCCEEDED(hrRes))
            {
                hr = pOutputType->GetMajorType( &guidMajorType );
                if (FAILED(hr))
                {
                    goto done;
                }

                hr = pOutputType->GetGUID( MF_MT_SUBTYPE, &guidSubType );
                if (FAILED(hr))
                {
                    goto done;
                }

                if ((guidMajorType == MFMediaType_Audio) && (guidSubType == MFAudioFormat_PCM))
                {
                    hr =  m_pMFT->SetOutputType(m_dwOutputID, pOutputType, 0);
                    if (FAILED(hr))
                    {
                        goto done;
                    }

                    hr =  m_pMediaController->OpenAudioDevice(pOutputType);
                    break;
                }
                else if ((guidMajorType == MFMediaType_Video) && (guidSubType == MFVideoFormat_RGB32))
                {
                    hr =  m_pMFT->SetOutputType(m_dwOutputID, pOutputType, 0);
                    break;
                }

                SafeRelease(&pOutputType);
            }
            else
            {
                //Output type not found
                hr = E_FAIL;
                break;
            }
        }
    }

done:
    SafeRelease(&pOutputType);
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: ProcessAudio
//
// Passes the input sample through the decoder and sends the output samples
// to the CMediaController class. This class adds the buffers of the
// output sample to the audio test sample that it maintains. When ready, the
// caller can play the test sample through methods on the CMediaController
//class.
//
// pSample: Pointer to a compressed sample that needs to be decoded
/////////////////////////////////////////////////////////////////////

HRESULT CDecoder::ProcessAudio(IMFSample *pSample)
{
    if (!pSample)
    {
        return E_INVALIDARG;
    }

    if (! m_pMFT || ! m_pMediaController)
    {
        return MF_E_NOT_INITIALIZED;
    }

    DWORD dwStatus = 0;

    IMFMediaBuffer* pBufferOut = NULL;
    IMFSample* pSampleOut = NULL;

    //get the size of the output buffer processed by the decoder.
    //Again, there is only one output so the output stream id is 0.
    MFT_OUTPUT_STREAM_INFO mftStreamInfo = { 0 };
    MFT_OUTPUT_DATA_BUFFER mftOutputData = { 0 };

    HRESULT hr = m_pMFT->GetOutputStreamInfo(m_dwOutputID, &mftStreamInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    hr =  m_pMFT->ProcessInput(m_dwInputID, pSample, 0);
    if (FAILED(hr))
    {
        goto done;
    }

    //Request output samples from the decoder
    while (SUCCEEDED(hr))
    {
        //create a buffer for the output sample
        hr = MFCreateMemoryBuffer(mftStreamInfo.cbSize, &pBufferOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Create the output sample
        hr = MFCreateSample(&pSampleOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Add the output buffer
        hr = pSampleOut->AddBuffer(pBufferOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Set the output sample
        mftOutputData.pSample = pSampleOut;

        //Set the output id
        mftOutputData.dwStreamID = m_dwOutputID;

        //Generate the output sample
        hr =  m_pMFT->ProcessOutput(0, 1, &mftOutputData, &dwStatus);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            hr = S_OK;
            break;
        }

        if (FAILED(hr))
        {
            goto done;
        }

        //Send it to the media controller so that it can collect the test sample

        hr =  m_pMediaController->AddToAudioTestSample(mftOutputData.pSample);
        if (FAILED(hr))
        {
            goto done;
        }

        SafeRelease(&pBufferOut);
        SafeRelease(&pSampleOut);
    }

done:
    SafeRelease(&pBufferOut);
    SafeRelease(&pSampleOut);

    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: ProcessVideo
//
// Passes the input sample through the decoder and sends the output sample data
// to the CMediaController class. This class creates a bitmap for the sample.
// When ready, the caller can display the bitmap through methods on
// the CMediaController class.
//
// pSample: Pointer to a compressed sample that needs to be decoded
/////////////////////////////////////////////////////////////////////

HRESULT CDecoder::ProcessVideo(IMFSample *pSample)
{
    if (!pSample)
    {
        return E_INVALIDARG;
    }

    if (! m_pMFT || ! m_pMediaController)
    {
        return MF_E_NOT_INITIALIZED;
    }

    DWORD dwStatus = 0;

    DWORD cbTotalLength = 0, cbCurrentLength = 0;

    BYTE *pData = NULL;

    IMFMediaBuffer* pBufferOut = NULL;
    IMFSample* pSampleOut = NULL;
    IMFSample* pBitmapSample = NULL;
    IMFMediaType* pMediaType = NULL;

    //Create a buffer for the transform output
    MFT_OUTPUT_STREAM_INFO mftStreamInfo = { 0 };
    MFT_OUTPUT_DATA_BUFFER mftOutputData = { 0 };

    //get the size of the output buffer processed by the decoder.
    //Again, there is only one output so the output stream id is 0.
    HRESULT hr = m_pMFT->GetOutputStreamInfo(0, &mftStreamInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    //Request samples from the decoder

    //Create the bitmap sample that the media controller will use to create the bitmap
    hr = MFCreateSample(&pBitmapSample);
    if (FAILED(hr))
    {
        goto done;
    }

    //Send input to the decoder. There is only one input stream so the ID is 0.
    hr =  m_pMFT->ProcessInput(m_dwInputID, pSample, 0);
    if (FAILED(hr))
    {
        goto done;
    }

    //Request output samples from the decoder
    while (SUCCEEDED(hr))
    {
        //create a buffer for the output sample
        hr = MFCreateMemoryBuffer(mftStreamInfo.cbSize, &pBufferOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Create the output sample
        hr = MFCreateSample(&pSampleOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Add the output buffer
        hr = pSampleOut->AddBuffer(pBufferOut);
        if (FAILED(hr))
        {
            goto done;
        }

        //Set the output sample
        mftOutputData.pSample = pSampleOut;

        mftOutputData.dwStreamID = m_dwOutputID;

        //Generate the output sample
        hr =  m_pMFT->ProcessOutput(0, 1, &mftOutputData, &dwStatus);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            hr = S_OK;
            goto done;
        }

        //Add the buffer to the bitmap sample
        hr = pBitmapSample->AddBuffer(pBufferOut);
        if (FAILED(hr))
        {
            goto done;
        }

        SafeRelease(&pBufferOut);
        SafeRelease(&pSampleOut);
    };

    //Get all bitmap data in one buffer
    hr = pBitmapSample->ConvertToContiguousBuffer(&pBufferOut);
    if (FAILED(hr))
    {
        goto done;
    }

    hr =  m_pMFT->GetOutputCurrentType(m_dwOutputID, &pMediaType);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get a pointer to the memory
    hr = pBufferOut->Lock(&pData, &cbTotalLength, &cbCurrentLength);
    if (FAILED(hr))
    {
        goto done;
    }

    //Send it to the media controller to create the bitmap
    hr = m_pMediaController->CreateBitmapForKeyFrame(pData, pMediaType);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pBufferOut->Unlock();

    pData = NULL;

done:

    if (pData)
    {
        pBufferOut->Unlock();
    }

    SafeRelease(&pBufferOut);
    SafeRelease(&pSampleOut);
    SafeRelease(&pMediaType);
    return hr;
}

HRESULT CDecoder::StartDecoding(void)
{
    if(! m_pMFT)
    {
        return MF_E_NOT_INITIALIZED;
    }

    HRESULT hr =  m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

    if (SUCCEEDED(hr))
    {
         m_DecoderState = STREAMING;
    }
    return hr;

}

HRESULT CDecoder::StopDecoding(void)
{
    if(! m_pMFT)
    {
        return MF_E_NOT_INITIALIZED;
    }

    HRESULT hr =  m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);

    if (SUCCEEDED(hr))
    {
         m_DecoderState = NOT_STREAMING;
    }
    return hr;

}
