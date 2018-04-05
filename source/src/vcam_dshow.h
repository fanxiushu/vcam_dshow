////by fanxiushu 2018-03-08
////没使用DirectShow的SDK开发，但是借鉴了DSHOW的代码

#pragma once

#include <Windows.h>
#include <uuids.h>
#include <Strmif.h>
#include <vfwmsgs.h>
#include <amvideo.h>
#include <evcode.h>
#include "vcamera.h"

#pragma comment(lib,"Strmiids.lib")

#define SAFE_RELEASE(A)  if(A){ (A)->Release(); (A)=NULL; }

const LONGLONG MILLISECONDS = (1000);            // 10 ^ 3
const LONGLONG NANOSECONDS = (1000000000);       // 10 ^ 9
const LONGLONG UNITS = (NANOSECONDS / 100);      // 10 ^ 7
#define MILLISECONDS_TO_100NS_UNITS(lMs) \
    Int32x32To64((lMs), (UNITS/MILLISECONDS))

/////
struct CLock{
	CRITICAL_SECTION& m_cs;
	CLock(CRITICAL_SECTION& cs):m_cs(cs) {
		EnterCriticalSection(&m_cs);
	}
	~CLock() { 
		LeaveCriticalSection(&m_cs); 
	}
};

///
DECLARE_INTERFACE(INonDelegatingUnknown)
{
	STDMETHOD(NonDelegatingQueryInterface) (THIS_ REFIID, LPVOID *) PURE;
	STDMETHOD_(ULONG, NonDelegatingAddRef)(THIS) PURE;
	STDMETHOD_(ULONG, NonDelegatingRelease)(THIS) PURE;
};

///
class VCamStream;
///
class VCamDShow: public INonDelegatingUnknown,
	public IBaseFilter, public IAMovieSetup
{
	friend class VCamStream;
	friend class CEnumPins;
	friend class VCamDShowFactory;
protected:
	LONG m_RefCount;

	CRITICAL_SECTION m_cs;
	
	IUnknown*       m_pUnk;
	CLSID           m_clsid;
	///
	VCamStream*     m_Stream; ///
	VCamStream*     m_AllPins[10];
	LONG            m_AllPinsCount; /// 

	//
	IFilterGraph    *m_pGraph;
	IMediaEventSink *m_pSink;
	PWCHAR           m_pName;

	///
	FILTER_STATE     m_State; //停止，允许，暂停状态
	static DWORD CALLBACK helper_thread(void* p);
	void setState(FILTER_STATE state);

	IReferenceClock* m_pClock; ///
	REFERENCE_TIME   m_tStart; ///
	HRESULT getStreamTime(REFERENCE_TIME& now); // get Stream Time for PTS 

	/// callback
	FRAME_CALLBACK   m_callback;
	void*            m_param;

public:
	VCamDShow(IUnknown* pUnk, REFCLSID clsid, FRAME_CALLBACK cbk, void* param );
	~VCamDShow();

public:
	////INonDelegatingUnknown
	virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject);
	virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef(void);
	virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease(void);

	/// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject) {
		return m_pUnk->QueryInterface(riid, ppvObject);
	}
	virtual ULONG STDMETHODCALLTYPE AddRef(void) { return m_pUnk->AddRef(); }
	virtual ULONG STDMETHODCALLTYPE Release(void) { return m_pUnk->Release(); }

	////IPersist
	virtual HRESULT STDMETHODCALLTYPE GetClassID(
		__RPC__out CLSID *pClassID);

	////IMediaFilter
	virtual HRESULT STDMETHODCALLTYPE Stop(void);
	virtual HRESULT STDMETHODCALLTYPE Pause(void);
	virtual HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME tStart);
	virtual HRESULT STDMETHODCALLTYPE GetState(
		DWORD dwMilliSecsTimeout,
		__out  FILTER_STATE *State)
	{
		if (!State)return E_POINTER;
		CLock lck(m_cs);
		*State = m_State;
		return S_OK;
	}
	virtual HRESULT STDMETHODCALLTYPE SetSyncSource(
		__in_opt  IReferenceClock *pClock);
	virtual HRESULT STDMETHODCALLTYPE GetSyncSource(
		__deref_out_opt  IReferenceClock **pClock);

	/////IBaseFilter
	virtual HRESULT STDMETHODCALLTYPE EnumPins(
		__out  IEnumPins **ppEnum);
	virtual HRESULT STDMETHODCALLTYPE FindPin(
		  LPCWSTR Id, 
		__out  IPin **ppPin);
	virtual HRESULT STDMETHODCALLTYPE QueryFilterInfo(
		__out  FILTER_INFO *pInfo);
	virtual HRESULT STDMETHODCALLTYPE JoinFilterGraph(
		__in_opt  IFilterGraph *pGraph,
		__in_opt  LPCWSTR pName);
	virtual HRESULT STDMETHODCALLTYPE QueryVendorInfo(
		__out  LPWSTR *pVendorInfo)
	{
		return E_NOTIMPL;
	}

	//////IAMovieSetup
	virtual HRESULT STDMETHODCALLTYPE Register(void);
	virtual HRESULT STDMETHODCALLTYPE Unregister(void);

	////other
	HRESULT NotifyEvent(long EventCode,
		LONG_PTR EventParam1, LONG_PTR EventParam2);
};

/// stream
class VCamStream : public INonDelegatingUnknown,
	public IPin, public IQualityControl, public IAMStreamConfig, public IKsPropertySet
{
	friend class VCamDShow;
	friend class CEnumMediaTypes;
protected:

	////
	IUnknown*       m_pUnk;

	inline void Lock() { EnterCriticalSection(&m_pFilter->m_cs); }
	inline void Unlock() { LeaveCriticalSection(&m_pFilter->m_cs); }

	///
	VCamDShow*     m_pFilter; ////
	PWCHAR         m_PinName;
	///
	IPin*          m_ConnectedPin;
	PIN_DIRECTION  m_Dir; //方向
	AM_MEDIA_TYPE  m_MediaType; /// 当前连接允许的TYPE

	///
	AM_MEDIA_TYPE** m_AllSupportedMediaTypes;
	LONG            m_AllSupportedMediaTypesCount;

	///
	IMemInputPin*  m_pInputPin;
	IMemAllocator* m_pAlloc;

	///
	REFERENCE_TIME m_tStart, m_tStop;
	double         m_dRate;

	IQualityControl *m_pQSink;
	///
	HRESULT doAlloc();
	HRESULT doConnect(IPin* pRecvPin, const AM_MEDIA_TYPE* mt, BOOL bRecvPin );
	BOOL IsStopped() { return (m_pFilter->m_State == State_Stopped) ? TRUE : FALSE; }
	HRESULT Active(BOOL bActive);

	////
	HANDLE  m_hThread; ///
	HANDLE  m_event;
	BOOL    m_quit;   ////
	static DWORD CALLBACK thread(void* _p) {
		VCamStream* p = (VCamStream*)_p;
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		p->StreamTreadLoop(); 
		CoUninitialize();
		return 0;
	}
	void StreamTreadLoop();

	///
public:
	VCamStream(VCamDShow* parent);
	~VCamStream();

public:
	////INonDelegatingUnknown
	virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject);
	virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef(void);
	virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease(void);

	/// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject) {
		return m_pUnk->QueryInterface(riid, ppvObject);
	}
	virtual ULONG STDMETHODCALLTYPE AddRef(void) { return m_pUnk->AddRef(); }
	virtual ULONG STDMETHODCALLTYPE Release(void) { return m_pUnk->Release(); }

	////IPin
	virtual HRESULT STDMETHODCALLTYPE Connect(
		/* [in] */ IPin *pReceivePin,
		__in_opt  const AM_MEDIA_TYPE *pmt);
	virtual HRESULT STDMETHODCALLTYPE ReceiveConnection(
		/* [in] */ IPin *pConnector,
		/* [in] */ const AM_MEDIA_TYPE *pmt);
	virtual HRESULT STDMETHODCALLTYPE Disconnect(void) ;
	virtual HRESULT STDMETHODCALLTYPE ConnectedTo(__out  IPin **pPin);
	virtual HRESULT STDMETHODCALLTYPE ConnectionMediaType(__out  AM_MEDIA_TYPE *pmt);
	virtual HRESULT STDMETHODCALLTYPE QueryPinInfo(__out  PIN_INFO *pInfo);
	virtual HRESULT STDMETHODCALLTYPE QueryDirection(__out  PIN_DIRECTION *pPinDir);
	virtual HRESULT STDMETHODCALLTYPE QueryId(__out  LPWSTR *Id);
	virtual HRESULT STDMETHODCALLTYPE QueryAccept(/* [in] */ const AM_MEDIA_TYPE *pmt);
	virtual HRESULT STDMETHODCALLTYPE EnumMediaTypes(__out  IEnumMediaTypes **ppEnum);
	virtual HRESULT STDMETHODCALLTYPE QueryInternalConnections(
		__out_ecount_part_opt(*nPin, *nPin)  IPin **apPin,
		/* [out][in] */ ULONG *nPin);
	virtual HRESULT STDMETHODCALLTYPE EndOfStream(void);
	virtual HRESULT STDMETHODCALLTYPE BeginFlush(void);
	virtual HRESULT STDMETHODCALLTYPE EndFlush(void);
	virtual HRESULT STDMETHODCALLTYPE NewSegment(
		/* [in] */ REFERENCE_TIME tStart,
		/* [in] */ REFERENCE_TIME tStop,
		/* [in] */ double dRate);

	/////IQualityControl
	virtual HRESULT STDMETHODCALLTYPE Notify(
		/* [in] */ IBaseFilter *pSelf,
		/* [in] */ Quality q);
	virtual HRESULT STDMETHODCALLTYPE SetSink(
		/* [in] */ IQualityControl *piqc);


	////IAMStreamConfig
	virtual HRESULT STDMETHODCALLTYPE SetFormat(
		/* [in] */ AM_MEDIA_TYPE *pmt);
	virtual HRESULT STDMETHODCALLTYPE GetFormat(
		__out  AM_MEDIA_TYPE **ppmt);
	virtual HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(
		__out  int *piCount,
		__out  int *piSize) ;
	virtual HRESULT STDMETHODCALLTYPE GetStreamCaps(
		/* [in] */ int iIndex,
		__out  AM_MEDIA_TYPE **ppmt,
		__out  BYTE *pSCC);

	
	//////IKsPropertySet
	virtual /* [local] */ HRESULT STDMETHODCALLTYPE Set(
		/* [in] */ REFGUID guidPropSet,
		/* [in] */ DWORD dwPropID,
		/* [annotation][size_is][in] */
		__in_bcount(cbInstanceData)  LPVOID pInstanceData,
		/* [in] */ DWORD cbInstanceData,
		/* [annotation][size_is][in] */
		__in_bcount(cbPropData)  LPVOID pPropData,
		/* [in] */ DWORD cbPropData) 
	{
		printf("VCamStream::Set [IKsPropertySet] \n");
		return E_NOTIMPL;
	}

	virtual /* [local] */ HRESULT STDMETHODCALLTYPE Get(
		/* [in] */ REFGUID guidPropSet,
		/* [in] */ DWORD dwPropID,
		/* [annotation][size_is][in] */
		__in_bcount(cbInstanceData)  LPVOID pInstanceData,
		/* [in] */ DWORD cbInstanceData,
		/* [annotation][size_is][out] */
		__out_bcount_part(cbPropData, *pcbReturned)  LPVOID pPropData,
		/* [in] */ DWORD cbPropData,
		/* [annotation][out] */
		__out  DWORD *pcbReturned);

	virtual HRESULT STDMETHODCALLTYPE QuerySupported(
		/* [in] */ REFGUID guidPropSet,
		/* [in] */ DWORD dwPropID,
		/* [annotation][out] */
		__out  DWORD *pTypeSupport);

};


//////////////// FORMAT
//
//0x32595559, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71, /* // YUY2 //
#define FOURCC_YUY2  0x32595559

#define BITCOUNT     16

#define VCAM_VIDEO_HEADER(DX,DY, INDEX) \
    static VIDEOINFOHEADER VCAM_VideoHeader_##INDEX = \
	{\
        0,0,0,0,                            /* RECT  rcSource; */\
        0,0,0,0,                            /* RECT  rcTarget; */\
        DX*DY*30*BITCOUNT/8,               /* DWORD dwBitRate;*/\
        0L,                                 /* DWORD dwBitErrorRate;   */\
        333667,                             /* REFERENCE_TIME  AvgTimePerFrame (30 FPS); */\
        sizeof (BITMAPINFOHEADER),       /* DWORD biSize;*/\
        DX,                                 /* LONG  biWidth;*/\
        DY,                                /* LONG  biHeight; -biHeight indicate TopDown for RGB*/\
        1,                         /* WORD  biPlanes;*/\
        BITCOUNT,                 /* WORD  biBitCount;*/\
        FOURCC_YUY2,                 /* DWORD biCompression;*/\
        DX*DY*BITCOUNT/8,         /* DWORD biSizeImage;*/\
        0,                         /* LONG  biXPelsPerMeter;*/\
        0,                         /* LONG  biYPelsPerMeter;*/\
        0,                         /* DWORD biClrUsed;*/\
        0                          /* DWORD biClrImportant;*/\
    }

/////
#define VCAM_MEDIA_TYPE(DX,DY, INDEX) \
	VCAM_VIDEO_HEADER(DX,DY, INDEX); \
	static AM_MEDIA_TYPE VCAM_MediaType_##INDEX = \
	{\
		MEDIATYPE_Video,  /*majortype*/\
		MEDIASUBTYPE_YUY2, /*subtype*/\
		TRUE, /*bFixedSizeSamples*/\
		FALSE, /*bTemporalCompression*/\
		DX*DY*BITCOUNT/8, /*lSampleSize*/\
		FORMAT_VideoInfo, /*formattype*/\
		NULL, /*pUnk*/\
		sizeof(VIDEOINFOHEADER), /*cbFormat*/\
		(BYTE*)&VCAM_VideoHeader_##INDEX /*pbFormat*/\
    }

///////
VCAM_MEDIA_TYPE(1280, 720, 1);
VCAM_MEDIA_TYPE(1920, 1080, 2);
VCAM_MEDIA_TYPE(720, 405, 3);
VCAM_MEDIA_TYPE(640, 480, 4);
VCAM_MEDIA_TYPE(320, 240, 5);

static AM_MEDIA_TYPE* VCamAllMediaTypes[]=
{
	&VCAM_MediaType_1,
	&VCAM_MediaType_2,
	&VCAM_MediaType_3,
	&VCAM_MediaType_4,
//	&VCAM_MediaType_5,
};


////// CLSID for VCamDShow 

// {01AB6ACC-35F9-41B4-B308-05CA6070749B}
static const GUID CLSID_VCamDShow =
{ 0x1ab6acc, 0x35f9, 0x41b4,{ 0xb3, 0x8, 0x5, 0xca, 0x60, 0x70, 0x74, 0x9b } };

