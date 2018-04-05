/// by fanxiushu 2018-03-08
/// 没使用DirectShow的SDK开发，但是借鉴了DSHOW的代码

#include <windows.h>
#include <stdio.h>

#include "vcam_dshow.h"
#include "vcamera.h"

///MediaType, 从DSHOW复制
HRESULT CopyMediaType(__out AM_MEDIA_TYPE *pmtTarget, const AM_MEDIA_TYPE *pmtSource);
void FreeMediaType(__inout AM_MEDIA_TYPE& mt);

BOOL EqualMediaType(AM_MEDIA_TYPE*a, AM_MEDIA_TYPE* b)
{
	if ((IsEqualGUID(a->majortype, b->majortype)) &&
		(IsEqualGUID(a->subtype, b->subtype)) &&
		(IsEqualGUID(a->formattype, b->formattype)) &&
		(a->cbFormat == b->cbFormat))
	{
		if (a->cbFormat == 0)return TRUE;
		////
		VIDEOINFOHEADER* p1 = (VIDEOINFOHEADER*)a->pbFormat;
		VIDEOINFOHEADER* p2 = (VIDEOINFOHEADER*)b->pbFormat;
		if (p1->bmiHeader.biWidth == p2->bmiHeader.biWidth && p1->bmiHeader.biHeight == p2->bmiHeader.biHeight
			&& p1->bmiHeader.biBitCount == p2->bmiHeader.biBitCount)
			return TRUE;
	}
	/////
	return FALSE;
}
void DeleteMediaType(__inout_opt AM_MEDIA_TYPE *pmt)
{
	// allow NULL pointers for coding simplicity

	if (pmt == NULL) {
		return;
	}

	FreeMediaType(*pmt);
	CoTaskMemFree((PVOID)pmt);
}


// this also comes in useful when using the IEnumMediaTypes interface so
// that you can copy a media type, you can do nearly the same by creating
// a CMediaType object but as soon as it goes out of scope the destructor
// will delete the memory it allocated (this takes a copy of the memory)

AM_MEDIA_TYPE * CreateMediaType(AM_MEDIA_TYPE const *pSrc)
{

	// Allocate a block of memory for the media type

	AM_MEDIA_TYPE *pMediaType =
		(AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));

	if (pMediaType == NULL) {
		return NULL;
	}
	// Copy the variable length format block

	HRESULT hr = CopyMediaType(pMediaType, pSrc);
	if (FAILED(hr)) {
		CoTaskMemFree((PVOID)pMediaType);
		return NULL;
	}

	return pMediaType;
}


//  Copy 1 media type to another

HRESULT CopyMediaType(__out AM_MEDIA_TYPE *pmtTarget, const AM_MEDIA_TYPE *pmtSource)
{
	//  We'll leak if we copy onto one that already exists - there's one
	//  case we can check like that - copying to itself.

	*pmtTarget = *pmtSource;
	if (pmtSource->cbFormat != 0) {

		pmtTarget->pbFormat = (PBYTE)CoTaskMemAlloc(pmtSource->cbFormat);
		if (pmtTarget->pbFormat == NULL) {
			pmtTarget->cbFormat = 0;
			return E_OUTOFMEMORY;
		}
		else {
			CopyMemory((PVOID)pmtTarget->pbFormat, (PVOID)pmtSource->pbFormat,
				pmtTarget->cbFormat);
		}
	}
	if (pmtTarget->pUnk != NULL) {
		pmtTarget->pUnk->AddRef();
	}

	return S_OK;
}

//  Free an existing media type (ie free resources it holds)

void FreeMediaType(__inout AM_MEDIA_TYPE& mt)
{
	if (mt.cbFormat != 0) {
		CoTaskMemFree((PVOID)mt.pbFormat);

		// Strictly unnecessary but tidier
		mt.cbFormat = 0;
		mt.pbFormat = NULL;
	}
	if (mt.pUnk != NULL) {
		mt.pUnk->Release();
		mt.pUnk = NULL;
	}
}

///////
VCamStream::VCamStream(VCamDShow* parent)
{
	m_pUnk = (IUnknown*)static_cast<INonDelegatingUnknown*>(this);
	////
	m_pFilter = parent;
	m_PinName = L"VCamera OutputPin";
	
	///
	m_ConnectedPin = 0;
	m_Dir = PINDIR_OUTPUT; /// 
	m_pInputPin = NULL;
	m_pAlloc = NULL;
	
	////
	m_AllSupportedMediaTypes = VCamAllMediaTypes;
	m_AllSupportedMediaTypesCount = sizeof(VCamAllMediaTypes) / sizeof(VCamAllMediaTypes[0]);

	CopyMediaType(&m_MediaType, VCamAllMediaTypes[0]); //设置默认模式
	/////

	m_tStart = m_tStop = 0;
	m_dRate = 30;
	m_pQSink = NULL;

	/////
	m_quit = false;
	m_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	DWORD tid;
	m_hThread =  CreateThread(NULL, 0, thread, this, 0, &tid);

	//////

}
VCamStream::~VCamStream()
{
	///
	m_quit = true;
	SetEvent(m_event);
	if (::WaitForSingleObject(m_hThread, 3 * 1000) != WAIT_OBJECT_0) {
		TerminateThread(m_hThread, 0);
	}
	CloseHandle(m_hThread);

	/////
	if (m_pAlloc) {
		m_pAlloc->Decommit();
		SAFE_RELEASE(m_pAlloc);
	}
	SAFE_RELEASE(m_pInputPin);
	SAFE_RELEASE(m_ConnectedPin);
	////
	FreeMediaType(m_MediaType);
	///
//	MessageBox(0, "VCamStream::~VCamStream()", 0, 0);
	printf("VCamStream::~VCamStream()\n");
}

HRESULT STDMETHODCALLTYPE VCamStream::NonDelegatingQueryInterface(
	REFIID riid,
	__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject)
{
	HRESULT hr = S_OK;
	if (!ppvObject)return E_POINTER;
	if (riid == IID_IUnknown) {
		*ppvObject = (INonDelegatingUnknown*)this;
	}
	else if (riid == IID_IPin) {
		*ppvObject = (IPin*)(this); 
	}
	else if (riid == IID_IQualityControl) {
		*ppvObject = (IQualityControl*)this;
	}
	else if (riid == IID_IAMStreamConfig) {
		*ppvObject = (IAMStreamConfig*)this; 
	}
	else if (riid == IID_IKsPropertySet) {
		*ppvObject = (IKsPropertySet*)this; 
	}
	else {
		*ppvObject = 0;
		printf("VCamStream:QI No Interface.\n");
		///
		hr = E_NOINTERFACE;
	}

	////
	if (hr == S_OK) {
		AddRef();
	}

	return hr;
}

///交给它的所属对象计数
ULONG STDMETHODCALLTYPE VCamStream::NonDelegatingAddRef(void)
{
	return m_pFilter->AddRef();
}
ULONG STDMETHODCALLTYPE VCamStream::NonDelegatingRelease(void)
{
	return m_pFilter->Release();
}

//////IPin
static HRESULT doAllocSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* prop, AM_MEDIA_TYPE* mt)
{
	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)mt->pbFormat;
	if (!pvi)return E_INVALIDARG;
	prop->cBuffers = 1;
	prop->cbAlign = 1;
	prop->cbBuffer = pvi->bmiHeader.biSizeImage;
	ALLOCATOR_PROPERTIES Actual; memset(&Actual, 0, sizeof(Actual));
	HRESULT hr = pAlloc->SetProperties(prop, &Actual);
	if (FAILED(hr)) return hr;
	if (Actual.cbBuffer < prop->cbBuffer) return E_FAIL;

	return S_OK;
}

HRESULT VCamStream::doAlloc()
{
	m_pAlloc = 0;
	HRESULT hr = S_OK;
	ALLOCATOR_PROPERTIES prop;
	ZeroMemory(&prop, sizeof(prop));

	m_pInputPin->GetAllocatorRequirements(&prop);
	if (prop.cbAlign == 0) {
		prop.cbAlign = 1;
	}
	hr = m_pInputPin->GetAllocator(&m_pAlloc);
	if (SUCCEEDED(hr)) {
		hr = doAllocSize(m_pAlloc, &prop, &m_MediaType);
		if (SUCCEEDED(hr)) {
			hr = m_pInputPin->NotifyAllocator(m_pAlloc, FALSE);
			if (SUCCEEDED(hr)) return S_OK;
		}
		/////
	}
	////
	SAFE_RELEASE(m_pAlloc);

	/////
	hr = CoCreateInstance(CLSID_MemoryAllocator,0,CLSCTX_INPROC_SERVER,IID_IMemAllocator,(void **)&m_pAlloc);
	if (FAILED(hr)) return hr;

	hr = doAllocSize(m_pAlloc, &prop, &m_MediaType);
	if (SUCCEEDED(hr)) {
		hr = m_pInputPin->NotifyAllocator(m_pAlloc, FALSE);
		if (SUCCEEDED(hr)) return S_OK;
	}

	////
	SAFE_RELEASE(m_pAlloc);

	return hr;
}
HRESULT VCamStream::Active(BOOL bActive)
{
	////
	HRESULT hr; //return 0;
	if (!m_ConnectedPin || !m_pAlloc)return E_FAIL;
	if (bActive) hr = m_pAlloc->Commit();
	else hr = m_pAlloc->Decommit();
	return hr;
}

HRESULT VCamStream::doConnect(IPin* pRecvPin, const AM_MEDIA_TYPE* mt, BOOL bRecvPin)
{
	HRESULT hr = S_OK;
	PIN_DIRECTION Dir; 
	hr = pRecvPin->QueryDirection(&Dir); if (FAILED(hr))return hr;
	if (Dir == m_Dir) {
		return VFW_E_INVALID_DIRECTION;
	}

	do {
		hr = pRecvPin->QueryInterface(IID_IMemInputPin, (void**)&m_pInputPin); ///
		if (FAILED(hr)) {
			printf("Can not QI IID_IMemInputPin. \n");
			break;
		}
		/////匹配当前类型
		if (!EqualMediaType(&this->m_MediaType, (AM_MEDIA_TYPE*)mt)) {
			printf("MediaType Not Match.\n");
			hr = E_INVALIDARG;
			break;
		}
		VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)mt->pbFormat;
	//	char s[256]; sprintf(s, "w=%d,h=%d", pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight); MessageBox(0, s, 0, 0);
		/////
		m_ConnectedPin = pRecvPin; 
		m_ConnectedPin->AddRef();

		///
		FreeMediaType(m_MediaType);
		CopyMediaType(&m_MediaType, mt);
		///
		if (!bRecvPin) {
			///
			hr = pRecvPin->ReceiveConnection((IPin*)this, mt);
			if (FAILED(hr)) {
				printf("ReceiveConnection err=0x%X\n", hr);
				break;
			}
			////
		}

		/////分配相关内存
		hr = doAlloc();

		if (SUCCEEDED(hr)) {
			
			///
		}
		else {
			if (!bRecvPin) {
				pRecvPin->Disconnect();
			}
			/////
		}
		
		////
	} while (FALSE);

	////
	if (FAILED(hr)) {
		SAFE_RELEASE(m_pAlloc);
		SAFE_RELEASE(m_pInputPin);
		SAFE_RELEASE(m_ConnectedPin);
	}
	////
	return hr;
}
HRESULT STDMETHODCALLTYPE VCamStream::Connect(
	/* [in] */ IPin *pReceivePin,
	__in_opt  const AM_MEDIA_TYPE *pmt)
{
	HRESULT hr = S_OK; 
	if (!pReceivePin )return E_POINTER;
	///
	CLock lck(m_pFilter->m_cs); 
	
	if (m_ConnectedPin) {
		printf("Alread Connected.\n");
		return VFW_E_ALREADY_CONNECTED;
	}
	if (!IsStopped()) {
		return VFW_E_NOT_STOPPED;
	}
	/////
	if (pmt && pmt->majortype != GUID_NULL && pmt->formattype != GUID_NULL) {
		///
		hr= doConnect(pReceivePin, pmt, FALSE); ///
		return hr;
	}

	/////
	hr = VFW_E_NO_ACCEPTABLE_TYPES;
	IEnumMediaTypes* types = NULL;
	for (int i = 0; i < 2; ++i) {
		if (i == 0) hr = pReceivePin->EnumMediaTypes(&types);
		else hr = EnumMediaTypes(&types);
		if (FAILED(hr)) continue;
		////
		hr = types->Reset(); if (FAILED(hr)) { SAFE_RELEASE(types); continue; }
		while (true) {
			AM_MEDIA_TYPE* mt = NULL; ULONG cc;
			hr = types->Next(1, &mt, &cc);
			if (hr != S_OK)break;
			////
			VIDEOINFOHEADER* p1 = (VIDEOINFOHEADER*)m_MediaType.pbFormat;
			VIDEOINFOHEADER* p2 = (VIDEOINFOHEADER*)mt->pbFormat;
			hr = doConnect(pReceivePin, mt, FALSE);

			DeleteMediaType(mt);
			///
			if (SUCCEEDED(hr)) {
				types->Release();  ///
				return hr;
			}
			/////
		}
		////
		SAFE_RELEASE(types); continue;
	}

	////
	return VFW_E_NO_ACCEPTABLE_TYPES;
}
HRESULT STDMETHODCALLTYPE VCamStream::ReceiveConnection(
	IPin *pConnector,const AM_MEDIA_TYPE *pmt)
{
	HRESULT hr = S_OK;
	if (!pConnector || !pmt)return E_POINTER;
	CLock lck(m_pFilter->m_cs);

	if (m_ConnectedPin) {
		printf("Alread Connected.\n");
		return VFW_E_ALREADY_CONNECTED;
	}
	if (!IsStopped()) {
		return VFW_E_NOT_STOPPED;
	}
	
	/////
	hr = doConnect(pConnector, pmt, TRUE);

	///
	return hr;
}
HRESULT STDMETHODCALLTYPE VCamStream::Disconnect(void)
{
	HRESULT hr = S_OK;
//	MessageBox(0, "DisConnect ", "", 0);
	////
	CLock lck(m_pFilter->m_cs);

	if (!m_ConnectedPin) return S_FALSE;
	///
	if (!IsStopped()) {
		return VFW_E_NOT_STOPPED;
	}
	///
	if (m_pAlloc) {
		m_pAlloc->Decommit();
		SAFE_RELEASE(m_pAlloc);
	}
	SAFE_RELEASE(m_pInputPin);
	SAFE_RELEASE(m_ConnectedPin);

	/////
	return S_OK;
}

HRESULT STDMETHODCALLTYPE VCamStream::ConnectedTo(__out  IPin **pPin)
{
	if (!pPin)return E_POINTER;
	CLock lck(m_pFilter->m_cs);
	*pPin = m_ConnectedPin;
	if (m_ConnectedPin) {
		m_ConnectedPin->AddRef();
		return S_OK;
	}

	return VFW_E_NOT_CONNECTED;
}

HRESULT STDMETHODCALLTYPE VCamStream::ConnectionMediaType(__out  AM_MEDIA_TYPE *pmt)
{
	if (!pmt)return E_POINTER;
	CLock lck(m_pFilter->m_cs);
	if (!m_ConnectedPin) {
		return VFW_E_NOT_CONNECTED;
	}
	////
	HRESULT hr = CopyMediaType(pmt, &m_MediaType);
	return hr;
}
HRESULT STDMETHODCALLTYPE VCamStream::QueryPinInfo(__out  PIN_INFO *pInfo)
{
	if (!pInfo)return E_POINTER;
	pInfo->pFilter = (IBaseFilter*)m_pFilter;
	pInfo->pFilter->AddRef();
	/////
	if (m_PinName) {
		wcscpy(pInfo->achName, m_PinName);
	}
	else {
		pInfo->achName[0] = 0;
	}
	pInfo->dir = m_Dir;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamStream::QueryDirection(__out  PIN_DIRECTION *pPinDir)
{
	if (!pPinDir)return E_POINTER;
	*pPinDir = m_Dir;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamStream::QueryId(__out  LPWSTR *Id)
{
	if (!Id)return E_POINTER;
	if (!m_PinName)return E_INVALIDARG;
	////
	LONG Len = (wcslen(m_PinName) + 1) * sizeof(WCHAR);
	*Id = (PWCHAR)CoTaskMemAlloc(Len);
	if (!*Id)return E_OUTOFMEMORY;
	wcscpy(*Id, m_PinName);
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamStream::QueryAccept(/* [in] */ const AM_MEDIA_TYPE *pmt)
{
	if (!pmt)return E_POINTER;
	CLock lck(m_pFilter->m_cs);
	if (EqualMediaType((AM_MEDIA_TYPE*)pmt, &m_MediaType)) return S_OK;
	return S_FALSE;
}

/////
class CEnumMediaTypes :public IEnumMediaTypes
{
protected:
	LONG m_RefCount;
	///
	LONG m_Position;
	VCamStream* m_Strm;

public:
	CEnumMediaTypes(VCamStream* strm, CEnumMediaTypes* o);
	~CEnumMediaTypes();

public:
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE Release(void);
	////
	virtual HRESULT STDMETHODCALLTYPE Next(
		 ULONG cMediaTypes,
		__out_ecount_part(cMediaTypes, *pcFetched)  AM_MEDIA_TYPE **ppMediaTypes,
		__out_opt  ULONG *pcFetched);
	virtual HRESULT STDMETHODCALLTYPE Skip(/* [in] */ ULONG cMediaTypes) {
		if (cMediaTypes == 0)return S_OK;
		m_Position += cMediaTypes;
		if (m_Position >= m_Strm->m_AllSupportedMediaTypesCount) return E_UNEXPECTED;
		return S_OK;
	}
	virtual HRESULT STDMETHODCALLTYPE Reset(void) { m_Position = 0; return S_OK; }
	virtual HRESULT STDMETHODCALLTYPE Clone(__out  IEnumMediaTypes **ppEnum);
		
};
CEnumMediaTypes::CEnumMediaTypes(VCamStream* strm, CEnumMediaTypes* o)
{
	m_RefCount = 1;
	m_Position = 0;
	m_Strm = strm; strm->AddRef();
	if (o) {
		m_Position = o->m_Position;
	}
	///
}
CEnumMediaTypes::~CEnumMediaTypes()
{
	m_Strm->Release();
}
HRESULT STDMETHODCALLTYPE CEnumMediaTypes::QueryInterface(
	REFIID riid,
	__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject)
{
	HRESULT hr = S_OK;
	if (!ppvObject)return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
		*ppvObject = (IEnumMediaTypes*)(this);
	}
	else {
		*ppvObject = 0;
		printf("VCamDShow :QI No Interface.\n");
		///
		hr = E_NOINTERFACE;
	}
	////
	if (hr == S_OK) {
		AddRef();
	}

	return hr;
}
ULONG STDMETHODCALLTYPE CEnumMediaTypes::AddRef(void)
{
	return InterlockedIncrement(&m_RefCount);
}
ULONG STDMETHODCALLTYPE CEnumMediaTypes::Release(void)
{
	LONG cnt = InterlockedDecrement(&m_RefCount);
	if (cnt == 0) {
		delete this;
	}
	return cnt;
}
HRESULT STDMETHODCALLTYPE CEnumMediaTypes::Next(
	ULONG cMediaTypes,
	__out_ecount_part(cMediaTypes, *pcFetched)  AM_MEDIA_TYPE **ppMediaTypes,
	__out_opt  ULONG *pcFetched)
{
	HRESULT hr = S_OK;
	if (pcFetched != NULL) {
		*pcFetched = 0;           // default unless we succeed
	}
	else if (cMediaTypes>1) {     // pcFetched == NULL
		return E_INVALIDARG;
	}
	ULONG cFetched = 0;

	while (cMediaTypes) {
		//
		if (m_Position >= m_Strm->m_AllSupportedMediaTypesCount)break;
		*ppMediaTypes = CreateMediaType(m_Strm->m_AllSupportedMediaTypes[m_Position++]);
		if (!*ppMediaTypes)break;

		////
		ppMediaTypes++;
		cFetched++;
		cMediaTypes--;
	}
	if (pcFetched != NULL) {
		*pcFetched = cFetched;
	}

	return (cMediaTypes == 0 ? NOERROR : S_FALSE);
}
HRESULT STDMETHODCALLTYPE CEnumMediaTypes::Clone(__out  IEnumMediaTypes **ppEnum)
{
	HRESULT hr = S_OK;
	if (!ppEnum)return E_POINTER;

	*ppEnum = new CEnumMediaTypes(m_Strm, this);
	if (!*ppEnum)return E_OUTOFMEMORY;
	return hr;
}

HRESULT STDMETHODCALLTYPE VCamStream::EnumMediaTypes(__out  IEnumMediaTypes **ppEnum)
{
	if (!ppEnum)return E_POINTER;

	*ppEnum = new CEnumMediaTypes(this, NULL);
	if (!*ppEnum)return E_OUTOFMEMORY;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamStream::QueryInternalConnections(
	__out_ecount_part_opt(*nPin, *nPin)  IPin **apPin,
	/* [out][in] */ ULONG *nPin)
{
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE VCamStream::EndOfStream(void)
{
	printf("VCamStream::EndOfStream(void) \n");
	return E_UNEXPECTED;
}
HRESULT STDMETHODCALLTYPE VCamStream::BeginFlush(void)
{
	printf("VCamStream::BeginFlush(void) \n");
	return E_UNEXPECTED;
}
HRESULT STDMETHODCALLTYPE VCamStream::EndFlush(void)
{
	printf("VCamStream::EndFlush(void) \n");
	return E_UNEXPECTED;
}
HRESULT STDMETHODCALLTYPE VCamStream::NewSegment(
	/* [in] */ REFERENCE_TIME tStart,
	/* [in] */ REFERENCE_TIME tStop,
	/* [in] */ double dRate)
{
	m_tStart = tStart;
	m_tStop = tStop;
	m_dRate = dRate;
	return S_OK;
}

////
HRESULT STDMETHODCALLTYPE VCamStream::Notify(
	/* [in] */ IBaseFilter *pSelf,
	/* [in] */ Quality q)
{
	printf(" VCamStream::Notify \n");
	return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE VCamStream::SetSink(
	/* [in] */ IQualityControl *piqc)
{
	CLock lck(m_pFilter->m_cs);
	m_pQSink = piqc;
	return S_OK;
}

////IAMStreamConfig
HRESULT STDMETHODCALLTYPE VCamStream::SetFormat(
	/* [in] */ AM_MEDIA_TYPE *pmt)
{
	HRESULT hr; //
	if (!pmt)return E_POINTER;
	{
		CLock lck(m_pFilter->m_cs);
		hr = VFW_E_NOT_FOUND;
		for (int i = 0; i < m_AllSupportedMediaTypesCount; ++i) {
			if (EqualMediaType(pmt, m_AllSupportedMediaTypes[i])) {
				FreeMediaType(m_MediaType);
				hr = CopyMediaType(&m_MediaType, pmt);
				break;
			}
		}

		if (FAILED(hr)) {
			///
			return hr;
		}
	}
	////
	IPin* pin = 0;
	ConnectedTo(&pin);
	hr = S_OK;
	if (pin) { // reconnect
		IFilterGraph* graph = m_pFilter->m_pGraph;
		if (graph) {
			hr = graph->Reconnect(pin);
		}
		////
		pin->Release();
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE VCamStream::GetFormat(
	__out  AM_MEDIA_TYPE **ppmt)
{
	if (!ppmt)return E_POINTER;
	CLock lck(m_pFilter->m_cs);
	*ppmt = CreateMediaType(&m_MediaType);
	if (!*ppmt)return E_OUTOFMEMORY;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE  VCamStream::GetNumberOfCapabilities(
	__out  int *piCount,
	__out  int *piSize)
{
	if (!piCount || !piSize)return E_POINTER;
	*piCount = m_AllSupportedMediaTypesCount;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}
HRESULT STDMETHODCALLTYPE  VCamStream::GetStreamCaps(
	/* [in] */ int iIndex,
	__out  AM_MEDIA_TYPE **ppmt,
	__out  BYTE *pSCC)
{
	if (!ppmt || !pSCC )return E_POINTER;
	if (iIndex < 0 || iIndex >= m_AllSupportedMediaTypesCount) {
		printf("VCamStream::GetStreamCaps out of range.\n");
		return E_INVALIDARG;
	}
	/////
	*ppmt = CreateMediaType(m_AllSupportedMediaTypes[iIndex]);
	if (!*ppmt)return E_OUTOFMEMORY;

	/////
	VIDEO_STREAM_CONFIG_CAPS*pvscc = (VIDEO_STREAM_CONFIG_CAPS*)pSCC;
	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)(*ppmt)->pbFormat;

	int X = pvi->bmiHeader.biWidth;
	int Y = abs(pvi->bmiHeader.biHeight);

	pvscc->guid = FORMAT_VideoInfo;
	pvscc->VideoStandard = AnalogVideo_None;
	pvscc->InputSize.cx = X;
	pvscc->InputSize.cy = Y;
	pvscc->MinCroppingSize.cx = X;
	pvscc->MinCroppingSize.cy = Y;
	pvscc->MaxCroppingSize.cx = X;
	pvscc->MaxCroppingSize.cy = Y;
	pvscc->CropGranularityX = X;
	pvscc->CropGranularityY = Y;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = X;
	pvscc->MinOutputSize.cy = Y;
	pvscc->MaxOutputSize.cx = X;
	pvscc->MaxOutputSize.cy = Y;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = pvi->AvgTimePerFrame;   //50 fps
	pvscc->MaxFrameInterval = pvi->AvgTimePerFrame; // 0.2 fps
	pvscc->MinBitsPerSecond = (80 * 60 * 3 * 8) / 5;
	pvscc->MaxBitsPerSecond = X * Y * 3 * 8 * 50;

	return S_OK;
}

////IKsPropertySet
HRESULT STDMETHODCALLTYPE VCamStream::Get(
	/* [in] */ REFGUID guidPropSet,
	/* [in] */ DWORD dwPropID,
	/* [annotation][size_is][in] */
	__in_bcount(cbInstanceData)  LPVOID pInstanceData,
	/* [in] */ DWORD cbInstanceData,
	/* [annotation][size_is][out] */
	__out_bcount_part(cbPropData, *pcbReturned)  LPVOID pPropData,
	/* [in] */ DWORD cbPropData,
	/* [annotation][out] */
	__out  DWORD *pcbReturned)
{
	if (guidPropSet != AMPROPSETID_Pin)             return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY)        return E_PROP_ID_UNSUPPORTED;
	if (pPropData == NULL && pcbReturned == NULL)   return E_POINTER;

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == NULL)          return S_OK; // Caller just wants to know the size. 
	if (cbPropData < sizeof(GUID))  return E_UNEXPECTED;// The buffer is too small.

	*(GUID *)pPropData = PIN_CATEGORY_CAPTURE;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamStream::QuerySupported(
	/* [in] */ REFGUID guidPropSet,
	/* [in] */ DWORD dwPropID,
	/* [annotation][out] */
	__out  DWORD *pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	// We support getting this property, but not setting it.
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

//////
void VCamStream::StreamTreadLoop()
{
	DWORD TMO = 33;
	///
	while (!m_quit) {
		///
		WaitForSingleObject(m_event, TMO);
		if (m_quit)break;
		
		/////
		if (m_pFilter->m_State != State_Running) { //不是运行状态
			continue;
		}

		/////
		IMediaSample* sample = NULL;
		HRESULT hr = E_FAIL;
		Lock();
		if (m_pAlloc) {
			hr = m_pAlloc->GetBuffer(&sample, NULL, NULL, 0);
		}
		////
		int width=100, height = 100;
		VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)m_MediaType.pbFormat;
		if (pvi) {
			width = pvi->bmiHeader.biWidth;
			height = abs(pvi->bmiHeader.biHeight);
		}
		Unlock();
		if (!sample) {
	//		m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
			continue;
		}

		////
		int ret = -1;
		LONG length = sample->GetSize();
		char* buffer = NULL;
		hr = sample->GetPointer((BYTE**)&buffer);
		///
		frame_t frame;
		frame.buffer = buffer;
		frame.delay_msec = 33;
		frame.width = width;
		frame.height = height;
		frame.length = length;
		frame.param = m_pFilter->m_param;

		if (SUCCEEDED(hr) && buffer && pvi) {

			m_pFilter->m_callback(&frame);
			ret = 0;
			TMO = frame.delay_msec; ///
			
			////set PTS 
		//	REFERENCE_TIME now = 0;
		//	m_pFilter->getStreamTime(now);
		//	REFERENCE_TIME end = now + MILLISECONDS_TO_100NS_UNITS(TMO);
		//	sample->SetTime(&now, &end);

			sample->SetSyncPoint(TRUE);
			////
		}

//		m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
		//////
		if (ret == 0) {
			Lock();
			if (m_pInputPin) {
				hr = m_pInputPin->Receive(sample);
				if (hr != S_OK) {
				    ///
					if (m_ConnectedPin) {
						m_ConnectedPin->EndOfStream();
					}
					///
				}
			}
			Unlock();
		}
		else {
			///
		}
		///
		sample->Release();
	}

	/////
}

