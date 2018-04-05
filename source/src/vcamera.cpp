/////by fanxiushu 2018-03-08
////没使用DirectShow的SDK开发，但是借鉴了DSHOW的代码

#include <Windows.h>
#include <stdio.h>

#include "vcam_dshow.h"
#include "vcamera.h"
#include <atlbase.h>

///
static LONG LockCount = 0 ; ////
/////

VCamDShow::VCamDShow(IUnknown* pUnk, REFCLSID clsid, FRAME_CALLBACK cbk, void* param):
	m_pUnk(pUnk? pUnk : ((IUnknown*)static_cast<INonDelegatingUnknown*>(this)) ),
	m_clsid(clsid), m_callback(cbk), m_param(param)
{
	m_RefCount = 0;
	InitializeCriticalSection(&m_cs);
	m_State = State_Stopped;
	m_tStart = 0;
	m_pGraph = NULL;
	m_pName = NULL;
	m_pSink = NULL;
	m_pClock = NULL;

	////
	m_Stream = new VCamStream(this);
	m_AllPins[0] = m_Stream;
	m_AllPinsCount = 1; ///只有一个output
	/////
	InterlockedIncrement(&LockCount);
}
VCamDShow::~VCamDShow()
{
	SAFE_RELEASE(m_pClock);
	if (m_pName)free(m_pName);

	////
	delete m_Stream;

	///
	::DeleteCriticalSection(&m_cs);

	////
	InterlockedDecrement(&LockCount);
	printf("VCamDShow::~VCamDShow()");
//	MessageBox(0, "VCamDShow::~VCamDShow()", 0, 0);
}
#pragma comment(lib,"rpcrt4.lib")
HRESULT STDMETHODCALLTYPE VCamDShow::NonDelegatingQueryInterface(
	REFIID riid,
	__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject)
{
	HRESULT hr = S_OK;
	if (!ppvObject)return E_POINTER;
	*ppvObject = NULL;
	///
	if (riid == IID_IAMStreamConfig || riid == IID_IKsPropertySet) {
	    ///
		return m_Stream->QueryInterface(riid, ppvObject);
	}

	/////
	if (riid == IID_IUnknown) {
		*ppvObject = static_cast<INonDelegatingUnknown*>(this);
	}
	else if ( riid == IID_IBaseFilter) {
		*ppvObject = static_cast<IBaseFilter*>(this);
	}
	else if (riid == IID_IPersist) {
		*ppvObject = (IPersist*)this;
	}
	else if (riid == IID_IMediaFilter) {
		*ppvObject = (IMediaFilter*)this;
	}
	else if (riid == IID_IAMovieSetup) {
		*ppvObject = (IAMovieSetup*)this;
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
ULONG STDMETHODCALLTYPE VCamDShow::NonDelegatingAddRef(void)
{
	return InterlockedIncrement(&m_RefCount);
}
ULONG STDMETHODCALLTYPE VCamDShow::NonDelegatingRelease(void)
{
	LONG cnt = InterlockedDecrement(&m_RefCount);
	if (cnt == 0) {
		delete this;
	}
	return cnt;
}

HRESULT STDMETHODCALLTYPE VCamDShow::GetClassID(
	__RPC__out CLSID *pClassID)
{
	if (!pClassID)return E_POINTER;
	*pClassID = m_clsid; 
	return S_OK;
}

//很诡异的，设置状态要在新线程设置，否则有些DSHOW程序运行 IMediaControl->Run 会死锁，原因不明！
DWORD CALLBACK VCamDShow::helper_thread(void* p)
{
	struct S { VCamDShow* pthis;  FILTER_STATE state; };
	S* s = (S*)p;
	////
	Sleep(10);
	////
	s->pthis->m_State = s->state;

	delete s;
	////
	return 0;
}
void VCamDShow::setState(FILTER_STATE state)
{
	struct S { VCamDShow* pthis;  FILTER_STATE state; };
	S* s = new S;
	s->pthis = this;
	s->state = state;
	DWORD tid; 
	CloseHandle(CreateThread(NULL, 0, helper_thread, s, 0, &tid));
	////
}

HRESULT STDMETHODCALLTYPE VCamDShow::Stop(void)
{
	CLock lck(m_cs);
	HRESULT hr = S_OK;  ///
	if (m_State != State_Stopped) {
		////
		if (m_Stream->m_ConnectedPin) {
			m_Stream->Active(FALSE);
		}
	}
	////
	setState(State_Stopped); //很诡异的，设置状态要在新线程设置，否则有些DSHOW程序运行Run会死锁，原因不明！
	
	///
	return hr;
}


HRESULT STDMETHODCALLTYPE VCamDShow::Pause(void)
{
	////
	CLock lck(m_cs);
	HRESULT hr = S_OK;
	if (m_State == State_Stopped) {
		if (m_Stream->m_ConnectedPin) {
			hr=m_Stream->Active(TRUE);
		}
	}
	setState(State_Paused); // 很诡异的，设置状态要在新线程设置，否则有些DSHOW程序运行Run会死锁，原因不明！
	
	return hr;
}

HRESULT STDMETHODCALLTYPE VCamDShow::Run(REFERENCE_TIME tStart)
{
	CLock lck(m_cs); 
	///
	Sleep(500); ///
	////
	HRESULT hr = S_OK;//
	if (m_State == State_Stopped) {
		hr = Pause();
		if (FAILED(hr))return hr;
	}
	if (m_State != State_Running) {
		///
	}
	////
	m_tStart = tStart; ///
	setState(State_Running);

	return hr;
}

HRESULT STDMETHODCALLTYPE VCamDShow::SetSyncSource(
	__in_opt  IReferenceClock *pClock)
{
	////
	CLock lck(m_cs);
	SAFE_RELEASE(m_pClock); //
	m_pClock = pClock;
	if(m_pClock)
		m_pClock->AddRef();
	///
	return S_OK;
}
HRESULT STDMETHODCALLTYPE VCamDShow::GetSyncSource(
	__deref_out_opt  IReferenceClock **pClock)
{
	if (!pClock)return E_POINTER;

	CLock lck(m_cs);

	if (m_pClock)m_pClock->AddRef();
	*pClock = m_pClock;
	return S_OK;
}

HRESULT VCamDShow::getStreamTime(REFERENCE_TIME& now)
{
	HRESULT hr = S_OK;
	CLock lck(m_cs);
	if (m_pClock == NULL) {
		return VFW_E_NO_CLOCK;
	}
	hr = m_pClock->GetTime(&now);
	if (SUCCEEDED(hr)) {
		now -= m_tStart;
	}
	///
	return hr;
}

/// ENUM
class CEnumPins : public IEnumPins
{
protected:
	LONG m_RefCount;
	LONG m_Position;

	VCamDShow* m_Filter;

public:
	CEnumPins(VCamDShow* vcam, CEnumPins* o):m_Filter(vcam), m_Position(0), m_RefCount(1){
		m_Filter->AddRef();
		if (o) m_Position = o->m_Position;
	}
	~CEnumPins() {
		m_Filter->Release();
	}
public:
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		__RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject) {
		HRESULT hr = S_OK;
		if (!ppvObject)return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IEnumPins) {
			*ppvObject = (IEnumPins*)(this);
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
	virtual ULONG STDMETHODCALLTYPE AddRef(void) {
		return InterlockedIncrement(&m_RefCount);
	}
	virtual ULONG STDMETHODCALLTYPE Release(void) {
		LONG cnt = InterlockedDecrement(&m_RefCount);
		if (cnt == 0) {
			delete this;
		}
		return cnt;
	}
	//////
	STDMETHODIMP Next(
		ULONG cPins,         // place this many pins...
		__out_ecount(cPins) IPin ** ppPins,    // ...in this array of IPin*
		__out_opt ULONG * pcFetched    // actual count passed returned here
	) 
	{
		HRESULT hr = S_OK;
		if (pcFetched != NULL) {
			*pcFetched = 0;           // default unless we succeed
		}
		else if (cPins>1) {     // pcFetched == NULL
			return E_INVALIDARG;
		}
		ULONG cFetched = 0;

		while (cPins) {
			//
			if (m_Position >= m_Filter->m_AllPinsCount)break;
			*ppPins = m_Filter->m_AllPins[m_Position++];
			if (!*ppPins)break;
			(*ppPins)->AddRef();

			////
			ppPins++;
			cFetched++;
			cPins--;
		}
		if (pcFetched != NULL) {
			*pcFetched = cFetched;
		}

		return (cPins == 0 ? NOERROR : S_FALSE);
	}

	STDMETHODIMP Skip(ULONG cPins) {
		if (cPins == 0)return S_OK;
		m_Position += cPins;
		if (m_Position >= m_Filter->m_AllPinsCount) return E_UNEXPECTED;
		return S_OK;
	}
	STDMETHODIMP Reset() { m_Position = 0; return S_OK; }
	STDMETHODIMP Clone(__deref_out IEnumPins **ppEnum) {
		HRESULT hr = S_OK;
		if (!ppEnum)return E_POINTER;

		*ppEnum = new CEnumPins(m_Filter, this);
		if (!*ppEnum)return E_OUTOFMEMORY;
		return hr;
	}
};

HRESULT STDMETHODCALLTYPE VCamDShow::EnumPins(
	__out  IEnumPins **ppEnum)
{
	if (!ppEnum)return E_POINTER;

	*ppEnum = new CEnumPins(this, NULL); 
	if (!*ppEnum)return E_OUTOFMEMORY;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE VCamDShow::FindPin(
	LPCWSTR Id,
	__out  IPin **ppPin)
{ 
	if (!Id || !ppPin) { return E_POINTER; }  
	////
	for (int i = 0; i < m_AllPinsCount; ++i) {
		if (wcscmp(m_AllPins[i]->m_PinName, Id) == 0) {
			*ppPin = m_AllPins[i];
			(*ppPin)->AddRef();
			return S_OK;
		}
	}
	*ppPin = NULL;
	return VFW_E_NOT_FOUND;
}
HRESULT STDMETHODCALLTYPE VCamDShow::QueryFilterInfo(
	__out  FILTER_INFO *pInfo)
{
	if (!pInfo)return E_POINTER; 
	CLock lck(m_cs);
	if (m_pName) {
		wcscpy(pInfo->achName, m_pName);
	}
	else {
		pInfo->achName[0] = 0;
	}
	pInfo->pGraph = m_pGraph;
	if (pInfo->pGraph)pInfo->pGraph->AddRef();
	////
	return S_OK;
}

HRESULT STDMETHODCALLTYPE VCamDShow::JoinFilterGraph(
	__in_opt  IFilterGraph *pGraph,
	__in_opt  LPCWSTR pName)
{
	CLock lck(m_cs); 
	///
	m_pSink = NULL;
	m_pGraph = pGraph;
	if (m_pGraph) {
		m_pGraph->QueryInterface(IID_IMediaEventSink,
			(void**)&m_pSink);
		///
		if (m_pSink)m_pSink->Release();
		///
	}
	/////
	if (m_pName) {
		free(m_pName);
		m_pName = NULL;
	}
	if (pName) {
		m_pName = (wchar_t*)malloc(sizeof(wchar_t)*(wcslen(pName) + 1) );
		wcscpy(m_pName, pName);
	}
	return S_OK;
}
HRESULT VCamDShow::NotifyEvent(long EventCode,
	LONG_PTR EventParam1,LONG_PTR EventParam2)
{
	CLock lck(m_cs);
	// Snapshot so we don't have to lock up
	IMediaEventSink *pSink = m_pSink;
	if (pSink) {
		if (EC_COMPLETE == EventCode) {
			EventParam2 = (LONG_PTR)(IBaseFilter*)this;
		}

		return pSink->Notify(EventCode, EventParam1, EventParam2);
	}
	else {
		return E_NOTIMPL;
	}
}

////
HRESULT STDMETHODCALLTYPE VCamDShow::Register(void)
{
	printf("VCamDShow::Register(void) \n");
	//MessageBox(0, "VCamDShow::Register", 0, 0);
	return S_FALSE;

}
HRESULT STDMETHODCALLTYPE VCamDShow::Unregister(void)
{
	printf("VCamDShow::Unregister(void) \n");
	//MessageBox(0, "VCamDShow::Unregister", 0, 0);
	return S_FALSE;
}


////// Factory

class VCamDShowFactory : public IClassFactory
{
protected:
	LONG m_RefCount;

public:
	FRAME_CALLBACK m_callback;
	void* param; 

public:
	VCamDShowFactory() {
		m_RefCount = 0; ///
		///
		InterlockedIncrement(&LockCount);
	}
	~VCamDShowFactory() {
		InterlockedDecrement(&LockCount);
	}

public:
	
	STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppv) {
		if (!ppv) return E_POINTER;
		printf("Fact QI.\n");
		*ppv = 0;
		if (iid == IID_IUnknown || iid == IID_IClassFactory) {
			*ppv = static_cast<IClassFactory*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}
	STDMETHOD_(ULONG, AddRef)() {

		return InterlockedIncrement(&m_RefCount);
	}
	STDMETHOD_(ULONG, Release)() {
		LONG cnt = InterlockedDecrement(&m_RefCount);
		if (cnt == 0) {
			delete this;
		}
		return cnt;
	}
	STDMETHOD(LockServer)(BOOL fLock) {
		if (fLock)InterlockedIncrement(&LockCount);
		else InterlockedDecrement(&LockCount);
		return S_OK;
	}
	STDMETHOD(CreateInstance)(IUnknown* pUnk, REFIID riid, LPVOID* ppv) {
		if (!ppv) return E_POINTER;
		
		/////
		VCamDShow* t = new VCamDShow(pUnk, CLSID_VCamDShow, m_callback, param);
		if (FAILED(t->QueryInterface(riid, ppv))) {
			delete t;
			return E_NOINTERFACE;
		}

		return S_OK;
	}
	
};

/////

REGPINTYPES PinTypes = {
	&MEDIATYPE_Video,
	&MEDIASUBTYPE_NULL
};
REGFILTERPINS VCamPins = {
	L"Pins",
	FALSE, /// 
	TRUE,  /// output
	FALSE, /// can hav none
	FALSE, /// can have many
	&CLSID_NULL, // obs
	L"PIN",
	1,
	&PinTypes
};

static HRESULT RegisterVCamDShow(const char* file, BOOL bReg, BOOL bInProcServer )
{
	LPOLESTR pp;
	::StringFromIID(CLSID_VCamDShow , &pp);
	char pt[120];
	WideCharToMultiByte(CP_ACP, 0, pp, -1, pt, sizeof(pt), NULL, NULL);
	::CoTaskMemFree(pp);
	////
	char p[256]; strcpy(p, "CLSID\\"); strcat(p, pt);
	CRegKey key;
	if (key.Create(HKEY_CLASSES_ROOT, p) != 0) {
		printf("Not Open [%s]\n", p);
		return REGDB_E_CLASSNOTREG;
	}

	LONG status = 0;
	if (bReg) {
		if (bInProcServer) {
			status = key.SetKeyValue("InprocServer32", file);
			key.SetKeyValue("InprocServer32", "Both", "ThreadingModel");
		}
		else {
			status = key.SetKeyValue("LocalServer32", file);
			key.SetKeyValue("LocalServer32", "Both", "ThreadingModel");
		}
		printf(" KKK [%s] status=%d\n", p, status);
		//
	}
	else {
		key.DeleteSubKey("InprocServer32");
		key.DeleteSubKey("LocalServer32");
		strcpy(p, "CLSID");
		if (key.Create(HKEY_CLASSES_ROOT, p) != 0)
			return REGDB_E_CLASSNOTREG;
		key.DeleteSubKey(pt);
	}
	/////
	if (status == 0) {
		IFilterMapper2* pFM = NULL;
		HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void**)&pFM);
		if (SUCCEEDED(hr)) {
			////
			IMoniker *pMoniker = 0;
			REGFILTER2 rf2;
			rf2.dwVersion = 1;
			rf2.dwMerit = MERIT_DO_NOT_USE;
			rf2.cPins = 1;
			rf2.rgPins = &VCamPins;

			if (bReg) {
				hr = pFM->RegisterFilter(CLSID_VCamDShow, L"Fanxiushu DShow VCamera", &pMoniker, &CLSID_VideoInputDeviceCategory, NULL, &rf2);
			}
			else {
				hr = pFM->UnregisterFilter( &CLSID_VideoInputDeviceCategory, NULL, CLSID_VCamDShow);
			}
			if (FAILED(hr)) {
				printf("FilterMapper2  RegisterFilter or UnregisterFilter err=0x%X\n", hr );
				status = hr;
			}
			/////

			pFM->Release();
		}
		else {
			status = hr;
			printf("CoCreateInstance IFilterMapper2 err=0x%X\n", hr );
		}
	}
	else {
		status = REGDB_E_CLASSNOTREG;
		printf("Change Registery Err status=0x%X\n", status );
	}
	
	return status;
}

/////DLL Export

STDAPI DllCanUnloadNow(void)
{
	///
	if (LockCount == 0)return S_OK;
	else return S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	if (!ppv)return E_POINTER;
	*ppv = 0;
	if (rclsid != CLSID_VCamDShow) {
		return E_NOINTERFACE;
	}
	if (!(riid == IID_IUnknown) && !(riid == IID_IClassFactory)) {
		return E_NOINTERFACE;
	}

	////
	VCamDShowFactory* fact = new VCamDShowFactory;
	fact->m_callback = VCam_Frame_Callback;        // 获取视频帧数据
	fact->param = 0;

	HRESULT hr = fact->QueryInterface(riid, ppv);
	if (FAILED(hr)) {
	   //
		delete fact;
	}
	
	return hr;
}

STDAPI DllRegisterServer()
{
	char path[MAX_PATH];
	GetModuleFileName(g_hInstance, path, MAX_PATH);
	HRESULT hr = RegisterVCamDShow(path, TRUE, TRUE);
	return hr;
}

STDAPI DllUnregisterServer()
{
	HRESULT hr = RegisterVCamDShow(NULL, FALSE, TRUE);
	return hr;
}

