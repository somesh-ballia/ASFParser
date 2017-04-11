//////////////////////////////////////////////////////////////////////////
//
// ASFManager.h : CASFManager class declaration.
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

struct FILE_PROPERTIES_OBJECT
{
    GUID guidFileID;
    FILETIME ftCreationTime;
    UINT32 MaxBitRate;
    UINT32 cbMaxPacketSize;
    UINT32 cbMinPacketSize;
    UINT32 cPackets;
    UINT64 hnsPlayDuration;
    UINT64 hnsSendDuration;
    UINT32 flags;
    UINT64 hnspreroll;
    UINT64 hnsPresentationDuration;

    FILE_PROPERTIES_OBJECT()
        :
    MaxBitRate(0),
    cbMaxPacketSize(0),
    cbMinPacketSize(0),
    cPackets(0),
    hnsPlayDuration(0),
    hnsSendDuration(0),
    hnspreroll(0),
    hnsPresentationDuration(0),
    flags(0)
    {}
};


struct SAMPLE_INFO
{
    UINT32 fSeekedKeyFrame;
    DWORD wStreamNumber;
    DWORD cBufferCount;
    LONGLONG hnsSampleTime;
    DWORD cbTotalLength;


    SAMPLE_INFO()
        :
    wStreamNumber(0),
    cBufferCount(0),
    hnsSampleTime(0),
    cbTotalLength(0),
    fSeekedKeyFrame(0)
    {}

};

class CASFManager : public IUnknown
{

public:

    static HRESULT CASFManager::CreateInstance(CASFManager **ppASFManager);

    CASFManager(HRESULT* hr);


    ~CASFManager();

    HRESULT OpenASFFile(const WCHAR *sFileName);

    HRESULT EnumerateStreams (
        WORD** ppwStreamNumbers,
        GUID** ppguidMajorType,
        DWORD* cbTotalStreams);

    HRESULT SelectStream(WORD wStreamNumber, GUID* pguidCurrentMediaType);

    HRESULT GetSeekPosition(
        MFTIME* seektime,
        QWORD *cbDataOffset,
        MFTIME* hnsApproxSeekTime
        );

    HRESULT SetFilePropertiesObject(FILE_PROPERTIES_OBJECT* fileinfo);

    HRESULT GenerateSamples(
        MFTIME hnsSeekTime,
        DWORD dwFlags,
        SAMPLE_INFO *pSampleInfo,
        void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*)
        );

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        static const QITAB qit[] = 
        {
            QITABENT(CASFManager, IUnknown),
            { 0 }
        };
        return QISearch(this, qit, riid, ppv);
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_nRefCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG uCount = InterlockedDecrement(&m_nRefCount);
        if (uCount == 0)
        {
            delete this;
        }
        return uCount;
    }

    HRESULT GetMediaController (CMediaController** pMediaController)
    {
        if (!m_pDecoder)
        {
            return MF_E_NOT_INITIALIZED;
        }

        return m_pDecoder->GetMediaController(pMediaController);
    }

protected:

    HRESULT CreateASFContentInfo(IMFByteStream *pContentByteStream, IMFASFContentInfo **ppContentInfo);

    HRESULT CreateASFSplitter(IMFByteStream *pContentByteStream, IMFASFSplitter **ppSplitter);

    HRESULT ReadDataIntoBuffer(
        IMFByteStream *pStream,
        DWORD cbOffset,
        DWORD cbToRead,
        IMFMediaBuffer **ppBuffer
        );

    HRESULT SetupStreamDecoder(WORD wStreamNumber, GUID* pguidCurrentMediaType);

    HRESULT GetSeekPositionManually(MFTIME hnsSeekTime, QWORD *cbDataOffset);

    HRESULT GetSeekPositionWithIndexer(
        MFTIME hnsSeekTime,
        QWORD *cbDataOffset,
        MFTIME* hnsApproxSeekTime
        );

    void GetTestDuration(
        const MFTIME& hnsSeekTime,
        BOOL bReverse,
        MFTIME* phnsTestDuration
        );

    HRESULT SendAudioSampleToDecoder (
        IMFSample* pSample,
        const MFTIME& hnsTestSampleEndTime,
        BOOL bReverse,
        BOOL *pbComplete,
        SAMPLE_INFO* pSampleInfo,
        void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*));

    HRESULT SendKeyFrameToDecoder (
        IMFSample* pSample,
        const MFTIME& hnsSeekTime,
        BOOL bReverse,
        BOOL* fDecodedKeyFrame,
        SAMPLE_INFO* pSampleInfo,
        void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*));

    HRESULT GetSampleInfo(IMFSample *pSample, SAMPLE_INFO *pSampleInfo);

    void Reset();

    HRESULT GenerateSamplesLoop(
        const MFTIME& hnsSeekTime,
        const MFTIME& hnsTestSampleDuration,
        BOOL  bReverse,
        DWORD cbDataOffset,
        DWORD cbDataLen,
        SAMPLE_INFO* pSampleInfo,
        void (*FuncPtrToDisplaySampleInfo)(SAMPLE_INFO*)
        );

protected:
    long    m_nRefCount;    // Reference count

    //currently selected stream information
    WORD        m_CurrentStreamID;
    GUID        m_guidCurrentMediaType;

    CDecoder* m_pDecoder;

    //File info
    FILE_PROPERTIES_OBJECT* m_fileinfo;

    //Media Foundation ASF components
    IMFASFContentInfo*  m_pContentInfo;
    IMFASFSplitter*     m_pSplitter;
    IMFASFIndexer*      m_pIndexer;
    IMFMediaBuffer*     m_pDataBuffer;


    // TEST!
    IMFByteStream*      m_pByteStream;
    UINT64              m_cbDataOffset;
    UINT64              m_cbDataLength;

};