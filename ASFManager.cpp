//////////////////////////////////////////////////////////////////////////
//
// ASFManager.cpp : CASFManager class implementation.
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

HRESULT CreateASFIndexer(
    IMFByteStream *pContentByteStream,  
    IMFASFContentInfo *pContentInfo,
    IMFASFIndexer **ppIndexer
    );


HRESULT GetSeekPositionWithIndexer(
    IMFASFIndexer   *pIndexer,
    WORD            wStreamNumber,
    MFTIME          hnsSeekTime,
    BOOL            bReverse,
    QWORD           *pcbDataOffset,
    MFTIME          *phnsApproxSeekTime
    );

//////////////////////////////////////////////////////////////////////////
//  Name: CreateInstance
//  Description: Instantiates the class statically
//
/////////////////////////////////////////////////////////////////////////

HRESULT CASFManager::CreateInstance(CASFManager **ppASFManager)
{

    // Note: CASFManager constructor sets the ref count to zero.
    // Create method calls AddRef.

    HRESULT hr = S_OK;

    CASFManager *pASFManager = new (std::nothrow) CASFManager(&hr);

    if (!pASFManager)
    {
        return E_OUTOFMEMORY;
    }

    if (SUCCEEDED(hr))
    {
        *ppASFManager = pASFManager;
        (*ppASFManager)->AddRef();
    }

    SafeRelease(&pASFManager);
    return hr;
}

// ----- Public Methods -----------------------------------------------
//////////////////////////////////////////////////////////////////////////
//  Name: CASFManager
//  Description: Constructor
//
/////////////////////////////////////////////////////////////////////////

CASFManager::CASFManager(HRESULT* hr)
:   m_nRefCount(1),
    m_CurrentStreamID (0),
    m_guidCurrentMediaType (GUID_NULL),
    m_fileinfo(NULL),
    m_pDecoder (NULL),
    m_pContentInfo (NULL),
    m_pIndexer (NULL),
    m_pSplitter (NULL),
    m_pDataBuffer (NULL),
    m_pByteStream(NULL),
    m_cbDataOffset(0),
    m_cbDataLength(0)
{
    //Initialize Media Foundation
    *hr = MFStartup(MF_VERSION);

}

//////////////////////////////////////////////////////////////////////////
//  Name: ~ASFManager
//  Description: Destructor
//
//  -Calls Shutdown
/////////////////////////////////////////////////////////////////////////

CASFManager::~CASFManager()
{

    //Release memory
    Reset();

   // Shutdown the Media Foundation platform
    (void)MFShutdown();

}



/////////////////////////////////////////////////////////////////////
// Name: OpenASFFile
//
// Opens a file and returns a byte stream.
//
// sFileName: Path name of the file
// ppStream:  Receives a pointer to the byte stream.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::OpenASFFile(const WCHAR *sFileName)
{
    IMFByteStream* pStream = NULL;

    // Open a byte stream for the file.
    HRESULT hr = MFCreateFile(
        MF_ACCESSMODE_READ,
        MF_OPENMODE_FAIL_IF_NOT_EXIST,
        MF_FILEFLAGS_NONE,
        sFileName,
        &pStream
        );

    if (FAILED(hr))
    {
        goto done;
    }

    //Reset the ASF components.
    Reset();

    // Create the Media Foundation ASF objects.
    hr = CreateASFContentInfo(pStream, &m_pContentInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = CreateASFSplitter(pStream, &m_pSplitter);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = CreateASFIndexer(pStream, m_pContentInfo, &m_pIndexer);
    if (FAILED(hr))
    {
        goto done;
    }

done:
    SafeRelease(&pStream);
    return hr;
}



// ----- Private Methods -----------------------------------------------

/////////////////////////////////////////////////////////////////////
// Name: CreateASFContentInfo
//
// Reads the ASF Header Object from a byte stream and returns a
// pointer to the ASF content information object.
//
// pStream:       Pointer to the byte stream. The byte stream's
//                current read position must be 0 that indicates the start of the
//                ASF Header Object.
// ppContentInfo: Receives a pointer to the ASF content information
//                object.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::CreateASFContentInfo (IMFByteStream *pContentByteStream,
                                           IMFASFContentInfo **ppContentInfo)
{
    if (!pContentByteStream || !ppContentInfo)
    {
        return E_INVALIDARG;
    }

    QWORD cbHeader = 0;

    IMFASFContentInfo *pContentInfo = NULL;
    IMFMediaBuffer *pBuffer = NULL;

    // Create the ASF content information object.
    HRESULT hr = MFCreateASFContentInfo(&pContentInfo);

    if (FAILED(hr))
    {
        goto done;
    }

    // Read the first 30 bytes to find the total header size.
    hr = ReadDataIntoBuffer(
        pContentByteStream, 0, MIN_ASF_HEADER_SIZE, &pBuffer);

    if (FAILED(hr))
    {
        goto done;
    }


    hr = pContentInfo->GetHeaderSize(pBuffer, &cbHeader);
    if (FAILED(hr))
    {
        goto done;
    }

    SafeRelease(&pBuffer);

    //Read the header into a buffer
    hr = ReadDataIntoBuffer(pContentByteStream, 0, (DWORD)cbHeader, &pBuffer);
    if (FAILED(hr))
    {
        goto done;
    }

    // Pass the buffer for the header object.
    hr = pContentInfo->ParseHeader(pBuffer, 0);
    if (FAILED(hr))
    {
        goto done;
    }

    // Return the pointer to the caller.
    *ppContentInfo = pContentInfo;
    (*ppContentInfo)->AddRef();

done:
    SafeRelease(&pBuffer);
    SafeRelease(&pContentInfo);
    return hr;
}


/////////////////////////////////////////////////////////////////////
// Name: CreateASFSplitter
//
// Creates the ASF splitter.
//
// pContentByteStream: Pointer to the byte stream that contains the ASF Data Object.
// ppSplitter:   Receives a pointer to the ASF splitter.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::CreateASFSplitter (IMFByteStream *pContentByteStream,
                                        IMFASFSplitter **ppSplitter)
{
    if (!pContentByteStream || !ppSplitter)
    {
        return E_INVALIDARG;
    }

    if (!m_pContentInfo)
    {
        return MF_E_NOT_INITIALIZED;
    }

    IMFASFSplitter *pSplitter = NULL;
    IMFPresentationDescriptor* pPD = NULL;

    UINT64 cbDataOffset = 0, cbDataLength = 0;

    HRESULT hr = MFCreateASFSplitter(&pSplitter);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pSplitter->Initialize(m_pContentInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    //Generate the presentation descriptor
    hr =  m_pContentInfo->GeneratePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get the offset to the start of the Data Object
    hr = pPD->GetUINT64(MF_PD_ASF_DATA_START_OFFSET, &cbDataOffset);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get the length of the Data Object
    hr = pPD->GetUINT64(MF_PD_ASF_DATA_LENGTH, &cbDataLength);
    if (FAILED(hr))
    {
        goto done;
    }

    m_pByteStream = pContentByteStream;
    m_pByteStream->AddRef();

    m_cbDataOffset = cbDataOffset;
    m_cbDataLength = cbDataLength;

    // Return the pointer to the caller.
    *ppSplitter = pSplitter;
    (*ppSplitter)->AddRef();

done:
    SafeRelease(&pSplitter);
    SafeRelease(&pPD);
    return hr;
}


/////////////////////////////////////////////////////////////////////
// Name: EnumerateStreams
//
// Enumerates the streams in the ASF file.
//
// ppwStreamNumbers: Receives the stream identifiers in an array.
//                   The caller must release the allocated memory.
//
// ppguidMajorType: Receives the major media type GUIDs in an array.
//                   The caller must release the allocated memory.
//
// cbTotalStreams:   Receives total number of elements in the array.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::EnumerateStreams (WORD** ppwStreamNumbers,
                                       GUID** ppguidMajorType,
                                       DWORD* pcTotalStreams)
{
    if (!ppwStreamNumbers || !ppguidMajorType || !pcTotalStreams)
    {
        return E_INVALIDARG;
    }

    if (!m_pContentInfo)
    {
        return MF_E_NOT_INITIALIZED;
    }

    IMFASFStreamConfig* pStream = NULL;
    IMFASFProfile* pProfile = NULL;

    *pcTotalStreams =0;

    DWORD cStreams;
    WORD* pwStreamNumbers;  // Array of stream numbers.
    GUID* pguidMajorType;   // Array of major types.

    HRESULT hr =  m_pContentInfo->GetProfile(&pProfile);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pProfile->GetStreamCount(&cStreams);
    if (FAILED(hr))
    {
        goto done;
    }

    if (&cStreams == 0)
    {
        hr = E_FAIL;
        goto done;
    }

    // Allocate arrays to hold the stream numbers and major type GUIDs.

    pwStreamNumbers = new (std::nothrow) WORD[cStreams];

    if (!pwStreamNumbers)
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    pguidMajorType = new (std::nothrow) GUID[cStreams];

    if (!pguidMajorType)
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    // Populate the arrays from the profile object.

    for (DWORD index = 0; index < cStreams; index++)
    {
        hr = pProfile->GetStream(index, &pwStreamNumbers[index], &pStream);
        if (FAILED(hr))
        {
            goto done;
        }

        hr = pStream->GetStreamType(&pguidMajorType[index]);
        if (FAILED(hr))
        {
            goto done;
        }

        SafeRelease(&pStream);
    }

    *ppwStreamNumbers = pwStreamNumbers;
    *ppguidMajorType = pguidMajorType;
    *pcTotalStreams = cStreams;

done:

    SafeRelease(&pProfile);
    SafeRelease(&pStream);

    if (FAILED (hr))
    {
        delete [] pwStreamNumbers;
        delete [] pguidMajorType;
    }
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: SelectStream
//
// Selects the streams in the ASF file.
//
// pwStreamNumber: Specifies the identifier of the stream to be selected
//                 on the splitter.
//
// pguidCurrentMediaType: Receives the major media type GUID of the
//                   currently selected stream.
//
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::SelectStream (WORD wStreamNumber,
                                   GUID* pguidCurrentMediaType)
{
    if (wStreamNumber == 0 || !pguidCurrentMediaType)
    {
        return E_INVALIDARG;
    }

    if (! m_pSplitter || ! m_pContentInfo)
    {
        return MF_E_NOT_INITIALIZED;
    }

    //Select the stream you want to parse. This sample allows you to select only one stream at a time
    HRESULT hr  =  m_pSplitter->SelectStreams(&wStreamNumber, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    //Load the appropriate stream decoder
    hr = SetupStreamDecoder(wStreamNumber, pguidCurrentMediaType);
    if (FAILED(hr))
    {
        goto done;
    }

    m_CurrentStreamID = wStreamNumber;

    m_guidCurrentMediaType = *pguidCurrentMediaType;

done:
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: SetupStreamDecoder
//
// Loads the appropriate decoder for stream. The decoder is implemented as
// a Media Foundation Transform (MFT). The class CDecoder provides a wrapper for
// the MFT. The CASFManager::GenerateSamples feeds compressed samples to
// the decoder. The MFT decodes the samples and sends them to the CMediaController
// object, which plays 10 seconds of uncompressed audio samples or displays the
// key frame for the video stream
//
// wStreamNumber: Specifies the identifier of the stream.
//
// pguidCurrentMediaType: Receives the major media type GUID of the
//                   currently selected stream.
//
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::SetupStreamDecoder (WORD wStreamNumber,
                                         GUID* pguidCurrentMediaType)
{
    if (! m_pContentInfo)
    {
        return MF_E_NOT_INITIALIZED;
    }

    if (wStreamNumber == 0)
    {
        return E_INVALIDARG;
    }

    IMFASFProfile* pProfile = NULL;
    IMFMediaType* pMediaType = NULL;
    IMFASFStreamConfig *pStream = NULL;

    GUID    guidMajorType = GUID_NULL;
    GUID    guidSubType = GUID_NULL;
    GUID    guidDecoderCategory = GUID_NULL;

    BOOL fIsCompressed = TRUE;

    CLSID *pDecoderCLSIDs = NULL;   // Pointer to an array of CLISDs.
    UINT32 cDecoderCLSIDs = 0;   // Size of the array.

    //Get the profile object that stores stream information
    HRESULT hr =  m_pContentInfo->GetProfile(&pProfile);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get stream configuration object from the profile
    hr = pProfile->GetStreamByNumber(wStreamNumber, &pStream);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get the media type
    hr = pStream->GetMediaType(&pMediaType);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get the major media type
    hr = pMediaType->GetMajorType(&guidMajorType);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get the sub media type
    hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubType);
    if (FAILED(hr))
    {
        goto done;
    }

    //find out if the media type is compressed
    hr = pMediaType->IsCompressedFormat(&fIsCompressed);
    if (FAILED(hr))
    {
        goto done;
    }

    if (fIsCompressed)
    {
        //get decoder category
        if (guidMajorType == MFMediaType_Video)
        {
            guidDecoderCategory = MFT_CATEGORY_VIDEO_DECODER;
        }
        else if (guidMajorType == MFMediaType_Audio)
        {
            guidDecoderCategory = MFT_CATEGORY_AUDIO_DECODER;
        }
        else
        {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }

        // Look for a decoder.
        MFT_REGISTER_TYPE_INFO tinfo;
        tinfo.guidMajorType = guidMajorType;
        tinfo.guidSubtype = guidSubType;

        hr = MFTEnum(
            guidDecoderCategory,
            0,                  // Reserved
            &tinfo,             // Input type to match. (Encoded type.)
            NULL,               // Output type to match. (Don't care.)
            NULL,               // Attributes to match. (None.)
            &pDecoderCLSIDs,    // Receives a pointer to an array of CLSIDs.
            &cDecoderCLSIDs     // Receives the size of the array.
            );

        if (FAILED(hr))
        {
            goto done;
        }
        else if (cDecoderCLSIDs == 0)
        {
            // MFTEnum can return zero matches.

            hr = MF_E_TOPO_CODEC_NOT_FOUND;
            goto done;
        }
        else
        {
            // If the CDecoder instance does not exist, create one.

            if (!m_pDecoder)
            {
                hr = CDecoder::CreateInstance(&m_pDecoder);
                if (FAILED(hr))
                {
                    goto done;
                }
            }

            // Load the first MFT in the array for the current media type
            hr = m_pDecoder->Initialize(pDecoderCLSIDs[0], pMediaType);
            if (FAILED(hr))
            {
                goto done;
            }
        }

        *pguidCurrentMediaType = guidMajorType;
    }
    else
    {
        // Not compressed. Don't need a decoder.
        hr = MF_E_INVALIDREQUEST;
    }

done:
    SafeRelease(&pProfile);
    SafeRelease(&pMediaType);
    SafeRelease(&pStream);
    CoTaskMemFree(pDecoderCLSIDs);
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: GetSeekPosition
//
// Gets the offset from the start of the ASF Data Object corresponding
// to the specified time that is seeked by the caller.
//
// hnsSeekTime: [In/out]Seek time in hns. This includes the preroll time. The received
//              value is the actual seek time wth preroll adjustment.
//
// cbDataOffset: Receives the offset in bytes.
//
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::GetSeekPosition (MFTIME* hnsSeekTime,
                                      QWORD *pcbDataOffset,
                                      MFTIME* phnsApproxSeekTime)
{
    HRESULT hr = E_FAIL;

    //if the media type is audio, or doesn't have an indexed data
    //calculate the offset manually
    if ((m_guidCurrentMediaType == MFMediaType_Audio) || (!m_pIndexer))
    {
        hr =  GetSeekPositionManually(*hnsSeekTime, pcbDataOffset);
    }
    //if the type is video, get the position with the indexer
    else if (( m_guidCurrentMediaType == MFMediaType_Video))
    {
        hr =  GetSeekPositionWithIndexer(*hnsSeekTime, pcbDataOffset, phnsApproxSeekTime);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: GetSeekPositionManually
//
// Gets the offset for audio media types or ones that do not have ASF Index Objects defined.
//Offset is calculated as fraction with respect to time
//
// hnsSeekTime: Presentation time in hns.
//
// cbDataOffset: Receives the offset in bytes.
//
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::GetSeekPositionManually(MFTIME hnsSeekTime,
                                    QWORD *cbDataOffset)
{
    DWORD dwFlags = 0;

    //Check if the reverse flag is set, if so, offset is calculated from the end of the presentation
    HRESULT hr = m_pSplitter->GetFlags(&dwFlags);
    if (FAILED(hr))
    {
        return hr;
    }

    //Get average packet size
    UINT32 averagepacketsize = ( m_fileinfo->cbMaxPacketSize+ m_fileinfo->cbMinPacketSize)/2;

    double fraction = 0;

    if (dwFlags & MFASF_SPLITTER_REVERSE)
    {
        fraction = ((double) (m_fileinfo->hnsPresentationDuration) - (double) (hnsSeekTime))/(double) (m_fileinfo->hnsPresentationDuration);
    }
    else
    {
        fraction = (double)(hnsSeekTime)/(double) (m_fileinfo->hnsPresentationDuration);
    }

    //calculate the number of packets passed
    int seeked_packets = (int)( m_fileinfo->cPackets * fraction);

    //get the offset
    *cbDataOffset = (QWORD)averagepacketsize * seeked_packets;

    return S_OK;
}


HRESULT CASFManager::GetSeekPositionWithIndexer (
                        MFTIME hnsSeekTime,
                        QWORD *cbDataOffset,
                        MFTIME* hnsApproxSeekTime)
{
    if (! m_pIndexer)
    {
        return MF_E_ASF_NOINDEX;
    }

    BOOL  bReverse = FALSE;
    DWORD dwFlags = 0;

    HRESULT hr = m_pSplitter->GetFlags(&dwFlags);
    if (FAILED(hr))
    {
        return hr;
    }

    if (dwFlags & MFASF_SPLITTER_REVERSE)
    {
        bReverse = TRUE;
    }

    hr = ::GetSeekPositionWithIndexer(
        m_pIndexer,
        m_CurrentStreamID,
        hnsSeekTime,
        bReverse,
        cbDataOffset,
        hnsApproxSeekTime
        );

    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: GetTestDuration
//
//For audio stream, this method retrieves the duration of the test sample.
//
// hnsSeekTime: Presentation time in hns.
// hnsTestDuration: Presentation time in hns that represents the end time.
// dwFlags: Specifies splitter configuration, generate samples in reverse
//          or generate samples for protected content.
/////////////////////////////////////////////////////////////////////

void CASFManager::GetTestDuration(const MFTIME& hnsSeekTime, BOOL bReverse, MFTIME* phnsTestDuration)
{
    MFTIME hnsMaxSeekableTime = m_fileinfo->hnsPlayDuration - m_fileinfo->hnspreroll;

    if (bReverse)
    {
        // Reverse playback: The stop time should not go beyond the start of the
        // ASF Data Object (reading backwards in the file).
        if (hnsSeekTime - TEST_AUDIO_DURATION < 0)
        {
            *phnsTestDuration = 0 ;
        }
        else
        {
            *phnsTestDuration = hnsSeekTime - TEST_AUDIO_DURATION;
        }

    }
    else
    {
        // Forward playback: The stop time should not exceed the end of the presentation.
        if (hnsSeekTime + TEST_AUDIO_DURATION > hnsMaxSeekableTime)
        {
            *phnsTestDuration = hnsMaxSeekableTime ;
        }
        else
        {
            *phnsTestDuration = hnsSeekTime + TEST_AUDIO_DURATION;
        }
    }
}

/////////////////////////////////////////////////////////////////////
// Name: GenerateSamples
//
//Gets data offset for the seektime and prepares buffer for parsing.
//
// hnsSeekTime: Presentation time in hns.
// dwFlags: Specifies splitter configuration, generate samples in
//          reverse or generate samples for protected content.
// pSampleInfo: Pointer to SAMPLE_INFO structure that stores sample
//          information.
// FuncPtrToDisplaySampleInfo: Callback defined by the caller that
//          will display the sample information
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::GenerateSamples(
    MFTIME hnsSeekTime,
    DWORD dwFlags,
    SAMPLE_INFO* pSampleInfo,
    void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*)
    )
{
    if (! m_pSplitter)
    {
        return MF_E_NOT_INITIALIZED;
    }

    QWORD   cbStartOffset = 0;
    DWORD   cbReadLen = 0;
    MFTIME  hnsApproxTime =0;
    MFTIME  hnsTestSampleDuration =0;
    BOOL    bReverse = FALSE;

    // Flush the splitter to remove any samples that were delivered
    // to the ASF splitter during a previous call to this method.
    HRESULT hr = m_pSplitter->Flush();
    if (FAILED(hr))
    {
        goto done;
    }

    //set the reverse flag if applicable
    hr = m_pSplitter->SetFlags(dwFlags);
    if (FAILED (hr))
    {
        dwFlags = 0;
        hr = S_OK;
    }

    bReverse = ((dwFlags & MFASF_SPLITTER_REVERSE) == MFASF_SPLITTER_REVERSE);

    // Get the offset from the start of the ASF Data Object to the desired seek time.
    hr =  GetSeekPosition(&hnsSeekTime, &cbStartOffset, &hnsApproxTime);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the audio playback duration. (The duration is TEST_AUDIO_DURATION or up to
    // the end of the file, whichever is shorter.)
    if (m_guidCurrentMediaType == MFMediaType_Audio)
    {
        GetTestDuration(hnsSeekTime, bReverse, &hnsTestSampleDuration);
    }

    // Notify the MFT we are about to start.
    if (m_pDecoder)
    {
        if ( m_pDecoder->GetDecoderStatus() != STREAMING)
        {
            hr =  m_pDecoder->StartDecoding();
        }

        if (FAILED(hr))
        {
            SafeRelease(&m_pDecoder);
        }
    }

    cbReadLen = (DWORD)(m_cbDataLength - cbStartOffset);

    if (bReverse)
    {
        // Reverse playback: Read from the offset back to zero.

        hr = GenerateSamplesLoop(
            hnsSeekTime,
            hnsTestSampleDuration,
            bReverse,
            (DWORD)(m_cbDataLength + m_cbDataOffset - cbStartOffset), //DWORD cbDataOffset
            cbReadLen,              //DWORD cbDataLen
            pSampleInfo,
            FuncPtrToDisplaySampleInfo
            );

    }
    else
    {
        // Forward playback: Read from the offset to the end.

        hr = GenerateSamplesLoop(
            hnsSeekTime,
            hnsTestSampleDuration,
            bReverse,
            (DWORD)(m_cbDataOffset + cbStartOffset), //DWORD cbDataOffset,
            cbReadLen,                              //DWORD cbDataLen
            pSampleInfo,
            FuncPtrToDisplaySampleInfo
            );
    }

    // Note: cbStartOffset is relative to the start of the data object.
    // GenerateSamplesLoop expects the offset relative to the start of the file.


done:
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: GenerateSamplesLoop
//
//Reads 1024 * 4 byte chunks of media data from a byte stream and
//parses the ASF Data Object starting at the specified offset.
//Collects 5seconds audio samples and sends to the MFT to decode.
//Gets the first key frame for the video stream and sends to the MFT
//
// hnsSeekTime: Presentation time in hns.
// hnsTestSampleDuration: Presentation time at which the parsing should end.
// bReverse: Specifies if the splitter configured to parse in reverse.
// cbDataOffset: Offset relative to the start of the data object.
// cbDataLen: Length of data to parse
// pSampleInfo: Pointer to SAMPLE_INFO structure that stores sample
//          information.
// FuncPtrToDisplaySampleInfo: Callback defined by the caller that
//          will display the sample information
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::GenerateSamplesLoop(
    const MFTIME& hnsSeekTime,
    const MFTIME& hnsTestSampleDuration,
    BOOL  bReverse,
    DWORD cbDataOffset,
    DWORD cbDataLen,
    SAMPLE_INFO* pSampleInfo,
    void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*)
    )
{
    const DWORD READ_SIZE = 1024 * 4;

    HRESULT hr = S_OK;
    DWORD   cbRead = 0;
    DWORD   dwStatusFlags = 0;
    WORD    wStreamNumber =  0;
    BOOL    fComplete = FALSE;

    IMFSample *pSample = NULL;
    IMFMediaBuffer *pBuffer = NULL;

    MFTIME hnsCurrentSampleTime = 0;

    while (!fComplete && (cbDataLen > 0))
    {
        cbRead = min(READ_SIZE, cbDataLen);

        if (bReverse)
        {
            // Reverse playback: Read data chunks going backward from cbDataOffset.
            hr = ReadDataIntoBuffer(m_pByteStream, cbDataOffset - cbRead, cbRead, &pBuffer);
            if (FAILED(hr))
            {
                goto done;
            }

            cbDataOffset -= cbRead;
            cbDataLen -= cbRead;
        }
        else
        {
            // Forward playback: Read data chunks going forward from cbDataOffset.
            hr = ReadDataIntoBuffer(m_pByteStream, cbDataOffset, cbRead, &pBuffer);
            if (FAILED(hr))
            {
                goto done;
            }

            cbDataOffset += cbRead;
            cbDataLen -= cbRead;
        }

        // Push data on the splitter
        hr =  m_pSplitter->ParseData(pBuffer, 0, 0);
        if (FAILED(hr))
        {
            goto done;
        }

        // Start getting samples from the splitter as long as it returns ASF_STATUSFLAGS_INCOMPLETE
        do
        {
            hr = m_pSplitter->GetNextSample(&dwStatusFlags, &wStreamNumber, &pSample);
            if (FAILED(hr))
            {
                goto done;
            }

            if (pSample)
            {
                // Get sample information
                pSampleInfo->wStreamNumber = wStreamNumber;

                //if decoder is initialized, collect test data
                if (m_pDecoder)
                {
                    if (m_guidCurrentMediaType == MFMediaType_Audio)
                    {
                        // Send audio data to the decoder.
                        (void)SendAudioSampleToDecoder(pSample, hnsTestSampleDuration, bReverse, &fComplete, pSampleInfo, FuncPtrToDisplaySampleInfo);
                    }
                    else if (m_guidCurrentMediaType == MFMediaType_Video)
                    {
                        // Send video data to the decoder.
                        (void)SendKeyFrameToDecoder(pSample, hnsSeekTime, bReverse, &fComplete, pSampleInfo, FuncPtrToDisplaySampleInfo);

                    }

                    if (fComplete)
                    {
                        break;
                    }
                }
            }

            SafeRelease(&pSample);

        } while (dwStatusFlags & ASF_STATUSFLAGS_INCOMPLETE);

        SafeRelease(&pBuffer);
    }

done:
    SafeRelease(&pBuffer);
    SafeRelease(&pSample);
    return hr;
}


/////////////////////////////////////////////////////////////////////
// Name: SendAudioSampleToDecoder
//
// For audio, collect test samples and send it to the decoder
//
// pSample:  Uncompressed sample that needs to be decoded
// hnsTestSampleEndTime: Presenation time at which to stop decoding.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::SendAudioSampleToDecoder(
    IMFSample* pSample,
    const MFTIME& hnsTestSampleEndTime,
    BOOL bReverse,
    BOOL *pbComplete,
    SAMPLE_INFO* pSampleInfo,
    void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*))
{
    if (!pSample)
    {
        return E_INVALIDARG;
    }

    MFTIME hnsCurrentSampleTime = 0;
    BOOL   bShouldDecode = FALSE;

    // Get the time stamp on the sample.
    HRESULT hr = pSample->GetSampleTime(&hnsCurrentSampleTime);
    if (FAILED(hr))
    {
        goto done;
    }

    if (bReverse)
    {
        bShouldDecode = (hnsCurrentSampleTime > hnsTestSampleEndTime);
    }
    else
    {
        bShouldDecode = (hnsCurrentSampleTime < hnsTestSampleEndTime);
    }

    if (bShouldDecode)
    {

        // If the decoder is not streaming, start it.
        if ( m_pDecoder->GetDecoderStatus() != STREAMING)
        {
            hr =  m_pDecoder->StartDecoding();
            if (FAILED(hr))
            {
                goto done;
            }
        }

        hr =  m_pDecoder->ProcessAudio (pSample);
        if (FAILED(hr))
        {
            goto done;
        }

        //Get sample information
        (void)GetSampleInfo(pSample, pSampleInfo);

        //Send it to callback to display
        FuncPtrToDisplaySampleInfo(pSampleInfo);
    }
    else
    {
        //all samples have been decoded. Inform the decoder.
        hr =  m_pDecoder->StopDecoding();
        if (FAILED(hr))
        {
            goto done;
        }
    }

    *pbComplete = !bShouldDecode;

done:
    return hr;
}


/////////////////////////////////////////////////////////////////////
// Name: SendKeyFrameToDecoder
//
//For Video, get the key frame closest to the time seeked by the caller
//
// pSample:  Uncompressed sample that needs to be decoded
// hnsTestSampleEndTime: Presenation time at which to stop decoding.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::SendKeyFrameToDecoder(
    IMFSample* pSample,
    const MFTIME& hnsSeekTime,
    BOOL bReverse,
    BOOL* fDecodedKeyFrame,
    SAMPLE_INFO* pSampleInfo,
    void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*))
{
    if (!pSample)
    {
        return E_INVALIDARG;
    }

    MFTIME hnsCurrentSampleTime =0;

    BOOL   fShouldDecode = FALSE;
    UINT32 fIsKeyFrame = 0;

    IMFSample* pKeyFrameSample = NULL;

    // Get the time stamp on the sample
    HRESULT hr = pSample->GetSampleTime (&hnsCurrentSampleTime);
    if (FAILED(hr))
    {
        goto done;
    }

    if ((UINT64)hnsCurrentSampleTime > m_fileinfo->hnspreroll)
    {
        hnsCurrentSampleTime -= m_fileinfo->hnspreroll;
    }

    // Check if the key-frame attribute is set on the sample
    fIsKeyFrame = MFGetAttributeUINT32(pSample, MFSampleExtension_CleanPoint, FALSE);

    if (!fIsKeyFrame)
    {
        return hr;
    }

    // Should we decode this sample?
    if (bReverse)
    {
        // Reverse playback:
        // Is the sample *prior* to the seek time, and a key frame?
        fShouldDecode = (hnsCurrentSampleTime <= hnsSeekTime) ;
    }
    else
    {
        // Forward playback:
        // Is the sample *after* the seek time, and a key frame?
        fShouldDecode = (hnsCurrentSampleTime >= hnsSeekTime);
    }

    if (fShouldDecode)
    {
        // We found the key frame closest to the seek time.
        // Start the decoder if not already started.
        if ( m_pDecoder->GetDecoderStatus() != STREAMING)
        {
            hr =  m_pDecoder->StartDecoding();
            if (FAILED(hr))
            {
                goto done;
            }
        }

        // Set the discontinity attribute.
        hr = pSample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        if (FAILED(hr))
        {
            goto done;
        }

        //Send the sample to the decoder.
        hr =  m_pDecoder->ProcessVideo(pSample);
        if (FAILED(hr))
        {
            goto done;
        }

        *fDecodedKeyFrame = TRUE;

        //Get sample information
        (void)GetSampleInfo(pSample, pSampleInfo);
        pSampleInfo->fSeekedKeyFrame = *fDecodedKeyFrame;

        //Send it to callback to display
        FuncPtrToDisplaySampleInfo(pSampleInfo);

        hr =  m_pDecoder->StopDecoding();
    }

done:
    return hr;
}

/////////////////////////////////////////////////////////////////////
// Name: ReadDataIntoBuffer
//
// Reads data from a byte stream and returns a media buffer that
// contains the data.
//
// pStream:  Pointer to the byte stream
// cbOffset: Offset at which to start reading
// cbToRead: Number of bytes to read
// ppBuffer: Receives a pointer to the buffer.
/////////////////////////////////////////////////////////////////////

HRESULT CASFManager::ReadDataIntoBuffer(
    IMFByteStream *pStream,     // Pointer to the byte stream.
    DWORD cbOffset,             // Offset at which to start reading
    DWORD cbToRead,             // Number of bytes to read
    IMFMediaBuffer **ppBuffer   // Receives a pointer to the buffer.
    )
{
    BYTE *pData = NULL;
    DWORD cbRead = 0;   // Actual amount of data read

    IMFMediaBuffer *pBuffer = NULL;

    // Create the media buffer. This function allocates the memory.
    HRESULT hr = MFCreateMemoryBuffer(cbToRead, &pBuffer);
    if (FAILED(hr))
    {
        goto done;
    }

    // Access the buffer.
    hr = pBuffer->Lock(&pData, NULL, NULL);
    if (FAILED(hr))
    {
        goto done;
    }

    //Set the offset
    hr = pStream->SetCurrentPosition(cbOffset);
    if (FAILED(hr))
    {
        goto done;
    }

    // Read the data from the byte stream.
    hr = pStream->Read(pData, cbToRead, &cbRead);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pBuffer->Unlock();
    pData = NULL;

    if (FAILED(hr))
    {
        goto done;
    }

    // Update the size of the valid data.
    hr = pBuffer->SetCurrentLength(cbRead);
    if (FAILED(hr))
    {
        goto done;
    }

    // Return the pointer to the caller.
    *ppBuffer = pBuffer;
    (*ppBuffer)->AddRef();

done:
    if (pData)
    {
        pBuffer->Unlock();
    }
    SafeRelease(&pBuffer);
    return hr;
}

//////////////////////////////////////////////////////////////////////////
//  Name: SetFilePropertiesObject
//  Description: Retrieves ASF File Object information through attributes on
//  the presentation descriptor that is generated from Content Info
//
/////////////////////////////////////////////////////////////////////////

HRESULT CASFManager::SetFilePropertiesObject(FILE_PROPERTIES_OBJECT* fileinfo)
{
    if (! m_pContentInfo)
    {
        return MF_E_NOT_INITIALIZED;
    }

    IMFPresentationDescriptor *pPD = NULL;

    UINT32 cbBlobSize = 0;

    HRESULT hr =  m_pContentInfo->GeneratePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
        goto done;
    }

    //get File ID
    hr = pPD->GetGUID(MF_PD_ASF_FILEPROPERTIES_FILE_ID, &fileinfo->guidFileID);
    if (FAILED(hr))
    {
        goto done;
    }

    // get Creation Time
    (void)pPD->GetBlob(MF_PD_ASF_FILEPROPERTIES_CREATION_TIME, (BYTE *)&fileinfo->ftCreationTime, sizeof(FILETIME), &cbBlobSize);

    fileinfo->cPackets = MFGetAttributeUINT32(pPD, MF_PD_ASF_FILEPROPERTIES_PACKETS, 0);
    fileinfo->hnsPlayDuration = MFGetAttributeUINT64(pPD, MF_PD_ASF_FILEPROPERTIES_PLAY_DURATION, 0);
    fileinfo->hnsPresentationDuration = MFGetAttributeUINT64(pPD, MF_PD_DURATION, 0);
    fileinfo->hnsSendDuration = MFGetAttributeUINT64(pPD, MF_PD_ASF_FILEPROPERTIES_SEND_DURATION, 0);
    fileinfo->hnspreroll = MFGetAttributeUINT64(pPD, MF_PD_ASF_FILEPROPERTIES_PREROLL, 0) * 10000;      // Pre-roll is in msec
    fileinfo->flags = MFGetAttributeUINT32(pPD, MF_PD_ASF_FILEPROPERTIES_FLAGS, 0);
    fileinfo->cbMaxPacketSize = MFGetAttributeUINT32(pPD, MF_PD_ASF_FILEPROPERTIES_MAX_PACKET_SIZE, 0);
    fileinfo->cbMinPacketSize = MFGetAttributeUINT32(pPD, MF_PD_ASF_FILEPROPERTIES_MIN_PACKET_SIZE, 0);
    fileinfo->MaxBitRate = MFGetAttributeUINT32(pPD, MF_PD_ASF_FILEPROPERTIES_MAX_BITRATE, 0);

    m_fileinfo = fileinfo;

done:
    SafeRelease(&pPD);
    return hr;
}

//////////////////////////////////////////////////////////////////////////
//  Name: GetSampleInfo
//  Description: Retrieves sample information from the sample generated by the splitter
//
//  pSample: Pointer to the sample object
//  pSampleInfo: Pointer to the SAMPLE_INFO structure tha stores the sample information.
//
/////////////////////////////////////////////////////////////////////////

HRESULT CASFManager::GetSampleInfo(IMFSample *pSample, SAMPLE_INFO* pSampleInfo)
{
    if (!pSampleInfo || !pSample)
    {
        return E_INVALIDARG;
    }


    //Number of buffers in the sample
    HRESULT hr = pSample->GetBufferCount(&pSampleInfo->cBufferCount);
    if (FAILED(hr))
    {
        goto done;
    }

    //Total buffer length
    hr = pSample->GetTotalLength(&pSampleInfo->cbTotalLength);
    if (FAILED(hr))
    {
        goto done;
    }

    //Sample time
    hr = pSample->GetSampleTime(&pSampleInfo->hnsSampleTime);

    if (hr == MF_E_NO_SAMPLE_TIMESTAMP)
    {
        hr = S_OK;
    }

done:
    return hr;
}

//////////////////////////////////////////////////////////////////////////
//  Name: Reset
//  Description: Releases the existing ASF objects for the current file
//
/////////////////////////////////////////////////////////////////////////

void CASFManager::Reset()
{
    SafeRelease(&m_pContentInfo);
    SafeRelease(&m_pDataBuffer);
    SafeRelease(&m_pIndexer);
    SafeRelease(&m_pSplitter);

    SafeRelease(&m_pByteStream);
    m_cbDataOffset = 0;
    m_cbDataLength = 0;

    if (m_pDecoder)
    {
        m_pDecoder->Reset();
        SafeRelease(&m_pDecoder);
    }

    if( m_fileinfo)
    {
        m_fileinfo = NULL;
    }

}


HRESULT CreateASFIndexer(
    IMFByteStream *pContentByteStream,  // Pointer to the content byte stream
    IMFASFContentInfo *pContentInfo,
    IMFASFIndexer **ppIndexer
    )
{
    IMFASFIndexer *pIndexer = NULL;
    IMFByteStream *pIndexerByteStream = NULL;

    QWORD qwLength = 0, qwIndexOffset = 0, qwBytestreamLength = 0;

    // Create the indexer.
    HRESULT hr = MFCreateASFIndexer(&pIndexer);
    if (FAILED(hr))
    {
        goto done;
    }

    //Initialize the indexer to work with this ASF library
    hr =  pIndexer->Initialize(pContentInfo);
    if (FAILED(hr))
    {
        goto done;
    }

    //Check if the index exists. You can only do this after creating the indexer

    //Get byte stream length
    hr = pContentByteStream->GetLength(&qwLength);
    if (FAILED(hr))
    {
        goto done;
    }

    //Get index offset
    hr = pIndexer->GetIndexPosition(pContentInfo, &qwIndexOffset);
    if (FAILED(hr))
    {
        goto done;
    }

    if ( qwIndexOffset >= qwLength)
    {
        //index object does not exist, release the indexer
        goto done;
    }
    else
    {
        // initialize the indexer
        // Create a byte stream that the Indexer will use to read in
        // and parse the indexers.
         hr = MFCreateASFIndexerByteStream( 
             pContentByteStream,
             qwIndexOffset,
             &pIndexerByteStream 
             );

        if (FAILED(hr))
        {
            goto done;
        }
   }

    hr = pIndexer->SetIndexByteStreams(&pIndexerByteStream, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Return the pointer to the caller.
    *ppIndexer = pIndexer;
    (*ppIndexer)->AddRef();

done:
    SafeRelease(&pIndexer);
    SafeRelease(&pIndexerByteStream);
    return hr;
}



/////////////////////////////////////////////////////////////////////
// Name: GetSeekPositionWithIndexer
//
// Gets the offset for video media types that have ASF Index Objects defined.
//
// hnsSeekTime: Presentation time in hns.
//
// cbDataOffset: Receives the offset in bytes.
//
/////////////////////////////////////////////////////////////////////

HRESULT GetSeekPositionWithIndexer(
    IMFASFIndexer *pIndexer,
    WORD          wStreamNumber,
    MFTIME        hnsSeekTime,          // Desired seek time, in 100-nsec.
    BOOL          bReverse,
    QWORD         *pcbDataOffset,       // Receives the offset in bytes.
    MFTIME        *phnsApproxSeekTime   // Receives the approximate seek time.
    )
{
    // Query whether the stream is indexed.

    ASF_INDEX_IDENTIFIER IndexIdentifier = { GUID_NULL, wStreamNumber };

    BOOL fIsIndexed = FALSE;

    ASF_INDEX_DESCRIPTOR descriptor;

    DWORD cbIndexDescriptor = sizeof(descriptor);

    HRESULT hr = pIndexer->GetIndexStatus( 
        &IndexIdentifier,
        &fIsIndexed,
        (BYTE*)&descriptor,
        &cbIndexDescriptor 
        );

    if (hr == MF_E_BUFFERTOOSMALL)
    {
        hr = S_OK;
    }
    else if (FAILED(hr))
    {
        goto done;
    }

    if (!fIsIndexed)
    {
        hr = MF_E_ASF_NOINDEX;
        goto done;
    }

    if (bReverse)
    {
        hr = pIndexer->SetFlags(MFASF_INDEXER_READ_FOR_REVERSEPLAYBACK);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    // Get the offset from the indexer.

    PROPVARIANT var;

    var.vt = VT_I8;
    var.hVal.QuadPart = hnsSeekTime;

    hr = pIndexer->GetSeekPositionForValue(
        &var, 
        &IndexIdentifier, 
        pcbDataOffset, 
        phnsApproxSeekTime, 
        0
        );

done:
    return hr;
}
