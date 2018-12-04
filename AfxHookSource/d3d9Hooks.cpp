#include "stdafx.h"

#include "d3d9Hooks.h"

#include "Gui.h"
#include "SourceInterfaces.h"
#include "CampathDrawer.h"
#include "AfxShaders.h"
#include "MirvPgl.h"
#include "csgo/hooks/shaderapidx9.h"
#include "AfxInterop.h"

#include <shared/detours.h>

#include <iterator>
#include <stack>
#include <set>
#include <map>
#include <list>

#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z')))

typedef struct __declspec(novtable) Interface_s abstract {} * Interface_t;
typedef void * (__stdcall Interface_s::*InterfaceFn_t) (void *);

#define IFACE_PASSTHROUGH_DECL(iface,method) \
	virtual void __stdcall iface ##method(void);

#define IFACE_PASSTHROUGH_DEF(iface,method,className,ifacePtr) \
	 static InterfaceFn_t fn_ ##iface ##method = (InterfaceFn_t)&iface::method; \
	__declspec(naked) void __stdcall className::iface ##method(void) \
	{ \
		__asm MOV EAX, ifacePtr \
		__asm MOV [ESP+4], EAX \
		__asm MOV EAX, fn_ ##iface ##method \
		__asm JMP EAX \
	}


extern bool g_bD3D9DebugPrint;
bool g_bD3D9DumpVertexShader = false;
bool g_bD3D9DumpPixelShader = false;


class CAfxHookDirect3DStateBlock9 : public IDirect3DStateBlock9
{
public:
	CAfxHookDirect3DStateBlock9(IDirect3DStateBlock9 * parent)
	: CreationCallStack(L"n/a (CAfxHookDirect3DStateBlock9)")
	, m_RefCount(1)
	, m_Parent(parent)
	{
		
	}

    /*** IUnknown methods ***/

    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		return m_Parent->QueryInterface(riid, ppvObj);
	}

    STDMETHOD_(ULONG,AddRef)(THIS)
	{
		ULONG result = m_Parent->AddRef();

		++m_RefCount;

		return result;
	}

    STDMETHOD_(ULONG,Release)(THIS)
	{
		--m_RefCount;

		HRESULT result = m_Parent->Release();

		if(0 == m_RefCount)
			delete this;

		return result;
	}

    /*** IDirect3DStateBlock9 methods ***/
    STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_Parent->GetDevice(ppDevice);
	}

    STDMETHOD(Capture)(THIS)
	{
		return m_Parent->Capture();
	}

    STDMETHOD(Apply)(THIS);
    
    //#ifdef D3D_DEBUG_INFO
    LPCWSTR CreationCallStack;
    //#endif

protected:

private:
	ULONG m_RefCount;
	IDirect3DStateBlock9 * m_Parent;

};

UINT g_Adapter = D3DADAPTER_DEFAULT;

void Shared_Direct3DDevice9_Init(
	UINT adapter,
	HWND hDeviceWindow,
	IDirect3DDevice9 * device
)
{
	g_Adapter = adapter;

#ifdef AFX_MIRV_PGL
	MirvPgl::D3D9_BeginDevice(device);
#endif

	AfxHookSource::Gui::On_Direct3DDevice9_Init(hDeviceWindow, device);

	g_AfxShaders.BeginDevice(device);

	g_CampathDrawer.BeginDevice(device);
}

void Shared_Direct3DDevice9_Shutdown()
{
	g_CampathDrawer.EndDevice();

	g_AfxShaders.EndDevice();

	AfxHookSource::Gui::On_Direct3DDevice9_Shutdown();

#ifdef AFX_MIRV_PGL
	MirvPgl::D3D9_EndDevice();
#endif
}

void Shared_Direct3DDevice9_EndScene()
{
	AfxHookSource::Gui::On_Direct3DDevice9_EndScene();
}

void Shared_Direct3DDevice9_Present(bool deviceLost, bool presentBlocked)
{
	AfxHookSource::Gui::On_Direct3DDevice9_Present(deviceLost);

#ifdef AFX_MIRV_PGL
	if(!presentBlocked) MirvPgl::DrawingThread_UnleashData();
#endif
}

void Shared_Direct3DDevice9_Reset_Before()
{
#ifdef AFX_MIRV_PGL
	MirvPgl::D3D9_Reset();
#endif

	AfxHookSource::Gui::On_Direct3DDevice9_Reset_Before();

	g_CampathDrawer.Reset();
}

void Shared_Direct3DDevice9_Reset_After()
{
	AfxHookSource::Gui::On_Direct3DDevice9_Reset_After();
}

ULONG g_NewDirect3DDevice9_RefCount = 1;
IDirect3DDevice9 * g_OldDirect3DDevice9 = nullptr;

ULONG g_NewDirect3DDevice9Ex_RefCount = 1;
IDirect3DDevice9Ex * g_OldDirect3DDevice9Ex = nullptr;


// CAfxDirect3DManaged /////////////////////////////////////////////////////////

template <typename T,typename D>
class CAfxDirect3DManaged : public T
{
public:
	D D3DDebugData;

	static void AfxDeviceLost()
	{
		for (typename Instances_t::iterator it = m_Instances.begin(); it != m_Instances.end(); ++it)
		{
			(*it)->OnAfxDeviceLost();
		}
	}

	static void AfxDevicePresented()
	{
		/*
		// Get rid of the old ones:

		while (!m_Lru.empty())
		{
			typename Instance_t * instance = m_Lru.back();

			// Only at least 4 frames old:
			if (!(std::abs(m_PresentNr - instance->m_MyPresentNr) >= 0b10000))
				break;

			m_InstanceToLruIt.erase(instance);
			m_Lru.pop_back();
		}

		m_PresentNr++;
		*/
	}

	CAfxDirect3DManaged()
	{
		/*
		m_DirtyCount = 0;
		m_MyPresentNr = m_PresentNr;
		*/
		m_Instances.insert(this);
	}

	T * AfxGetOrCreateUnmanaged(void)
	{
		/*
		m_MyPresentNr = m_PresentNr;

		typename InstanceToLruIt_t::iterator itLruIt = m_InstanceToLruIt.find(this);
		if (itLruIt != m_InstanceToLruIt.end())
		{
			m_Lru.erase(itLruIt->second);
			itLruIt->second = m_Lru.insert(m_Lru.begin(), this);
		}
		else
		{
			m_InstanceToLruIt[this] = m_Lru.insert(m_Lru.begin(), this);
		}

		bool wasDirty;

		T * result = OnAfxGetOrCreateUnmanaged(0b100 <= m_DirtyCount, wasDirty);

		if (wasDirty && m_DirtyCount < 0b100) ++m_DirtyCount;

		return result;*/

		bool dummy;
		return OnAfxGetOrCreateUnmanaged(false, dummy);
	}

protected:
	~CAfxDirect3DManaged()
	{
		/*
		typename InstanceToLruIt_t::iterator itLruIt = m_InstanceToLruIt.find(this);
		if (itLruIt != m_InstanceToLruIt.end())
		{
			m_Lru.erase(itLruIt->second);
			m_InstanceToLruIt.erase(itLruIt);
		}
		*/

		m_Instances.erase(this);
	}

	virtual void OnAfxDeviceLost() = 0;

	virtual T * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty) = 0;

private:
	typedef CAfxDirect3DManaged<T,D> Instance_t;
	typedef std::set<typename Instance_t *> Instances_t;
	//typedef std::list<typename Instance_t *> Lru_t;
	//typedef std::map<typename Instance_t *, typename Lru_t::iterator> InstanceToLruIt_t;

	//static int m_PresentNr;
	static typename Instances_t m_Instances;
	//static typename Lru_t m_Lru;
	//static typename InstanceToLruIt_t m_InstanceToLruIt;

	//int m_MyPresentNr;
	//int m_DirtyCount;
};

//template <typename T> int CAfxDirect3DManaged<T>::m_PresentNr = 0;
template <typename T, typename D> typename CAfxDirect3DManaged <T, D> ::Instances_t CAfxDirect3DManaged<T, D>::m_Instances;
//template <typename T> typename CAfxDirect3DManaged<T>::Lru_t CAfxDirect3DManaged<T>::m_Lru;
//template <typename T> typename CAfxDirect3DManaged<T>::InstanceToLruIt_t CAfxDirect3DManaged<T>::m_InstanceToLruIt;


// CAfxManagedOffscreenPlainSurface ////////////////////////////////////////////

// {ACEC2927-3FED-48B9-8297-69D60550D242}
DEFINE_GUID(IID_CAfxManagedOffscreenPlainSurface,
	0xacec2927, 0x3fed, 0x48b9, 0x82, 0x97, 0x69, 0xd6, 0x5, 0x50, 0xd2, 0x42);

// {ACEC2927-3FED-48B9-8297-69D60550D242}
static const GUID IID_CAfxManagedOffscreenPlainSurface =
{ 0xacec2927, 0x3fed, 0x48b9, { 0x82, 0x97, 0x69, 0xd6, 0x5, 0x50, 0xd2, 0x42 } };

struct CAfxManagedOffscreenPlainSurface_D3DDebugData
{
	LPCWSTR Name;
	UINT Width;
	UINT Height;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	DWORD Priority;
	UINT LockCount;
	UINT DCCount;
	LPCWSTR CreationCallStack;
};

// TODO, the D3D9 Debug fields should be layouted first in this class, otherwise the debug info if accessed is trash.
class CAfxManagedOffscreenPlainSurface : public CAfxDirect3DManaged<IDirect3DSurface9, CAfxManagedOffscreenPlainSurface_D3DDebugData>
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedOffscreenPlainSurface == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();
			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DSurface9 methods ***/

	STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer)
	{
		return m_pSystemMemPool->GetContainer(riid, ppContainer);
	}

	STDMETHOD(GetDesc)(THIS_ D3DSURFACE_DESC *pDesc)
	{
		return m_pSystemMemPool->GetDesc(pDesc);
	}

	STDMETHOD(LockRect)(THIS_ D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
	{
		if(!(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE)))
			this->AddDirtyRect(pRect);

		return m_pSystemMemPool->LockRect(pLockedRect, pRect, Flags);
	}

	STDMETHOD(UnlockRect)(THIS)
	{
		return m_pSystemMemPool->UnlockRect();
	}

	STDMETHOD(GetDC)(THIS_ HDC *phdc)
	{
		return m_pSystemMemPool->GetDC(phdc);
	}

	STDMETHOD(ReleaseDC)(THIS_ HDC hdc)
	{
		return m_pSystemMemPool->ReleaseDC(hdc);
	}



	CAfxManagedOffscreenPlainSurface(
		UINT Width,
		UINT Height,
		DWORD Usage,
		D3DFORMAT Format,
		D3DPOOL Pool,
		D3DMULTISAMPLE_TYPE MultiSampleType,
		DWORD MultiSampleQuality,
		IDirect3DSurface9 * pSystemMemPool
	)
		: m_pSystemMemPool(pSystemMemPool)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name =L"CAfxManagedOffscreenPlainSurface";
		this->D3DDebugData.Width = Width;
		this->D3DDebugData.Height = Height;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.MultiSampleType = MultiSampleType;
		this->D3DDebugData.MultiSampleQuality = MultiSampleQuality;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.DCCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

	HRESULT AddDirtyRect(const RECT *pDirtyRect)
	{
		if (NULL == pDirtyRect)
		{
			m_DirtyRect.left = 0;
			m_DirtyRect.top = 0;
			m_DirtyRect.right = this->D3DDebugData.Width;
			m_DirtyRect.bottom = this->D3DDebugData.Height;
		}
		else if (!m_Dirty)
		{
			m_DirtyRect = *pDirtyRect;
		}
		else
		{
			m_DirtyRect.left = min(m_DirtyRect.left, pDirtyRect->left);
			m_DirtyRect.top = min(m_DirtyRect.top, pDirtyRect->top);
			m_DirtyRect.right = max(m_DirtyRect.right, pDirtyRect->right);
			m_DirtyRect.bottom = max(m_DirtyRect.bottom, pDirtyRect->bottom);
		}

		m_Dirty = true;

		return D3D_OK;
	}

protected:
	virtual IDirect3DSurface9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
		{
			this->AddDirtyRect(NULL);
		}

		outWasDirty = m_Dirty;

		if (m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					if (SUCCEEDED(device->CreateOffscreenPlainSurface(this->D3DDebugData.Width, this->D3DDebugData.Height, this->D3DDebugData.Format, this->D3DDebugData.Pool, &m_pDefaultPool, nullptr)))
					{
						m_pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					POINT point = { m_DirtyRect.left, m_DirtyRect.top };
					device->UpdateSurface(m_pSystemMemPool, &m_DirtyRect, m_pDefaultPool, &point);
					m_Dirty = false;
				}

				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

private:
	bool m_Dirty;
	RECT m_DirtyRect;
	IDirect3DSurface9 * m_pSystemMemPool;
	IDirect3DSurface9 * m_pDefaultPool;
};


// CAfxManagedChildDirect3DSurface9 ////////////////////////////////////////////

class CAfxManagedChildDirect3DSurface9;

class __declspec(novtable) IAfxManagedTexture abstract
{
public:

	/// <summary>Called by child wrapped surface in order to update after device has been lost.</summary>
	/// <param name="surface">The child surface of this texture.</param>
	/// <returns>The new default pool surface if available.</returns>
	virtual IDirect3DSurface9 * AfxManagedChildDirect3DSurface9_GetDefaultPoolSurface(CAfxManagedChildDirect3DSurface9 * surface, bool switchToDynamic, bool & inOutasDirty) = 0;

	virtual void AfxManagedChildDirect3DSurface9_GotDirty(CAfxManagedChildDirect3DSurface9 * surface) = 0;
};

// {AFF452F9-B129-4B01-85AC-6D7EFD4F1D9D}
DEFINE_GUID(IID_CAfxManagedChildDirect3DSurface9,
	0xaff452f9, 0xb129, 0x4b01, 0x85, 0xac, 0x6d, 0x7e, 0xfd, 0x4f, 0x1d, 0x9d);

// {AFF452F9-B129-4B01-85AC-6D7EFD4F1D9D}
static const GUID IID_CAfxManagedChildDirect3DSurface9 =
{ 0xaff452f9, 0xb129, 0x4b01, { 0x85, 0xac, 0x6d, 0x7e, 0xfd, 0x4f, 0x1d, 0x9d } };

// {F4196378-526C-4EC2-8847-349CC570C75B}
DEFINE_GUID(IID_CAfxManagedChildDirect3DSurface9_ParentData,
	0xf4196378, 0x526c, 0x4ec2, 0x88, 0x47, 0x34, 0x9c, 0xc5, 0x70, 0xc7, 0x5b);

// {F4196378-526C-4EC2-8847-349CC570C75B}
static const GUID IID_CAfxManagedChildDirect3DSurface9_ParentData =
{ 0xf4196378, 0x526c, 0x4ec2, { 0x88, 0x47, 0x34, 0x9c, 0xc5, 0x70, 0xc7, 0x5b } };

struct CAfxManagedChildDirect3DSurface9_D3DDebugData
{
	LPCWSTR Name;
	UINT Width;
	UINT Height;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	DWORD Priority;
	UINT LockCount;
	UINT DCCount;
	LPCWSTR CreationCallStack;
};

// TODO, the D3D9 Debug fields should be layouted first in this class, otherwise the debug info if accessed is trash.
class CAfxManagedChildDirect3DSurface9 : public CAfxDirect3DManaged<IDirect3DSurface9, CAfxManagedChildDirect3DSurface9_D3DDebugData>
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedChildDirect3DSurface9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();
			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DSurface9 methods ***/

	STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer)
	{
		return m_pSystemMemPool->GetContainer(riid, ppContainer);
	}

	STDMETHOD(GetDesc)(THIS_ D3DSURFACE_DESC *pDesc)
	{
		return m_pSystemMemPool->GetDesc(pDesc);
	}

	STDMETHOD(LockRect)(THIS_ D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
	{
		if (!(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE)))
			this->AddDirtyRect(pRect);

		return m_pSystemMemPool->LockRect(pLockedRect, pRect, Flags);
	}

	STDMETHOD(UnlockRect)(THIS)	
	{
		return m_pSystemMemPool->UnlockRect();
	}

	STDMETHOD(GetDC)(THIS_ HDC *phdc)
	{
		return m_pSystemMemPool->GetDC(phdc);
	}

	STDMETHOD(ReleaseDC)(THIS_ HDC hdc)
	{
		return m_pSystemMemPool->ReleaseDC(hdc);
	}

	CAfxManagedChildDirect3DSurface9(
		IAfxManagedTexture * parentTexture,
		UINT Width,
		UINT Height,
		DWORD Usage,
		D3DFORMAT Format,
		D3DPOOL Pool,
		D3DMULTISAMPLE_TYPE MultiSampleType,
		DWORD MultiSampleQuality,
		IDirect3DSurface9 * pSystemMemPool
	)
		: m_ParentTexture(parentTexture)
		, m_pSystemMemPool(pSystemMemPool)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name = L"CAfxManagedChildDirect3DSurface9";
		this->D3DDebugData.Width = Width;
		this->D3DDebugData.Height = Height;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.MultiSampleType = MultiSampleType;
		this->D3DDebugData.MultiSampleQuality = MultiSampleQuality;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.DCCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

	HRESULT AddDirtyRect(const RECT *pDirtyRect)
	{
		if (NULL == pDirtyRect)
		{
			m_DirtyRect.left = 0;
			m_DirtyRect.top = 0;
			m_DirtyRect.right = this->D3DDebugData.Width;
			m_DirtyRect.bottom = this->D3DDebugData.Height;
		}
		else if (!m_Dirty)
		{
			m_DirtyRect = *pDirtyRect;
		}
		else
		{
			m_DirtyRect.left = min(m_DirtyRect.left, pDirtyRect->left);
			m_DirtyRect.top = min(m_DirtyRect.top, pDirtyRect->top);
			m_DirtyRect.right = max(m_DirtyRect.right, pDirtyRect->right);
			m_DirtyRect.bottom = max(m_DirtyRect.bottom, pDirtyRect->bottom);
		}

		m_Dirty = true;

		m_ParentTexture->AfxManagedChildDirect3DSurface9_GotDirty(this);

		return D3D_OK;
	}

protected:
	virtual IDirect3DSurface9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}

		m_pDefaultPool = m_ParentTexture->AfxManagedChildDirect3DSurface9_GetDefaultPoolSurface(this, this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC, m_Dirty);
		
		outWasDirty = m_Dirty;

		IDirect3DDevice9 * device;

		if (m_pDefaultPool && m_Dirty)
		{
			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				POINT point = { m_DirtyRect.left, m_DirtyRect.top };
				device->UpdateSurface(m_pSystemMemPool, &m_DirtyRect, m_pDefaultPool, &point);
				m_Dirty = false;
				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

private:
	bool m_Dirty;
	RECT m_DirtyRect;
	IAfxManagedTexture * m_ParentTexture;
	IDirect3DSurface9 * m_pSystemMemPool;
	IDirect3DSurface9 * m_pDefaultPool;
};

// CAfxManagedDirect3DTexture9 /////////////////////////////////////////////////

// {4E2FC120-3361-4870-9BAD-5288E4EF74F2}
DEFINE_GUID(IID_CAfxManagedDirect3DTexture9,
	0x4e2fc120, 0x3361, 0x4870, 0x9b, 0xad, 0x52, 0x88, 0xe4, 0xef, 0x74, 0xf2);

// {4E2FC120-3361-4870-9BAD-5288E4EF74F2}
static const GUID IID_CAfxManagedDirect3DTexture9 =
{ 0x4e2fc120, 0x3361, 0x4870, { 0x9b, 0xad, 0x52, 0x88, 0xe4, 0xef, 0x74, 0xf2 } };

struct CAfxManagedDirect3DTexture9_D3DDebugData
{
	LPCWSTR Name;
	UINT Width;
	UINT Height;
	UINT Levels;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	DWORD Priority;
	DWORD LOD;
	D3DTEXTUREFILTERTYPE FilterType;
	UINT LockCount;
	LPCWSTR CreationCallStack;
};

// TODO, the D3D9 Debug fields should be layouted first in this class, otherwise the debug info if accessed is trash.
class CAfxManagedDirect3DTexture9 : public CAfxDirect3DManaged<IDirect3DTexture9, CAfxManagedDirect3DTexture9_D3DDebugData>, protected IAfxManagedTexture
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedDirect3DTexture9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();
			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DBaseTexture9 methods ***/
	STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) {
		if (m_pDefaultPool) m_pDefaultPool->SetLOD(LODNew);

		return m_pSystemMemPool->SetLOD(LODNew);
	}

	STDMETHOD_(DWORD, GetLOD)(THIS)
	{
		return m_pSystemMemPool->GetLOD();
	}

	STDMETHOD_(DWORD, GetLevelCount)(THIS)
	{
		return m_pSystemMemPool->GetLevelCount();
	}

	STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetAutoGenFilterType(FilterType);

		return m_pSystemMemPool->SetAutoGenFilterType(FilterType);
	}

	STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS)
	{
		return m_pSystemMemPool->GetAutoGenFilterType();
	}

	STDMETHOD_(void, GenerateMipSubLevels)(THIS)
	{
		if (m_pDefaultPool) m_pDefaultPool->GenerateMipSubLevels();

		return m_pSystemMemPool->GenerateMipSubLevels();
	}

	/*** IDirect3DTexture9 methods ***/
	STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DSURFACE_DESC *pDesc)
	{
		return m_pSystemMemPool->GetLevelDesc(Level, pDesc);
	}

	STDMETHOD(GetSurfaceLevel)(THIS_ UINT Level, IDirect3DSurface9** ppSurfaceLevel)
	{
		if (!ppSurfaceLevel)
			return D3DERR_INVALIDCALL;

		CAfxManagedChildDirect3DSurface9 * manSurf;

		HRESULT result = this->GetManagedSurface(Level, &manSurf);

		if (SUCCEEDED(result))
		{
			*ppSurfaceLevel = manSurf;
		}

		return result;
	}

	STDMETHOD(LockRect)(THIS_ UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
	{
		HRESULT result = m_pSystemMemPool->LockRect(Level, pLockedRect, pRect, Flags);

		if (SUCCEEDED(result) && 0 == Level)
		{
			++this->D3DDebugData.LockCount;
			m_Dirty = m_Dirty || !(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE));
		}

		return result;
	}

	STDMETHOD(UnlockRect)(THIS_ UINT Level)
	{
		HRESULT result = m_pSystemMemPool->UnlockRect(Level);

		if (SUCCEEDED(result) && 0 == Level)
		{
			--this->D3DDebugData.LockCount;
		}

		return result;
	}

	STDMETHOD(AddDirtyRect)(THIS_ CONST RECT* pDirtyRect)
	{
		HRESULT result = m_pSystemMemPool->AddDirtyRect(pDirtyRect);

		if (SUCCEEDED(result))
		{
			m_Dirty = true;

			HRESULT result = D3D_OK;

			int levelCount = min(1, this->GetLevelCount()); // D3D9 only tracks dirty on level 0.

			for (int level = 0; level < levelCount; ++level)
			{
				CAfxManagedChildDirect3DSurface9 * surf;

				HRESULT result = this->GetManagedSurface(level, &surf);

				if (FAILED(result))
				{
					surf->Release();
					return result;
				}

				surf->AddDirtyRect(pDirtyRect);
				surf->Release();
			}
		}

		return result;
	}

	CAfxManagedDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, IDirect3DTexture9 * pSystemMemPoolTexture)
		: m_pSystemMemPool(pSystemMemPoolTexture)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name = L"IAfxManagedDirect3DTexture9";
		this->D3DDebugData.Width = Width;
		this->D3DDebugData.Height = Height;
		this->D3DDebugData.Levels = Levels;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.LOD = 0;
		this->D3DDebugData.FilterType = D3DTEXF_NONE;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

protected:
	virtual IDirect3DTexture9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
			m_Dirty = true;

		outWasDirty = m_Dirty;

		if (m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					IDirect3DTexture9 * pDefaultPool;
					if (SUCCEEDED(device->CreateTexture(this->D3DDebugData.Width, this->D3DDebugData.Height, this->D3DDebugData.Levels, this->D3DDebugData.Usage, this->D3DDebugData.Format, D3DPOOL_DEFAULT, &pDefaultPool, nullptr)))
					{
						m_pDefaultPool = pDefaultPool;

						pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
						pDefaultPool->SetLOD(m_pSystemMemPool->GetLOD());
						pDefaultPool->SetAutoGenFilterType(m_pSystemMemPool->GetAutoGenFilterType());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					device->UpdateTexture(m_pSystemMemPool, m_pDefaultPool);
					m_Dirty = false;
				}

				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

	virtual IDirect3DSurface9 * AfxManagedChildDirect3DSurface9_GetDefaultPoolSurface(CAfxManagedChildDirect3DSurface9 * surface, bool switchToDynamic, bool & inOutWasDirty)
	{
		UINT level = 0;
		DWORD levelSize = sizeof(level);
		IDirect3DSurface9 * surf = NULL;

		m_Dirty = m_Dirty || inOutWasDirty;
		inOutWasDirty = m_Dirty;

		m_pDefaultPool = AfxGetOrCreateUnmanaged();

		if (m_pDefaultPool)
		{
			surface->GetPrivateData(IID_CAfxManagedChildDirect3DSurface9_ParentData, &level, &levelSize);

			if (FAILED(m_pDefaultPool->GetSurfaceLevel(level, &surf)))
			{
				surf = NULL;
			}
		}

		return surf;
	}

	virtual void AfxManagedChildDirect3DSurface9_GotDirty(CAfxManagedChildDirect3DSurface9 * surface)
	{
		m_Dirty = true;
	}

private:
	bool m_Dirty;
	IDirect3DTexture9 * m_pSystemMemPool;
	IDirect3DTexture9 * m_pDefaultPool;

	STDMETHOD(GetManagedSurface)(THIS_ UINT Level, CAfxManagedChildDirect3DSurface9 ** pManSurf)
	{
		IDirect3DSurface9 * surf;

		HRESULT result = m_pSystemMemPool->GetSurfaceLevel(Level, &surf);

		if (SUCCEEDED(result) && surf)
		{
			CAfxManagedChildDirect3DSurface9 * manSurf;
			DWORD manSurfSize = sizeof(manSurf);

			if (FAILED(surf->GetPrivateData(IID_CAfxManagedChildDirect3DSurface9, &manSurf, &manSurfSize)))
			{
				manSurf = new CAfxManagedChildDirect3DSurface9(
					this,
					this->D3DDebugData.Width,
					this->D3DDebugData.Height,
					this->D3DDebugData.Usage,
					this->D3DDebugData.Format,
					this->D3DDebugData.Pool,
					D3DMULTISAMPLE_NONE,
					0,
					surf
				);

				if (FAILED(manSurf->SetPrivateData(IID_CAfxManagedChildDirect3DSurface9_ParentData, &Level, sizeof(Level), 0))
					|| FAILED(surf->SetPrivateData(IID_CAfxManagedChildDirect3DSurface9, &manSurf, manSurfSize, 0)))
				{
					manSurf->Release();
					return D3DERR_NOTAVAILABLE;
				}
			}
			else
			{
				manSurf->AddRef();
				surf->Release();
			}

			*pManSurf = manSurf;
		}

		return result;
	}
};

// CAfxManagedDirect3DVolumeTexture9 ///////////////////////////////////////////

// {7764F7E0-B5C4-4D33-A98B-3BFC145DEF8C}
DEFINE_GUID(IID_CAfxManagedDirect3DVolumeTexture9,
	0x7764f7e0, 0xb5c4, 0x4d33, 0xa9, 0x8b, 0x3b, 0xfc, 0x14, 0x5d, 0xef, 0x8c);


// {7764F7E0-B5C4-4D33-A98B-3BFC145DEF8C}
static const GUID IID_CAfxManagedDirect3DVolumeTexture9 =
{ 0x7764f7e0, 0xb5c4, 0x4d33, { 0xa9, 0x8b, 0x3b, 0xfc, 0x14, 0x5d, 0xef, 0x8c } };

struct CAfxManagedDirect3DVolumeTexture9_D3DDebugData
{
	LPCWSTR Name;
	UINT Width;
	UINT Height;
	UINT Depth;
	UINT Levels;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	DWORD Priority;
	DWORD LOD;
	D3DTEXTUREFILTERTYPE FilterType;
	UINT LockCount;
	LPCWSTR CreationCallStack;
};

class CAfxManagedDirect3DVolumeTexture9 : public CAfxDirect3DManaged<IDirect3DVolumeTexture9, CAfxManagedDirect3DVolumeTexture9_D3DDebugData>
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedDirect3DVolumeTexture9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();

			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DBaseTexture9 methods ***/
	STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) {
		if (m_pDefaultPool) m_pDefaultPool->SetLOD(LODNew);

		return m_pSystemMemPool->SetLOD(LODNew);
	}

	STDMETHOD_(DWORD, GetLOD)(THIS)
	{
		return m_pSystemMemPool->GetLOD();
	}

	STDMETHOD_(DWORD, GetLevelCount)(THIS)
	{
		return m_pSystemMemPool->GetLevelCount();
	}

	STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetAutoGenFilterType(FilterType);

		return m_pSystemMemPool->SetAutoGenFilterType(FilterType);
	}

	STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS)
	{
		return m_pSystemMemPool->GetAutoGenFilterType();
	}

	STDMETHOD_(void, GenerateMipSubLevels)(THIS)
	{
		if (m_pDefaultPool) m_pDefaultPool->GenerateMipSubLevels();

		return m_pSystemMemPool->GenerateMipSubLevels();
	}

	/*** IDirect3DVolumeTexture9 methods ***/
	STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DVOLUME_DESC *pDesc)
	{
		return m_pSystemMemPool->GetLevelDesc(Level, pDesc);
	}

	STDMETHOD(GetVolumeLevel)(THIS_ UINT Level, IDirect3DVolume9** ppVolumeLevel)
	{
		// TODO: out interface needs to be tracked for changes as well!

		return m_pSystemMemPool->GetVolumeLevel(Level, ppVolumeLevel);
	}

	STDMETHOD(LockBox)(THIS_ UINT Level, D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags)
	{
		m_Dirty = m_Dirty || !(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE));

		return m_pSystemMemPool->LockBox(Level, pLockedVolume, pBox, Flags);
	}

	STDMETHOD(UnlockBox)(THIS_ UINT Level)
	{
		HRESULT result =  m_pSystemMemPool->UnlockBox(Level);

		return result;
	}

	STDMETHOD(AddDirtyBox)(THIS_ CONST D3DBOX* pDirtyBox)
	{
		m_Dirty = true;

		return m_pSystemMemPool->AddDirtyBox(pDirtyBox);
	}

	CAfxManagedDirect3DVolumeTexture9(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, IDirect3DVolumeTexture9 * pSystemMemPoolTexture)
		: m_pSystemMemPool(pSystemMemPoolTexture)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name = L"IAfxManagedDirect3DVolumeTexture9";
		this->D3DDebugData.Width = Width;
		this->D3DDebugData.Height = Height;
		this->D3DDebugData.Depth = Depth;
		this->D3DDebugData.Levels = Levels;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.LOD = 0;
		this->D3DDebugData.FilterType = D3DTEXF_NONE;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

protected:
	virtual IDirect3DVolumeTexture9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
			m_Dirty = true;

		outWasDirty = m_Dirty;

		if (m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					IDirect3DVolumeTexture9 * pDefaultPool;
					if (SUCCEEDED(device->CreateVolumeTexture(this->D3DDebugData.Width, this->D3DDebugData.Height, this->D3DDebugData.Depth, this->D3DDebugData.Levels, this->D3DDebugData.Usage, this->D3DDebugData.Format, D3DPOOL_DEFAULT, &pDefaultPool, nullptr)))
					{
						m_pDefaultPool = pDefaultPool;

						pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
						pDefaultPool->SetLOD(m_pSystemMemPool->GetLOD());
						pDefaultPool->SetAutoGenFilterType(m_pSystemMemPool->GetAutoGenFilterType());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					device->UpdateTexture(m_pSystemMemPool, m_pDefaultPool);
					m_Dirty = false;
				}

				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

private:
	bool m_Dirty;
	IDirect3DVolumeTexture9 * m_pSystemMemPool;
	IDirect3DVolumeTexture9 * m_pDefaultPool;
};


// CAfxManagedDirect3DCubeTexture9 ///////////////////////////////////////////

// {24AA51E7-9575-49D5-A2DF-AF18CB6CC587}
DEFINE_GUID(IID_CAfxManagedDirect3DCubeTexture9,
	0x24aa51e7, 0x9575, 0x49d5, 0xa2, 0xdf, 0xaf, 0x18, 0xcb, 0x6c, 0xc5, 0x87);

// {24AA51E7-9575-49D5-A2DF-AF18CB6CC587}
static const GUID IID_CAfxManagedDirect3DCubeTexture9 =
{ 0x24aa51e7, 0x9575, 0x49d5, { 0xa2, 0xdf, 0xaf, 0x18, 0xcb, 0x6c, 0xc5, 0x87 } };


struct CAfxManagedDirect3DCubeTexture9_D3DDebugData
{
	LPCWSTR Name;
	UINT Width;
	UINT Height;
	UINT Levels;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	DWORD Priority;
	DWORD LOD;
	D3DTEXTUREFILTERTYPE FilterType;
	UINT LockCount;
	LPCWSTR CreationCallStack;
};

class CAfxManagedDirect3DCubeTexture9 : public CAfxDirect3DManaged<IDirect3DCubeTexture9, CAfxManagedDirect3DCubeTexture9_D3DDebugData>, protected IAfxManagedTexture
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedDirect3DCubeTexture9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();

			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DBaseTexture9 methods ***/
	STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) {
		if (m_pDefaultPool) m_pDefaultPool->SetLOD(LODNew);

		return m_pSystemMemPool->SetLOD(LODNew);
	}

	STDMETHOD_(DWORD, GetLOD)(THIS)
	{
		return m_pSystemMemPool->GetLOD();
	}

	STDMETHOD_(DWORD, GetLevelCount)(THIS)
	{
		return m_pSystemMemPool->GetLevelCount();
	}

	STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetAutoGenFilterType(FilterType);

		return m_pSystemMemPool->SetAutoGenFilterType(FilterType);
	}

	STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS)
	{
		return m_pSystemMemPool->GetAutoGenFilterType();
	}

	STDMETHOD_(void, GenerateMipSubLevels)(THIS)
	{
		if (m_pDefaultPool) m_pDefaultPool->GenerateMipSubLevels();

		return m_pSystemMemPool->GenerateMipSubLevels();
	}

	/*** IDirect3DCubeTexture9 methods ***/
	STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DSURFACE_DESC *pDesc)
	{
		return m_pSystemMemPool->GetLevelDesc(Level, pDesc);
	}

	STDMETHOD(GetCubeMapSurface)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9** ppCubeMapSurface)
	{
		if (nullptr == ppCubeMapSurface)
			return D3DERR_INVALIDCALL;

		CAfxManagedChildDirect3DSurface9 * surf;
		
		HRESULT result = this->GetManagedSurface(FaceType, Level, &surf);

		if (SUCCEEDED(result))
		{
			*ppCubeMapSurface = surf;
		}

		return result;
	}

	STDMETHOD(LockRect)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
	{
		HRESULT result = m_pSystemMemPool->LockRect(FaceType, Level, pLockedRect, pRect, Flags);

		if (SUCCEEDED(result))
		{
			m_Dirty = m_Dirty || !(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE));
		}

		return result;
	}

	STDMETHOD(UnlockRect)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level)
	{
		HRESULT result = m_pSystemMemPool->UnlockRect(FaceType, Level);

		if (SUCCEEDED(result))
		{
		}

		return result;
	}

	STDMETHOD(AddDirtyRect)(THIS_ D3DCUBEMAP_FACES FaceType, CONST RECT* pDirtyRect)
	{
		m_Dirty = true;

		HRESULT result = D3D_OK;

		int levelCount = min(1,this->GetLevelCount()); // D3D9 only tracks dirty on level 0.

		for (int level = 0; level < levelCount; ++level)
		{
			CAfxManagedChildDirect3DSurface9 * surf;

			HRESULT result = this->GetManagedSurface(FaceType, level, &surf);

			if (FAILED(result))
			{
				surf->Release();
				return result;
			}

			surf->AddDirtyRect(pDirtyRect);
			surf->Release();
		}

		return result;
	}

	CAfxManagedDirect3DCubeTexture9(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, IDirect3DCubeTexture9 * pSystemMemPoolTexture)
		: m_pSystemMemPool(pSystemMemPoolTexture)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name = L"IAfxManagedDirect3DVolumeTexture9";
		this->D3DDebugData.Width = EdgeLength;
		this->D3DDebugData.Height = EdgeLength;
		this->D3DDebugData.Levels = Levels;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.LOD = 0;
		this->D3DDebugData.FilterType = D3DTEXF_NONE;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

protected:

	virtual IDirect3DCubeTexture9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
			m_Dirty = true;

		outWasDirty = m_Dirty;

		if (m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					IDirect3DCubeTexture9 * pDefaultPool;
					if (SUCCEEDED(device->CreateCubeTexture(this->D3DDebugData.Width, this->D3DDebugData.Levels, this->D3DDebugData.Usage, this->D3DDebugData.Format, D3DPOOL_DEFAULT, &pDefaultPool, nullptr)))
					{
						m_pDefaultPool = pDefaultPool;

						pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
						pDefaultPool->SetLOD(m_pSystemMemPool->GetLOD());
						pDefaultPool->SetAutoGenFilterType(m_pSystemMemPool->GetAutoGenFilterType());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					device->UpdateTexture(m_pSystemMemPool, m_pDefaultPool);
					m_Dirty = false;
				}

				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

	virtual IDirect3DSurface9 * AfxManagedChildDirect3DSurface9_GetDefaultPoolSurface(CAfxManagedChildDirect3DSurface9 * surface, bool switchToDynamic, bool & inOutWasDirty)
	{
		MyCubeSurfaceInfo myCubeSurfaceInfo = { D3DCUBEMAP_FACE_POSITIVE_X, 0 };
		DWORD myCubeSurfaceInfoSize = sizeof(myCubeSurfaceInfo);
		IDirect3DSurface9 * surf = NULL;

		m_Dirty = m_Dirty || inOutWasDirty;
		inOutWasDirty = m_Dirty;

		m_pDefaultPool = AfxGetOrCreateUnmanaged();

		if (m_pDefaultPool)
		{
			surface->GetPrivateData(IID_CAfxManagedChildDirect3DSurface9_ParentData, &myCubeSurfaceInfo, &myCubeSurfaceInfoSize);

			if (FAILED(m_pDefaultPool->GetCubeMapSurface(myCubeSurfaceInfo.FaceType, myCubeSurfaceInfo.Level, &surf)))
			{
				surf = NULL;
			}
		}

		return surf;
	}

	virtual void AfxManagedChildDirect3DSurface9_GotDirty(CAfxManagedChildDirect3DSurface9 * surface)
	{
		m_Dirty = true;
	}

private:
	struct MyCubeSurfaceInfo
	{
		D3DCUBEMAP_FACES FaceType;
		UINT Level;
	};

	bool m_Dirty;
	IDirect3DCubeTexture9 * m_pSystemMemPool;
	IDirect3DCubeTexture9 * m_pDefaultPool;

	HRESULT GetManagedSurface(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level, CAfxManagedChildDirect3DSurface9 ** pManagedSurface)
	{
		IDirect3DSurface9 * surface = nullptr;

		HRESULT result = m_pSystemMemPool->GetCubeMapSurface(FaceType, Level, &surface);

		if (SUCCEEDED(result))
		{
			CAfxManagedChildDirect3DSurface9 * manSurf;
			DWORD manSurfSize = sizeof(manSurf);

			if (FAILED(surface->GetPrivateData(IID_CAfxManagedChildDirect3DSurface9, &manSurf, &manSurfSize)))
			{
				manSurf = new CAfxManagedChildDirect3DSurface9(
					this,
					this->D3DDebugData.Width,
					this->D3DDebugData.Height,
					this->D3DDebugData.Usage,
					this->D3DDebugData.Format,
					this->D3DDebugData.Pool,
					D3DMULTISAMPLE_NONE,
					0,
					surface
				);

				MyCubeSurfaceInfo myCubeSurfaceInfo = { FaceType , Level };

				if (
					FAILED(manSurf->SetPrivateData(IID_CAfxManagedChildDirect3DSurface9_ParentData, &myCubeSurfaceInfo, sizeof(myCubeSurfaceInfo), 0))
					|| FAILED(surface->SetPrivateData(IID_CAfxManagedChildDirect3DSurface9, &manSurf, manSurfSize, 0)))
				{
					manSurf->Release();
					return D3DERR_NOTAVAILABLE;
				}
			}
			else
			{
				manSurf->AddRef();
				surface->Release();
			}

			*pManagedSurface = manSurf;
		}

		return result;
	}
};

// CAfxManagedDirect3DVertexBuffer9 ///////////////////////////////////////////

// {57CDD386-9136-4E22-B215-77792080A04F}
DEFINE_GUID(IID_CAfxManagedDirect3DVertexBuffer9,
	0x57cdd386, 0x9136, 0x4e22, 0xb2, 0x15, 0x77, 0x79, 0x20, 0x80, 0xa0, 0x4f);

// {57CDD386-9136-4E22-B215-77792080A04F}
static const GUID IID_CAfxManagedDirect3DVertexBuffer9 =
{ 0x57cdd386, 0x9136, 0x4e22, { 0xb2, 0x15, 0x77, 0x79, 0x20, 0x80, 0xa0, 0x4f } };

struct CAfxManagedDirect3DVertexBuffer9_D3DDebugData
{
	LPCWSTR Name;
	UINT Length;
	DWORD Usage;
	DWORD FVF;
	D3DPOOL Pool;
	DWORD Priority;
	UINT LockCount;
	LPCWSTR CreationCallStack;
};

class CAfxManagedDirect3DVertexBuffer9 : public CAfxDirect3DManaged<IDirect3DVertexBuffer9, CAfxManagedDirect3DVertexBuffer9_D3DDebugData>
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedDirect3DVertexBuffer9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();

			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DVertexBuffer9 methods ***/
	STDMETHOD(Lock)(THIS_ UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags)
	{
		m_Dirty = m_Dirty || !(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE));

		return m_pSystemMemPool->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
	}

	STDMETHOD(Unlock)(THIS)
	{
		HRESULT result = m_pSystemMemPool->Unlock();

		return result;
	}

	STDMETHOD(GetDesc)(THIS_ D3DVERTEXBUFFER_DESC *pDesc)
	{
		return m_pSystemMemPool->GetDesc(pDesc);
	}

	CAfxManagedDirect3DVertexBuffer9(UINT Length, DWORD Usage, DWORD FVF, IDirect3DVertexBuffer9 * pSystemMemPool)
		: m_pSystemMemPool(pSystemMemPool)
		, m_Dirty(false)
		, m_pDefaultPool(nullptr)
	{
		this->D3DDebugData.Name = L"IAfxManagedDirect3DVertexBuffer9";
		this->D3DDebugData.Length = Length;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.FVF = FVF;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

protected:

	virtual IDirect3DVertexBuffer9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
			m_Dirty = true;

		outWasDirty = m_Dirty;

		if (m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					IDirect3DVertexBuffer9 * pDefaultPool;
					if (SUCCEEDED(device->CreateVertexBuffer(this->D3DDebugData.Length, this->D3DDebugData.Usage, this->D3DDebugData.FVF, D3DPOOL_DEFAULT, &pDefaultPool, nullptr)))
					{
						m_pDefaultPool = pDefaultPool;

						pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					void * pSource;
					void * pTarget;

					if (SUCCEEDED(m_pSystemMemPool->Lock(0, this->D3DDebugData.Length, &pSource, D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE | D3DLOCK_NO_DIRTY_UPDATE)))
					{
						if (SUCCEEDED(m_pDefaultPool->Lock(0, this->D3DDebugData.Length, &pTarget, 0)))
						{
							memcpy(pTarget, pSource, this->D3DDebugData.Length);
							m_pDefaultPool->Unlock();
							m_Dirty = false;
						}
						m_pSystemMemPool->Unlock();
					}

				}

				device->Release();
			}
		}

		return m_pDefaultPool;

	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

private:
	bool m_Dirty;
	IDirect3DVertexBuffer9 * m_pSystemMemPool;
	IDirect3DVertexBuffer9 * m_pDefaultPool;
};


// CAfxManagedDirect3DIndexBuffer9 ///////////////////////////////////////////

// {B8355D17-E24B-4B62-ABA6-C5625791AEC4}
DEFINE_GUID(IID_CAfxManagedDirect3DIndexBuffer9,
	0xb8355d17, 0xe24b, 0x4b62, 0xab, 0xa6, 0xc5, 0x62, 0x57, 0x91, 0xae, 0xc4);

// {B8355D17-E24B-4B62-ABA6-C5625791AEC4}
static const GUID IID_CAfxManagedDirect3DIndexBuffer9 =
{ 0xb8355d17, 0xe24b, 0x4b62, { 0xab, 0xa6, 0xc5, 0x62, 0x57, 0x91, 0xae, 0xc4 } };

struct CAfxManagedDirect3DIndexBuffer9_D3DDebugData
{
	LPCWSTR Name;
	UINT Length;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	DWORD Priority;
	UINT LockCount;
	LPCWSTR CreationCallStack;
};

class CAfxManagedDirect3DIndexBuffer9 : public CAfxDirect3DManaged<IDirect3DIndexBuffer9, CAfxManagedDirect3DIndexBuffer9_D3DDebugData>
{
public:
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_CAfxManagedDirect3DIndexBuffer9 == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_pSystemMemPool->QueryInterface(riid, ppvObj);

	}
	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		return m_pSystemMemPool->AddRef();
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		ULONG result = m_pSystemMemPool->Release();

		if (0 == result)
		{
			if (m_pDefaultPool) m_pDefaultPool->Release();

			delete this;
		}

		return result;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice)
	{
		return m_pSystemMemPool->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
	{
		return m_pSystemMemPool->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData)
	{
		return m_pSystemMemPool->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid)
	{
		return m_pSystemMemPool->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew)
	{
		if (m_pDefaultPool) m_pDefaultPool->SetPriority(PriorityNew);

		return m_pSystemMemPool->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS)
	{
		return m_pSystemMemPool->GetPriority();
	}

	STDMETHOD_(void, PreLoad)(THIS)
	{
		AfxGetOrCreateUnmanaged();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS)
	{
		return m_pSystemMemPool->GetType();
	}

	/*** IDirect3DIndexBuffer9 methods ***/
	STDMETHOD(Lock)(THIS_ UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags)
	{
		m_Dirty = m_Dirty || !(Flags & (DWORD)(D3DLOCK_NO_DIRTY_UPDATE));

		return m_pSystemMemPool->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
	}

	STDMETHOD(Unlock)(THIS)
	{
		HRESULT result = m_pSystemMemPool->Unlock();

		return result;
	}

	STDMETHOD(GetDesc)(THIS_ D3DINDEXBUFFER_DESC *pDesc)
	{
		return m_pSystemMemPool->GetDesc(pDesc);
	}

	CAfxManagedDirect3DIndexBuffer9(UINT Length, DWORD Usage, D3DFORMAT Format, IDirect3DIndexBuffer9 * pSystemMemPool)
		: m_pSystemMemPool(pSystemMemPool)
		, m_pDefaultPool(nullptr)
		, m_Dirty(false)
	{
		this->D3DDebugData.Name = L"IAfxManagedDirect3DIndexBuffer9";
		this->D3DDebugData.Length = Length;
		this->D3DDebugData.Usage = Usage;
		this->D3DDebugData.Format = Format;
		this->D3DDebugData.Pool = D3DPOOL_MANAGED;
		this->D3DDebugData.Priority = 0;
		this->D3DDebugData.LockCount = 0;
		this->D3DDebugData.CreationCallStack = L"n/a";
	}

protected:
	virtual IDirect3DIndexBuffer9 * OnAfxGetOrCreateUnmanaged(bool switchToDynamic, bool & outWasDirty)
	{
		if (switchToDynamic && !(this->D3DDebugData.Usage & D3DUSAGE_DYNAMIC))
		{
			this->D3DDebugData.Usage |= D3DUSAGE_DYNAMIC;
			if (m_pDefaultPool)
			{
				m_pDefaultPool->Release();
				m_pDefaultPool = nullptr;
			}
		}

		if (nullptr == m_pDefaultPool)
			m_Dirty = true;

		outWasDirty = m_Dirty;

		if(m_Dirty)
		{
			IDirect3DDevice9 * device;

			if (SUCCEEDED(m_pSystemMemPool->GetDevice(&device)))
			{
				if (nullptr == m_pDefaultPool)
				{
					IDirect3DIndexBuffer9 * pDefaultPool;
					if (SUCCEEDED(device->CreateIndexBuffer(this->D3DDebugData.Length, this->D3DDebugData.Usage, this->D3DDebugData.Format, D3DPOOL_DEFAULT, &pDefaultPool, nullptr)))
					{
						m_pDefaultPool = pDefaultPool;

						pDefaultPool->SetPriority(m_pSystemMemPool->GetPriority());
					}
				}

				if (m_pDefaultPool && m_Dirty)
				{
					void * pSource;
					void * pTarget;

					if (SUCCEEDED(m_pSystemMemPool->Lock(0, this->D3DDebugData.Length, &pSource, D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE | D3DLOCK_NO_DIRTY_UPDATE)))
					{
						if (SUCCEEDED(m_pDefaultPool->Lock(0, this->D3DDebugData.Length, &pTarget, 0)))
						{
							memcpy(pTarget, pSource, this->D3DDebugData.Length);
							m_pDefaultPool->Unlock();
							m_Dirty = false;
						}
						m_pSystemMemPool->Unlock();
					}

				}

				device->Release();
			}
		}

		return m_pDefaultPool;
	}

	virtual void OnAfxDeviceLost()
	{
		if (m_pDefaultPool)
		{
			m_pDefaultPool->Release();
			m_pDefaultPool = nullptr;
		}
	}

private:
	bool m_Dirty;
	IDirect3DIndexBuffer9 * m_pSystemMemPool;
	IDirect3DIndexBuffer9 * m_pDefaultPool;

};


////////////////////////////////////////////////////////////////////////////////

void FixPresentationparementers(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
#ifdef AFX_INTEROP
	if (AfxInterop::Enabled() && pPresentationParameters)
	{
		pPresentationParameters->MultiSampleType = D3DMULTISAMPLE_NONE;
		pPresentationParameters->MultiSampleQuality = 0;
		pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
	}
#endif
}


// {424A968E-EBD4-4BE2-9BEB-374580F00775}
static const GUID IID_IAfxInteropSharedSurface =
{ 0x424a968e, 0xebd4, 0x4be2, { 0x9b, 0xeb, 0x37, 0x45, 0x80, 0xf0, 0x7, 0x75 } };

// {424A968E-EBD4-4BE2-9BEB-374580F00775}
DEFINE_GUID(IID_IAfxInteropSharedSurface,
	0x424a968e, 0xebd4, 0x4be2, 0x9b, 0xeb, 0x37, 0x45, 0x80, 0xf0, 0x7, 0x75);

class CSharedSurfaceHook : public IDirect3DSurface9, public ISharedSurfaceInfo
{
public:
	CSharedSurfaceHook()
		: m_RefCount(1)
	{
	}

	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
	{
		if (IID_IAfxInteropSharedSurface == riid && ppvObj)
		{
			*ppvObj = this;
			return D3D_OK;
		}

		return m_Parent->QueryInterface(riid, ppvObj);
	}

	STDMETHOD_(ULONG, AddRef)(THIS)
	{
		++m_RefCount;

		return m_RefCount;
	}

	STDMETHOD_(ULONG, Release)(THIS)
	{
		--m_RefCount;

		if (0 == m_RefCount)
		{
			if (m_Parent)
			{
				AfxInterop::OnReleaseSharedSurface(this);
				m_Parent->FreePrivateData(IID_IAfxInteropSharedSurface);
				m_Parent->Release();
			}

			delete this;

			return 0;
		}

		return m_RefCount;
	}

	/*** IDirect3DResource9 methods ***/
	STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) {
		if (!m_Parent) return D3DERR_INVALIDCALL;

		return m_Parent->GetDevice(ppDevice);
	}

	STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) {
		if (!m_Parent) return D3DERR_INVALIDCALL;

		return m_Parent->SetPrivateData(refguid, pData, SizeOfData, Flags);
	}

	STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) {
		if (!m_Parent) return D3DERR_INVALIDCALL;

		return m_Parent->GetPrivateData(refguid, pData, pSizeOfData);
	}

	STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) {
		if (!m_Parent) return D3DERR_INVALIDCALL;

		return m_Parent->FreePrivateData(refguid);
	}

	STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) {
		if (!m_Parent) return D3DERR_INVALIDCALL;

		return m_Parent->SetPriority(PriorityNew);
	}

	STDMETHOD_(DWORD, GetPriority)(THIS) {
		if (!m_Parent) return 0;

		return m_Parent->GetPriority();
	}
	STDMETHOD_(void, PreLoad)(THIS)
	{
		if (!m_Parent) return;

		return m_Parent->PreLoad();
	}

	STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) {
		if (!m_Parent) D3DRTYPE_SURFACE;

		return m_Parent->GetType();
	}

	STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer) {
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->GetContainer(riid, ppContainer);
	}

	STDMETHOD(GetDesc)(THIS_ D3DSURFACE_DESC *pDesc) {
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->GetDesc(pDesc);
	}

	STDMETHOD(LockRect)(THIS_ D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
	{
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->LockRect(pLockedRect, pRect, Flags);
	}

	STDMETHOD(UnlockRect)(THIS)
	{
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->UnlockRect();
	}

	STDMETHOD(GetDC)(THIS_ HDC *phdc)
	{
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->GetDC(phdc);
	}

	STDMETHOD(ReleaseDC)(THIS_ HDC hdc)
	{
		if (!m_Parent) D3DERR_INVALIDCALL;

		return m_Parent->ReleaseDC(hdc);
	}

	LPCWSTR Name = L"n/a (AfxInterop::CSharedSurfaceHook)";
	UINT Width;
	UINT Height;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	DWORD Priority = 0;
	UINT LockCount = 0;
	UINT DCCount = 0;
	LPCWSTR CreationCallStack = L"n/a (AfxInterop::CSharedSurfaceHook)";

	virtual IDirect3DSurface9 * GetSharedSurface()
	{
		return m_Parent;
	}

	virtual D3DMULTISAMPLE_TYPE GetMultiSampleType()
	{
		return MultiSampleType;
	}

	virtual DWORD GetMultiSampleQuality()
	{
		return MultiSampleQuality;
	}

	virtual HANDLE GetSharedHandle()
	{
		return m_SharedHandle;
	}

	void DeviceLost()
	{
		if (m_Parent)
		{
			AfxInterop::OnReleaseSharedSurface(this);
			m_Parent->FreePrivateData(IID_IAfxInteropSharedSurface);
			m_Parent->Release();
			m_Parent = nullptr;
		}
	}

	void DeviceReset(
		UINT                Width,
		UINT                Height,
		DWORD				Usage,
		D3DFORMAT           Format,
		D3DPOOL             Pool,
		D3DMULTISAMPLE_TYPE MultiSampleType,
		DWORD               MultisampleQuality,
		IDirect3DSurface9   *pSurface,
		HANDLE              sharedHandle
	)
	{
		this->Width = Width;
		this->Height = Height;
		this->Usage = Usage;
		this->Format = Format;
		this->Pool = Pool;
		this->MultiSampleType = MultiSampleType;
		this->MultiSampleQuality = MultisampleQuality;
		this->m_Parent = pSurface;
		this->m_SharedHandle = sharedHandle;

		CSharedSurfaceHook * data = this;
		m_Parent->SetPrivateData(IID_IAfxInteropSharedSurface, &data, sizeof(CSharedSurfaceHook *), 0);

		AfxInterop::OnCreatedSharedSurface(this);
	}

private:
	ULONG m_RefCount;
	IDirect3DSurface9 * m_Parent;
	HANDLE m_SharedHandle;
};


// NewDirect3DDevice9 //////////////////////////////////////////////////////////

// TODO: Wrap / Unwrap swapchaing (low priority)
struct NewDirect3DDevice9
{
private:
	DWORD m_D3DRS_SRCBLEND = D3DBLEND_ONE;
	DWORD m_D3DRS_DESTBLEND = D3DBLEND_ZERO;
	DWORD m_D3DRS_SRGBWRITEENABLE = FALSE;
	DWORD m_D3DRS_ZWRITEENABLE = TRUE;
	DWORD m_D3DRS_ALPHABLENDENABLE = FALSE;
	float m_OriginalValue_ps_c0[4] = { 0, 0, 0, 0 };
	float m_OriginalValue_ps_c5[4] = { 0, 0, 0, 0 };
	float m_OriginalValue_ps_c12[4] = { 0, 0, 0, 0 };
	float m_OriginalValue_ps_c29[4] = { 0, 0, 0, 0 };
	float m_OriginalValue_ps_c31[4] = { 0, 0, 0, 0 };
	float m_OriginalValue_ps_c1[4] = { 0, 0, 0, 0 };
	IDirect3DVertexShader9 * m_Original_VertexShader = 0;
	IDirect3DPixelShader9 * m_Original_PixelShader = 0;

	IDirect3DSurface9 * orgDeviceContextRenderTarget = nullptr;
	IDirect3DSurface9 * orgDepthStencilSurface = nullptr;

	CSharedSurfaceHook * sharedRenderTarget = nullptr;
	CSharedSurfaceHook * sharedDepthStencilSurface = nullptr;

	IDirect3DQuery9* waitQuery = nullptr;

	void ReleaseQueries()
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (waitQuery)
			{
				waitQuery->Release();
				waitQuery = nullptr;
			}
		}
#endif
	}

	void CreateQueries()
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (FAILED(g_OldDirect3DDevice9->CreateQuery(D3DQUERYTYPE_EVENT, &waitQuery)))
				waitQuery = nullptr;
		}
#endif
	}

public:
	void Init(D3DPRESENT_PARAMETERS* pPresentationParameters)
	{
		CreateQueries();
		CreateSharedRenderTargets(pPresentationParameters);
	}

	void AfxWaitForGPU()
	{
		if (waitQuery)
		{
			waitQuery->Issue(D3DISSUE_END);

			while (S_FALSE == waitQuery->GetData(NULL, 0, D3DGETDATA_FLUSH))
				;
		}
	}

private:
	bool GetAfxManagedChildDirect3DSurface9(IDirect3DSurface9 * surface, CAfxManagedChildDirect3DSurface9 ** pOut)
	{
		if (surface && SUCCEEDED(surface->QueryInterface(IID_CAfxManagedChildDirect3DSurface9, (void **)pOut)))
			return true;

		return false;
	}

	bool GetAfxManagedChildDirect3DSurface9Reverse(IDirect3DSurface9 * surface, CAfxManagedChildDirect3DSurface9 ** pOut)
	{
		DWORD size = sizeof(CAfxManagedChildDirect3DSurface9 *);
		if (surface && SUCCEEDED(surface->GetPrivateData(IID_CAfxManagedChildDirect3DSurface9, pOut, &size)))
			return true;

		return false;
	}

	bool GetSharedSurfaceHook(IDirect3DSurface9 * surface, CSharedSurfaceHook ** pSharedSurfaceHook)
	{
		if (surface && SUCCEEDED(surface->QueryInterface(IID_IAfxInteropSharedSurface, (void **)pSharedSurfaceHook)))
			return true;

		return false;
	}

	bool GetSharedSurfaceHookReverse(IDirect3DSurface9 * surface, CSharedSurfaceHook ** pSharedSurfaceHook)
	{
		DWORD size = sizeof(*pSharedSurfaceHook);
		if (surface && SUCCEEDED(surface->GetPrivateData(IID_IAfxInteropSharedSurface, pSharedSurfaceHook, &size)))
			return true;

		return false;
	}

	bool GetAfxManagedOffscreenPlainSurface(IDirect3DSurface9 * surface, CAfxManagedOffscreenPlainSurface ** pOut)
	{
		if (surface && SUCCEEDED(surface->QueryInterface(IID_CAfxManagedOffscreenPlainSurface, (void **)pOut)))
			return true;

		return false;
	}

	bool GetAfxManagedOffscreenPlainSurfaceReverse(IDirect3DSurface9 * surface, CAfxManagedOffscreenPlainSurface ** pOut)
	{
		DWORD size = sizeof(*pOut);
		if (pOut && SUCCEEDED(surface->GetPrivateData(IID_CAfxManagedOffscreenPlainSurface, pOut, &size)))
			return true;

		return false;
	}	

	IDirect3DSurface9 * UnwrapSurface(IDirect3DSurface9 * surface)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && surface)
		{
			{
				CAfxManagedChildDirect3DSurface9 * afxWrapper;
				if (GetAfxManagedChildDirect3DSurface9(surface, &afxWrapper))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
			{
				CAfxManagedOffscreenPlainSurface * afxWrapper;
				if (GetAfxManagedOffscreenPlainSurface(surface, &afxWrapper))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
			{
				CSharedSurfaceHook * afxWrapper;
				if (GetSharedSurfaceHook(surface, &afxWrapper))
				{
					//if (makeDirty) afxWrapper->AddDirtyRect(NULL);
					return afxWrapper->GetSharedSurface();
				}
			}
		}
#endif

		return surface;
	}

	IDirect3DSurface9 * UnwrapSurfaceReverse(IDirect3DSurface9 * surface, bool handleRef)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && surface)
		{
			{
				CAfxManagedChildDirect3DSurface9 * afxWrapper;

				if (GetAfxManagedChildDirect3DSurface9Reverse(surface, &afxWrapper))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						surface->Release();
					}

					return afxWrapper;
				}
			}
			{
				CAfxManagedOffscreenPlainSurface * afxWrapper;

				if (GetAfxManagedOffscreenPlainSurfaceReverse(surface, &afxWrapper))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						surface->Release();
					}

					return afxWrapper;
				}
			}
			{
				CSharedSurfaceHook * sharedSurfaceHook;

				if (GetSharedSurfaceHookReverse(surface, &sharedSurfaceHook))
				{
					if (handleRef)
					{
						sharedSurfaceHook->AddRef();
						surface->Release();
					}

					return sharedSurfaceHook;
				}
			}
		}
#endif

		return surface;
	}

	IDirect3DBaseTexture9 * UnwrapTextureReverse(IDirect3DBaseTexture9 * pTexture, bool handleRef)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && pTexture)
		{
			{
				CAfxManagedDirect3DTexture9 * afxWrapper;
				DWORD afxWrapperSize = sizeof(afxWrapper);
				if (SUCCEEDED(pTexture->GetPrivateData(IID_CAfxManagedDirect3DTexture9, &afxWrapper, &afxWrapperSize)))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						pTexture->Release();
					}

					return afxWrapper;
				}
			}
			{
				CAfxManagedDirect3DVolumeTexture9 * afxWrapper;
				DWORD afxWrapperSize = sizeof(afxWrapper);
				if (SUCCEEDED(pTexture->GetPrivateData(IID_CAfxManagedDirect3DVolumeTexture9, &afxWrapper, &afxWrapperSize)))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						pTexture->Release();
					}

					return afxWrapper;
				}
			}
			{
				CAfxManagedDirect3DCubeTexture9 * afxWrapper;
				DWORD afxWrapperSize = sizeof(afxWrapper);
				if (SUCCEEDED(pTexture->GetPrivateData(IID_CAfxManagedDirect3DCubeTexture9, &afxWrapper, &afxWrapperSize)))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						pTexture->Release();
					}

					return afxWrapper;
				}
			}
		}
#endif
		return pTexture;
	}

	IDirect3DBaseTexture9 * UnwrapTexture(IDirect3DBaseTexture9 * pTexture)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && pTexture)
		{
			{
				CAfxManagedDirect3DTexture9 * afxWrapper;

				if (SUCCEEDED(pTexture->QueryInterface(IID_CAfxManagedDirect3DTexture9, (void **)&afxWrapper)))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
			{
				CAfxManagedDirect3DVolumeTexture9 * afxWrapper;

				if (SUCCEEDED(pTexture->QueryInterface(IID_CAfxManagedDirect3DVolumeTexture9, (void **)&afxWrapper)))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
			{
				CAfxManagedDirect3DCubeTexture9 * afxWrapper;

				if (SUCCEEDED(pTexture->QueryInterface(IID_CAfxManagedDirect3DCubeTexture9, (void **)&afxWrapper)))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
		}
#endif
		return pTexture;
	}

	IDirect3DVertexBuffer9 * UnwrapVertexBuffer(IDirect3DVertexBuffer9 * buffer)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && buffer)
		{
			{
				CAfxManagedDirect3DVertexBuffer9 * afxWrapper;

				if (SUCCEEDED(buffer->QueryInterface(IID_CAfxManagedDirect3DVertexBuffer9, (void **)&afxWrapper)))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
		}
#endif
		return buffer;
	}

	IDirect3DVertexBuffer9 * UnwrapVertexBufferReverse(IDirect3DVertexBuffer9 * buffer, bool handleRef)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && buffer)
		{
			{
				IDirect3DVertexBuffer9 * afxWrapper;
				DWORD afxWrapperSize = sizeof(afxWrapper);
				if (SUCCEEDED(buffer->GetPrivateData(IID_CAfxManagedDirect3DVertexBuffer9, &afxWrapper, &afxWrapperSize)))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						buffer->Release();
					}

					return afxWrapper;
				}
			}
		}
#endif
		return buffer;
	}

	IDirect3DIndexBuffer9 * UnwrapIndexBuffer(IDirect3DIndexBuffer9 * buffer)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && buffer)
		{
			{
				CAfxManagedDirect3DIndexBuffer9 * afxWrapper;

				if (SUCCEEDED(buffer->QueryInterface(IID_CAfxManagedDirect3DIndexBuffer9, (void **)&afxWrapper)))
				{
					return afxWrapper->AfxGetOrCreateUnmanaged();
				}
			}
		}
#endif
		return buffer;
	}

	IDirect3DIndexBuffer9 * UnwrapIndexBufferReverse(IDirect3DIndexBuffer9 * buffer, bool handleRef)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && buffer)
		{
			{
				IDirect3DIndexBuffer9 * afxWrapper;
				DWORD afxWrapperSize = sizeof(afxWrapper);
				if (SUCCEEDED(buffer->GetPrivateData(IID_CAfxManagedDirect3DIndexBuffer9, &afxWrapper, &afxWrapperSize)))
				{
					if (handleRef)
					{
						afxWrapper->AddRef();
						buffer->Release();
					}

					return afxWrapper;
				}
			}
		}
#endif
		return buffer;
	}


#ifdef AFX_INTEROP
	void ReleaseSharedRenderTargets()
	{
		if (AfxInterop::Enabled())
		{
			if (sharedRenderTarget)
			{
				sharedRenderTarget->DeviceLost();
			}

			if (sharedDepthStencilSurface)
			{
				sharedDepthStencilSurface->DeviceLost();
			}

			if (orgDeviceContextRenderTarget)
			{
				g_NewDirect3DDevice9.SetRenderTarget(0, orgDeviceContextRenderTarget);
				orgDeviceContextRenderTarget->Release();
				orgDeviceContextRenderTarget = nullptr;
			}
			if (orgDepthStencilSurface)
			{
				g_NewDirect3DDevice9.SetDepthStencilSurface(orgDepthStencilSurface);
				orgDepthStencilSurface->Release();
				orgDepthStencilSurface = nullptr;
			}
		}
	}

	void CreateSharedRenderTargets(D3DPRESENT_PARAMETERS* pPresentationParameters)
	{
		if (AfxInterop::Enabled())
		{
			if (nullptr == orgDeviceContextRenderTarget)
			{
				if (SUCCEEDED(g_OldDirect3DDevice9->GetRenderTarget(0, &orgDeviceContextRenderTarget)))
				{
					D3DSURFACE_DESC desc;

					if (SUCCEEDED(orgDeviceContextRenderTarget->GetDesc(&desc)))
					{
						if (nullptr == sharedRenderTarget) sharedRenderTarget = new CSharedSurfaceHook();

						HRESULT hr;
						HANDLE sharedHandle = NULL;
						IDirect3DSurface9 * surface = nullptr;

						if (SUCCEEDED(hr = g_OldDirect3DDevice9->CreateRenderTarget(desc.Width, desc.Height, desc.Format, pPresentationParameters->MultiSampleType, pPresentationParameters->MultiSampleQuality, FALSE, &surface, &sharedHandle)))
						{
							sharedRenderTarget->DeviceReset(desc.Width, desc.Height, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, pPresentationParameters->MultiSampleType, pPresentationParameters->MultiSampleQuality, surface, sharedHandle);

							if (FAILED(g_NewDirect3DDevice9.SetRenderTarget(0, sharedRenderTarget)))
								Tier0_Warning("AfxInterop: CreateSharedRenderTarget set sharedRenderTarget failed with 0x%08x.\n", hr);
						}
						else Tier0_Warning("AfxInterop: CreateSharedRenderTarget sharedRenderTarget failed with 0x%08x.\n", hr);

					}
				}
				else
				{
					orgDeviceContextRenderTarget = nullptr;
				}
			}

			if (nullptr == orgDepthStencilSurface)
			{
				if (SUCCEEDED(g_OldDirect3DDevice9->GetDepthStencilSurface(&orgDepthStencilSurface)))
				{
					D3DSURFACE_DESC desc;

					if (SUCCEEDED(orgDepthStencilSurface->GetDesc(&desc)))
					{
						if (nullptr == sharedDepthStencilSurface) sharedDepthStencilSurface = new CSharedSurfaceHook();

						HRESULT hr;
						HANDLE sharedHandle = NULL;
						IDirect3DSurface9 * surface = nullptr;

						// This has to happen transparently to CS:GO:
						if (desc.Format == D3DFMT_D24S8)
						{
							Tier0_Msg("AfxHookSource: Secretly requesting INTZ format ...\n");
							desc.Format = FOURCC_INTZ;
						}

						if (SUCCEEDED(hr = g_OldDirect3DDevice9->CreateDepthStencilSurface(desc.Width, desc.Height, desc.Format, pPresentationParameters->MultiSampleType, pPresentationParameters->MultiSampleQuality, FALSE, &surface, &sharedHandle)))
						{
							sharedDepthStencilSurface->DeviceReset(desc.Width, desc.Height, D3DUSAGE_DEPTHSTENCIL, desc.Format, D3DPOOL_DEFAULT, pPresentationParameters->MultiSampleType, pPresentationParameters->MultiSampleQuality, surface, sharedHandle);

							if (FAILED(g_NewDirect3DDevice9.SetDepthStencilSurface(sharedDepthStencilSurface)))
								Tier0_Warning("AfxInterop: CreateSharedRenderTarget set sharedDepthStencilSurface failed with 0x%08x.\n", hr);
						}
						else Tier0_Warning("AfxInterop: CreateSharedRenderTarget sharedDepthStencilSurface failed with 0x%08x.\n", hr);


					}
				}
				else
				{
					orgDepthStencilSurface = nullptr;
				}
			}
		}
	}
#endif

private:
	bool m_Block_Present = false;

	class CAfxOverride
	{
	public:
		bool m_Override_D3DRS_SRCBLEND;
		DWORD m_OverrideValue_D3DRS_SRCBLEND;

		bool m_Override_D3DRS_DESTBLEND;
		DWORD m_OverrideValue_D3DRS_DESTBLEND;

		bool m_Override_D3DRS_SRGBWRITEENABLE;
		DWORD m_OverrideValue_D3DRS_SRGBWRITEENABLE;

		bool m_Override_D3DRS_ZWRITEENABLE;
		DWORD m_OverrideValue_D3DRS_ZWRITEENABLE;

		bool m_Override_D3DRS_ALPHABLENDENABLE;
		DWORD m_OverrideValue_D3DRS_ALPHABLENDENABLE;

		bool m_Override_ps_c0;
		float m_OverrideValue_ps_c0[4];

		bool m_Override_ps_c5;
		float m_OverrideValue_ps_c5[4];

		bool m_Override_ps_c12_y;
		float m_OverrideValue_ps_c12_y;

		bool m_Override_ps_c29_w;
		float m_OverrideValue_ps_c29_w;

		bool m_Override_ps_c31;
		float m_OverrideValue_ps_c31[4];

		bool m_Override_ps_c1_xyz;
		float m_OverrideValue_ps_c1_xyz[3];
		bool m_Override_ps_c1_w;
		float m_OverrideValue_ps_c1_w;

		bool m_Override_VertexShader;
		IDirect3DVertexShader9 * m_OverrideValue_VertexShader;

		bool m_Override_PixelShader;
		IDirect3DPixelShader9 * m_OverrideValue_PixelShader;

		CAfxOverride(NewDirect3DDevice9 & dev)
			: m_Dev(dev)
			, m_Override_D3DRS_SRCBLEND(false)
			, m_Override_D3DRS_DESTBLEND(false)
			, m_Override_D3DRS_SRGBWRITEENABLE(false)
			, m_Override_D3DRS_ZWRITEENABLE(false)
			, m_Override_D3DRS_ALPHABLENDENABLE(false)
			, m_Override_ps_c0(false)
			, m_Override_ps_c5(false)
			, m_Override_ps_c12_y(false)
			, m_Override_ps_c29_w(false)
			, m_Override_ps_c31(false)
			, m_Override_ps_c1_xyz(false)
			, m_Override_ps_c1_w(false)
			, m_Override_VertexShader(false)
			, m_Override_PixelShader(false)
		{
		}

		void Redo(void)
		{
			if (m_Override_D3DRS_SRCBLEND) m_Dev.OverrideBegin_D3DRS_SRCBLEND(m_OverrideValue_D3DRS_SRCBLEND);
			if (m_Override_D3DRS_DESTBLEND) m_Dev.OverrideBegin_D3DRS_DESTBLEND(m_OverrideValue_D3DRS_DESTBLEND);
			if (m_Override_D3DRS_SRGBWRITEENABLE) m_Dev.OverrideBegin_D3DRS_SRGBWRITEENABLE(m_OverrideValue_D3DRS_SRGBWRITEENABLE);
			if (m_Override_D3DRS_ZWRITEENABLE) m_Dev.OverrideBegin_D3DRS_ZWRITEENABLE(m_OverrideValue_D3DRS_ZWRITEENABLE);
			if (m_Override_D3DRS_ALPHABLENDENABLE) m_Dev.OverrideBegin_D3DRS_ALPHABLENDENABLE(m_OverrideValue_D3DRS_ALPHABLENDENABLE);
			if (m_Override_ps_c0) m_Dev.OverrideBegin_ps_c0(m_OverrideValue_ps_c0);
			if (m_Override_ps_c5) m_Dev.OverrideBegin_ps_c5(m_OverrideValue_ps_c5);
			if (m_Override_ps_c12_y) m_Dev.OverrideBegin_ps_c12_y(m_OverrideValue_ps_c12_y);
			if (m_Override_ps_c29_w) m_Dev.OverrideBegin_ps_c29_w(m_OverrideValue_ps_c29_w);
			if (m_Override_ps_c31) m_Dev.OverrideBegin_ps_c31(m_OverrideValue_ps_c31);
			if (m_Override_ps_c1_xyz) m_Dev.OverrideBegin_ps_c1_xyz(m_OverrideValue_ps_c1_xyz);
			if (m_Override_ps_c1_w) m_Dev.OverrideBegin_ps_c1_w(m_OverrideValue_ps_c1_w);
			if (m_Override_VertexShader) m_Dev.OverrideBegin_SetVertexShader(m_OverrideValue_VertexShader);
			if (m_Override_PixelShader) m_Dev.OverrideBegin_SetPixelShader(m_OverrideValue_PixelShader);
		}

		void Undo(void)
		{
			if (m_Override_D3DRS_SRCBLEND) m_Dev.OverrideEnd_D3DRS_SRCBLEND();
			if (m_Override_D3DRS_DESTBLEND) m_Dev.OverrideEnd_D3DRS_DESTBLEND();
			if (m_Override_D3DRS_SRGBWRITEENABLE) m_Dev.OverrideEnd_D3DRS_SRGBWRITEENABLE();
			if (m_Override_D3DRS_ZWRITEENABLE) m_Dev.OverrideEnd_D3DRS_ZWRITEENABLE();
			if (m_Override_D3DRS_ALPHABLENDENABLE) m_Dev.OverrideEnd_D3DRS_ALPHABLENDENABLE();
			if (m_Override_ps_c0) m_Dev.OverrideEnd_ps_c0();
			if (m_Override_ps_c5) m_Dev.OverrideEnd_ps_c5();
			if (m_Override_ps_c12_y) m_Dev.OverrideEnd_ps_c12_y();
			if (m_Override_ps_c29_w) m_Dev.OverrideEnd_ps_c29_w();
			if (m_Override_ps_c31) m_Dev.OverrideEnd_ps_c31();
			if (m_Override_ps_c1_xyz) m_Dev.OverrideEnd_ps_c1_xyz();
			if (m_Override_ps_c1_w) m_Dev.OverrideEnd_ps_c1_w();
			if (m_Override_VertexShader) m_Dev.OverrideEnd_SetVertexShader();
			if (m_Override_PixelShader) m_Dev.OverrideEnd_SetPixelShader();
		}

	private:
		NewDirect3DDevice9 & m_Dev;

	};

	std::stack<CAfxOverride> m_OverrideStack;

public:
	NewDirect3DDevice9()
	{
		m_OverrideStack.emplace(*this);
	}

	void OverrideBegin_D3DRS_SRCBLEND(DWORD value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_D3DRS_SRCBLEND = true;
		curOverride.m_OverrideValue_D3DRS_SRCBLEND = value;

		g_OldDirect3DDevice9->SetRenderState(D3DRS_SRCBLEND, value);
	}

	void OverrideEnd_D3DRS_SRCBLEND(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_D3DRS_SRCBLEND)
		{
			curOverride.m_Override_D3DRS_SRCBLEND = false;

			g_OldDirect3DDevice9->SetRenderState(D3DRS_SRCBLEND, m_D3DRS_SRCBLEND);
		}
	}

	void OverrideBegin_D3DRS_DESTBLEND(DWORD value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_D3DRS_DESTBLEND = true;
		curOverride.m_OverrideValue_D3DRS_DESTBLEND = value;

		g_OldDirect3DDevice9->SetRenderState(D3DRS_DESTBLEND, value);
	}

	void OverrideEnd_D3DRS_DESTBLEND(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_D3DRS_DESTBLEND)
		{
			curOverride.m_Override_D3DRS_DESTBLEND = false;

			g_OldDirect3DDevice9->SetRenderState(D3DRS_DESTBLEND, m_D3DRS_DESTBLEND);
		}
	}

	void OverrideBegin_D3DRS_SRGBWRITEENABLE(DWORD value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_D3DRS_SRGBWRITEENABLE = true;
		curOverride.m_OverrideValue_D3DRS_SRGBWRITEENABLE = value;

		g_OldDirect3DDevice9->SetRenderState(D3DRS_SRGBWRITEENABLE, value);
	}

	void OverrideEnd_D3DRS_SRGBWRITEENABLE(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_D3DRS_SRGBWRITEENABLE)
		{
			curOverride.m_Override_D3DRS_SRGBWRITEENABLE = false;

			g_OldDirect3DDevice9->SetRenderState(D3DRS_SRGBWRITEENABLE, m_D3DRS_SRGBWRITEENABLE);
		}
	}

	void OverrideBegin_D3DRS_ZWRITEENABLE(DWORD value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_D3DRS_ZWRITEENABLE = true;
		curOverride.m_OverrideValue_D3DRS_ZWRITEENABLE = value;

		g_OldDirect3DDevice9->SetRenderState(D3DRS_ZWRITEENABLE, value);
	}

	void OverrideEnd_D3DRS_ZWRITEENABLE(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_D3DRS_ZWRITEENABLE)
		{
			curOverride.m_Override_D3DRS_ZWRITEENABLE = false;

			g_OldDirect3DDevice9->SetRenderState(D3DRS_ZWRITEENABLE, m_D3DRS_ZWRITEENABLE);
		}
	}

	void OverrideBegin_D3DRS_ALPHABLENDENABLE(DWORD value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_D3DRS_ALPHABLENDENABLE = true;
		curOverride.m_OverrideValue_D3DRS_ALPHABLENDENABLE = value;

		g_OldDirect3DDevice9->SetRenderState(D3DRS_ALPHABLENDENABLE, value);
	}

	void OverrideEnd_D3DRS_ALPHABLENDENABLE(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_D3DRS_ALPHABLENDENABLE)
		{
			curOverride.m_Override_D3DRS_ALPHABLENDENABLE = false;

			g_OldDirect3DDevice9->SetRenderState(D3DRS_ALPHABLENDENABLE, m_D3DRS_ALPHABLENDENABLE);
		}
	}

	void OverrideBegin_SetVertexShader(IDirect3DVertexShader9 * override_VertexShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_VertexShader = true;
		curOverride.m_OverrideValue_VertexShader = override_VertexShader;
		if(curOverride.m_OverrideValue_VertexShader) curOverride.m_OverrideValue_VertexShader->AddRef();

		g_OldDirect3DDevice9->SetVertexShader(override_VertexShader);
	}

	void OverrideEnd_SetVertexShader()
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_VertexShader)
		{
			curOverride.m_Override_VertexShader = false;

			g_OldDirect3DDevice9->SetVertexShader(m_Original_VertexShader);

			if(curOverride.m_OverrideValue_VertexShader) curOverride.m_OverrideValue_VertexShader->Release();
		}
	}

	void OverrideBegin_SetPixelShader(IDirect3DPixelShader9 * override_PixelShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_PixelShader = true;
		curOverride.m_OverrideValue_PixelShader = override_PixelShader;
		if(curOverride.m_OverrideValue_PixelShader) curOverride.m_OverrideValue_PixelShader->AddRef();

		g_OldDirect3DDevice9->SetPixelShader(override_PixelShader);
	}

	void OverrideEnd_SetPixelShader()
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_PixelShader)
		{
			curOverride.m_Override_PixelShader = false;

			g_OldDirect3DDevice9->SetPixelShader(m_Original_PixelShader);

			if(curOverride.m_OverrideValue_PixelShader) curOverride.m_OverrideValue_PixelShader->Release();
		}
	}

	void OverrideBegin_ps_c0(float const values[4])
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c0 = true;
		curOverride.m_OverrideValue_ps_c0[0] = values[0];
		curOverride.m_OverrideValue_ps_c0[1] = values[1];
		curOverride.m_OverrideValue_ps_c0[2] = values[2];
		curOverride.m_OverrideValue_ps_c0[3] = values[3];

		g_OldDirect3DDevice9->SetPixelShaderConstantF(0, curOverride.m_OverrideValue_ps_c0, 1);
	}

	void OverrideEnd_ps_c0(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_ps_c0)
		{
			curOverride.m_Override_ps_c0 = false;

			g_OldDirect3DDevice9->SetPixelShaderConstantF(0, m_OriginalValue_ps_c0, 1);
		}
	}

	void OverrideBegin_ps_c5(float const values[4])
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c5 = true;
		curOverride.m_OverrideValue_ps_c5[0] = values[0];
		curOverride.m_OverrideValue_ps_c5[1] = values[1];
		curOverride.m_OverrideValue_ps_c5[2] = values[2];
		curOverride.m_OverrideValue_ps_c5[3] = values[3];

		g_OldDirect3DDevice9->SetPixelShaderConstantF(5, curOverride.m_OverrideValue_ps_c5, 1);
	}

	void OverrideEnd_ps_c5(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_ps_c5)
		{
			curOverride.m_Override_ps_c5 = false;

			g_OldDirect3DDevice9->SetPixelShaderConstantF(5, m_OriginalValue_ps_c5, 1);
		}
	}

	void OverrideBegin_ps_c12_y(float value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c12_y = true;
		curOverride.m_OverrideValue_ps_c12_y = value;

		float tmp[4] = { m_OriginalValue_ps_c12[0], value, m_OriginalValue_ps_c12[2], m_OriginalValue_ps_c12[3] };
		g_OldDirect3DDevice9->SetPixelShaderConstantF(12, tmp, 1);
	}

	void OverrideEnd_ps_c12_y(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_ps_c12_y)
		{
			curOverride.m_Override_ps_c12_y = false;

			g_OldDirect3DDevice9->SetPixelShaderConstantF(12, m_OriginalValue_ps_c12, 1);
		}
	}

	void OverrideBegin_ps_c29_w(float value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c29_w = true;
		curOverride.m_OverrideValue_ps_c29_w = value;

		float tmp[4] = { m_OriginalValue_ps_c29[0], m_OriginalValue_ps_c29[1], m_OriginalValue_ps_c29[2], value };
		g_OldDirect3DDevice9->SetPixelShaderConstantF(29, tmp, 1);
	}

	void OverrideEnd_ps_c29_w(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_ps_c29_w)
		{
			curOverride.m_Override_ps_c29_w = false;

			g_OldDirect3DDevice9->SetPixelShaderConstantF(29, m_OriginalValue_ps_c29, 1);
		}
	}

	void OverrideBegin_ps_c31(float const values[4])
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c31 = true;

		curOverride.m_OverrideValue_ps_c31[0] = values[0];
		curOverride.m_OverrideValue_ps_c31[1] = values[1];
		curOverride.m_OverrideValue_ps_c31[2] = values[2];
		curOverride.m_OverrideValue_ps_c31[3] = values[3];

		g_OldDirect3DDevice9->SetPixelShaderConstantF(31, curOverride.m_OverrideValue_ps_c31, 1);
	}

	void OverrideEnd_ps_c31(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_ps_c31)
		{
			curOverride.m_Override_ps_c31 = false;

			g_OldDirect3DDevice9->SetPixelShaderConstantF(31, m_OriginalValue_ps_c31, 1);
		}
	}

	void OverrideBegin_ps_c1_xyz(float const value[3])
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c1_xyz = true;
		curOverride.m_OverrideValue_ps_c1_xyz[0] = value[0];
		curOverride.m_OverrideValue_ps_c1_xyz[1] = value[1];
		curOverride.m_OverrideValue_ps_c1_xyz[2] = value[2];

		float tmp[4] = { value[0], value[1], value[2], curOverride.m_Override_ps_c1_w ? curOverride.m_OverrideValue_ps_c1_w : m_OriginalValue_ps_c1[3] };
		g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
	}

	void OverrideEnd_ps_c1_xyz(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if (curOverride.m_Override_ps_c1_xyz)
		{
			curOverride.m_Override_ps_c1_xyz = false;

			float tmp[4] = { m_OriginalValue_ps_c1[0], m_OriginalValue_ps_c1[1], m_OriginalValue_ps_c1[2], curOverride.m_Override_ps_c1_w ? curOverride.m_OverrideValue_ps_c1_w : m_OriginalValue_ps_c1[3] };
			g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
		}
	}

	void OverrideBegin_ps_c1_w(float value)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		curOverride.m_Override_ps_c1_w = true;
		curOverride.m_OverrideValue_ps_c1_w = value;

		float tmp[4] = { curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[0] : m_OriginalValue_ps_c1[0], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[1] : m_OriginalValue_ps_c1[1], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[2] : m_OriginalValue_ps_c1[2], value };
		g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
	}

	void OverrideEnd_ps_c1_w(void)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if (curOverride.m_Override_ps_c1_w)
		{
			curOverride.m_Override_ps_c1_w = false;

			float tmp[4] = { curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[0] : m_OriginalValue_ps_c1[0], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[1] : m_OriginalValue_ps_c1[1], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[2] : m_OriginalValue_ps_c1[2], m_OriginalValue_ps_c1[3] };
			g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
		}
	}

	void Block_Present(bool block)
	{
		m_Block_Present = block;
	}

	void On_AfxHookDirect3DStateBlock9_Applied(void)
	{
		//Tier0_Warning("On_AfxHookDirect3DStateBlock9_Applied\n");

		CAfxOverride & curOverride = m_OverrideStack.top();

		g_OldDirect3DDevice9->GetRenderState(D3DRS_SRCBLEND, &m_D3DRS_SRCBLEND);
		if (curOverride.m_Override_D3DRS_SRCBLEND)
			g_OldDirect3DDevice9->SetRenderState(D3DRS_SRCBLEND, curOverride.m_OverrideValue_D3DRS_SRCBLEND);

		g_OldDirect3DDevice9->GetRenderState(D3DRS_DESTBLEND, &m_D3DRS_DESTBLEND);
		if (curOverride.m_Override_D3DRS_DESTBLEND)
			g_OldDirect3DDevice9->SetRenderState(D3DRS_DESTBLEND, curOverride.m_OverrideValue_D3DRS_DESTBLEND);

		g_OldDirect3DDevice9->GetRenderState(D3DRS_SRGBWRITEENABLE, &m_D3DRS_SRGBWRITEENABLE);
		if (curOverride.m_Override_D3DRS_SRGBWRITEENABLE)
			g_OldDirect3DDevice9->SetRenderState(D3DRS_SRGBWRITEENABLE, curOverride.m_OverrideValue_D3DRS_SRGBWRITEENABLE);

		g_OldDirect3DDevice9->GetRenderState(D3DRS_ZWRITEENABLE, &m_D3DRS_ZWRITEENABLE);
		if (curOverride.m_Override_D3DRS_ZWRITEENABLE)
			g_OldDirect3DDevice9->SetRenderState(D3DRS_ZWRITEENABLE, curOverride.m_OverrideValue_D3DRS_ZWRITEENABLE);

		g_OldDirect3DDevice9->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_D3DRS_ALPHABLENDENABLE);
		if (curOverride.m_Override_D3DRS_ALPHABLENDENABLE)
			g_OldDirect3DDevice9->SetRenderState(D3DRS_ALPHABLENDENABLE, curOverride.m_OverrideValue_D3DRS_ALPHABLENDENABLE);

		g_OldDirect3DDevice9->GetPixelShaderConstantF(0, m_OriginalValue_ps_c0, 1);
		if (curOverride.m_Override_ps_c0)
		{
			g_OldDirect3DDevice9->SetPixelShaderConstantF(0, curOverride.m_OverrideValue_ps_c0, 1);
		}

		g_OldDirect3DDevice9->GetPixelShaderConstantF(5, m_OriginalValue_ps_c5, 1);
		if (curOverride.m_Override_ps_c5)
		{
			g_OldDirect3DDevice9->SetPixelShaderConstantF(5, curOverride.m_OverrideValue_ps_c5, 1);
		}

		g_OldDirect3DDevice9->GetPixelShaderConstantF(12, m_OriginalValue_ps_c12, 1);
		if (curOverride.m_Override_ps_c12_y)
		{
			float tmp[4] = { m_OriginalValue_ps_c12[0], curOverride.m_OverrideValue_ps_c12_y, m_OriginalValue_ps_c12[2], m_OriginalValue_ps_c12[3] };
			g_OldDirect3DDevice9->SetPixelShaderConstantF(12, tmp, 1);
		}

		g_OldDirect3DDevice9->GetPixelShaderConstantF(29, m_OriginalValue_ps_c29, 1);
		if (curOverride.m_Override_ps_c29_w)
		{
			float tmp[4] = { m_OriginalValue_ps_c29[0], m_OriginalValue_ps_c29[1], m_OriginalValue_ps_c29[2], curOverride.m_OverrideValue_ps_c29_w };
			g_OldDirect3DDevice9->SetPixelShaderConstantF(29, tmp, 1);
		}

		g_OldDirect3DDevice9->GetPixelShaderConstantF(31, m_OriginalValue_ps_c31, 1);
		if (curOverride.m_Override_ps_c31)
		{
			g_OldDirect3DDevice9->SetPixelShaderConstantF(31, curOverride.m_OverrideValue_ps_c31, 1);
		}

		g_OldDirect3DDevice9->GetPixelShaderConstantF(1, m_OriginalValue_ps_c1, 1);
		if (curOverride.m_Override_ps_c1_xyz || curOverride.m_Override_ps_c1_w)
		{
			float tmp[4] = { curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[0] : m_OriginalValue_ps_c1[0], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[1] : m_OriginalValue_ps_c1[1],curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[2] : m_OriginalValue_ps_c1[2], curOverride.m_Override_ps_c1_w ? curOverride.m_OverrideValue_ps_c1_w : m_OriginalValue_ps_c1[3] };
			g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
		}


		{
			IDirect3DVertexShader9 * pShader = 0;
			g_OldDirect3DDevice9->GetVertexShader(&pShader);
			if (curOverride.m_Override_VertexShader)
			{
				if (pShader != curOverride.m_OverrideValue_VertexShader)
				{
					if (m_Original_VertexShader) m_Original_VertexShader->Release();
					m_Original_VertexShader = pShader;
					if (m_Original_VertexShader) m_Original_VertexShader->AddRef();

					g_OldDirect3DDevice9->SetVertexShader(curOverride.m_OverrideValue_VertexShader);
				}
			}
			else
			{
				if (m_Original_VertexShader) m_Original_VertexShader->Release();
				m_Original_VertexShader = pShader;
				if (m_Original_VertexShader) m_Original_VertexShader->AddRef();
			}
		}

		{
			IDirect3DPixelShader9 * pShader = 0;
			g_OldDirect3DDevice9->GetPixelShader(&pShader);
			if (curOverride.m_Override_PixelShader)
			{
				if (pShader != curOverride.m_OverrideValue_PixelShader)
				{
					if (m_Original_PixelShader) m_Original_PixelShader->Release();
					m_Original_PixelShader = pShader;
					if (m_Original_PixelShader) m_Original_PixelShader->AddRef();

					g_OldDirect3DDevice9->SetPixelShader(curOverride.m_OverrideValue_PixelShader);
				}
			}
			else
			{
				if (m_Original_PixelShader) m_Original_PixelShader->Release();
				m_Original_PixelShader = pShader;
				if (m_Original_PixelShader) m_Original_PixelShader->AddRef();
			}
		}

	}

	void AfxPushOverrideState(void)
	{
		m_OverrideStack.top().Undo();

		m_OverrideStack.emplace(*this);

		m_OverrideStack.top().Redo();
	}

	void AfxPopOverrideState(void)
	{
		m_OverrideStack.top().Undo();

		m_OverrideStack.pop();

		m_OverrideStack.top().Redo();
	}


    /*** IUnknown methods ***/

	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) {

		if (riid == __uuidof(IDirect3DDevice9Ex))
		{
			if (ppvObj) ppvObj = NULL;
			return E_NOINTERFACE; // If we shoved in D3D9Ex, make sure CS:GO does not notice it.
		}

		return g_OldDirect3DDevice9->QueryInterface(riid, ppvObj);
	}


	STDMETHOD_(ULONG,AddRef)(THIS)
	{
		ULONG result = g_OldDirect3DDevice9->AddRef();

		++g_NewDirect3DDevice9_RefCount;

		return result;
	}

    STDMETHOD_(ULONG,Release)(THIS)
	{
		--g_NewDirect3DDevice9_RefCount;

		if(0 == g_NewDirect3DDevice9_RefCount)
		{
			Shared_Direct3DDevice9_Shutdown();

			if(m_Original_VertexShader)
			{
				m_Original_VertexShader->Release();
				m_Original_VertexShader = 0;
			}

			if(m_Original_PixelShader)
			{
				m_Original_PixelShader->Release();
				m_Original_PixelShader = 0;
			}

			ReleaseSharedRenderTargets();

			if (sharedRenderTarget) sharedRenderTarget->Release();
			if (sharedDepthStencilSurface) sharedDepthStencilSurface->Release();
		}

		return g_OldDirect3DDevice9->Release();
	}


    /*** IDirect3DDevice9 methods ***/

	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, TestCooperativeLevel);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetAvailableTextureMem);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, EvictManagedResources);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetDirect3D);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetDeviceCaps);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetDisplayMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetCreationParameters);
    
	STDMETHOD(SetCursorProperties)(THIS_ UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
	{
		return g_OldDirect3DDevice9->SetCursorProperties(XHotSpot, YHotSpot, UnwrapSurface(pCursorBitmap));
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetCursorPosition);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, ShowCursor);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, CreateAdditionalSwapChain);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetSwapChain);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetNumberOfSwapChains);
    
	STDMETHOD(Reset)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters)
	{
		if(pPresentationParameters) FixPresentationparementers(pPresentationParameters);

		Shared_Direct3DDevice9_Reset_Before();

#if AFX_INTEROP
		ReleaseSharedRenderTargets();
		ReleaseQueries();
#endif

		HRESULT hResult = g_OldDirect3DDevice9->Reset(pPresentationParameters);

#if AFX_INTEROP
		if (SUCCEEDED(hResult))
		{
			CreateQueries();
			g_NewDirect3DDevice9.CreateSharedRenderTargets(pPresentationParameters);
		}
#endif

		Shared_Direct3DDevice9_Reset_After();

		return hResult;
	}

    STDMETHOD(Present)(THIS_ CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion)
	{
		if (m_Block_Present)
			return D3D_OK;

#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (orgDeviceContextRenderTarget && sharedRenderTarget)
			{
				if(IDirect3DSurface9 * sharedSurface = sharedRenderTarget->GetSharedSurface())
					g_OldDirect3DDevice9->StretchRect(sharedSurface, pSourceRect, orgDeviceContextRenderTarget, pDestRect, D3DTEXF_NONE);
			}
		}
#endif

		HRESULT result = g_OldDirect3DDevice9->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

#if AFX_INTEROP
		if (result == D3DERR_DEVICELOST)
		{
			if (sharedDepthStencilSurface) sharedDepthStencilSurface->DeviceLost();
			if (sharedRenderTarget) sharedRenderTarget->DeviceLost();

			CAfxManagedChildDirect3DSurface9::AfxDeviceLost();
			CAfxManagedDirect3DTexture9::AfxDeviceLost();
			CAfxManagedDirect3DVolumeTexture9::AfxDeviceLost();
			CAfxManagedDirect3DCubeTexture9::AfxDeviceLost();
			CAfxManagedOffscreenPlainSurface::AfxDeviceLost();
			CAfxManagedDirect3DVertexBuffer9::AfxDeviceLost();
			CAfxManagedDirect3DIndexBuffer9::AfxDeviceLost();
		}

		CAfxManagedDirect3DIndexBuffer9::AfxDevicePresented();
		CAfxManagedDirect3DVertexBuffer9::AfxDevicePresented();
		CAfxManagedOffscreenPlainSurface::AfxDevicePresented();
		CAfxManagedDirect3DVolumeTexture9::AfxDevicePresented();
		CAfxManagedDirect3DCubeTexture9::AfxDevicePresented();
		CAfxManagedDirect3DTexture9::AfxDevicePresented();
		CAfxManagedChildDirect3DSurface9::AfxDevicePresented();
#endif

		Shared_Direct3DDevice9_Present(result == D3DERR_DEVICELOST, m_Block_Present);

		return result;
	}

	STDMETHOD(GetBackBuffer)(THIS_ UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer)
	{
		HRESULT result = g_OldDirect3DDevice9->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);

		if (SUCCEEDED(result) && ppBackBuffer) *ppBackBuffer = UnwrapSurfaceReverse(*ppBackBuffer, true);

		return result;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetRasterStatus);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetDialogBoxMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetGammaRamp);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetGammaRamp);

	STDMETHOD(CreateTexture)(THIS_ UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DTexture9 * pSystemMemPoolTexture;
				HRESULT result = g_OldDirect3DDevice9->CreateTexture(Width, Height, Levels, Usage, Format, D3DPOOL_SYSTEMMEM, &pSystemMemPoolTexture, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedDirect3DTexture9 * texture = new CAfxManagedDirect3DTexture9(Width, Height, Levels, Usage, Format, pSystemMemPoolTexture);
					
					if (FAILED(pSystemMemPoolTexture->SetPrivateData(IID_CAfxManagedDirect3DTexture9, &texture, sizeof(texture), 0)))
					{
						texture->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppTexture = texture;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
	}

	STDMETHOD(CreateVolumeTexture)(THIS_ UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DVolumeTexture9 * pSystemMemPoolTexture;
				HRESULT result = g_OldDirect3DDevice9->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, D3DPOOL_SYSTEMMEM, &pSystemMemPoolTexture, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedDirect3DVolumeTexture9 * texture = new CAfxManagedDirect3DVolumeTexture9(Width, Height, Depth, Levels, Usage, Format, pSystemMemPoolTexture);
					
					if (FAILED(pSystemMemPoolTexture->SetPrivateData(IID_CAfxManagedDirect3DVolumeTexture9, &texture, sizeof(texture), 0)))
					{
						texture->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppVolumeTexture = texture;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
	}
	
	STDMETHOD(CreateCubeTexture)(THIS_ UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DCubeTexture9 * pSystemMemPoolTexture;
				HRESULT result = g_OldDirect3DDevice9->CreateCubeTexture(EdgeLength, Levels, Usage, Format, D3DPOOL_SYSTEMMEM, &pSystemMemPoolTexture, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedDirect3DCubeTexture9 * texture = new CAfxManagedDirect3DCubeTexture9(EdgeLength, Levels, Usage, Format, pSystemMemPoolTexture);

					if (FAILED(pSystemMemPoolTexture->SetPrivateData(IID_CAfxManagedDirect3DCubeTexture9, &texture, sizeof(texture), 0)))
					{
						texture->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppCubeTexture = texture;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
	}

	STDMETHOD(CreateVertexBuffer)(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DVertexBuffer9 * pSystemMemPool;
				HRESULT result = g_OldDirect3DDevice9->CreateVertexBuffer(Length, Usage, FVF, D3DPOOL_SYSTEMMEM, &pSystemMemPool, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedDirect3DVertexBuffer9 * buffer = new CAfxManagedDirect3DVertexBuffer9(Length, Usage, FVF, pSystemMemPool);

					if (FAILED(pSystemMemPool->SetPrivateData(IID_CAfxManagedDirect3DVertexBuffer9, &buffer, sizeof(buffer), 0)))
					{
						buffer->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppVertexBuffer = buffer;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
	}

	STDMETHOD(CreateIndexBuffer)(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DIndexBuffer9 * pSystemMemPool;
				HRESULT result = g_OldDirect3DDevice9->CreateIndexBuffer(Length, Usage, Format, D3DPOOL_SYSTEMMEM, &pSystemMemPool, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedDirect3DIndexBuffer9 * buffer = new CAfxManagedDirect3DIndexBuffer9(Length, Usage, Format, pSystemMemPool);

					if (FAILED(pSystemMemPool->SetPrivateData(IID_CAfxManagedDirect3DIndexBuffer9, &buffer, sizeof(buffer), 0)))
					{
						buffer->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppIndexBuffer = buffer;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
	}

	STDMETHOD(CreateRenderTarget)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
	{
#if 0
		if (AfxInterop::Enabled())
		{
			HRESULT hr = AfxInterop::OnCreateRenderTarget(g_OldDirect3DDevice9, Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
			if (SUCCEEDED(hr)) return hr;
		}
#endif
		return g_OldDirect3DDevice9->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
	}

	STDMETHOD(CreateDepthStencilSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
	{
#if 0
		if (AfxInterop::Enabled())
		{
			HRESULT hr = AfxInterop::OnCreateDepthStencilSurface(g_OldDirect3DDevice9, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
			if (SUCCEEDED(hr)) return hr;
		}
#endif

		return g_OldDirect3DDevice9->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
	}

	STDMETHOD(UpdateSurface)(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint)
	{
		return g_OldDirect3DDevice9->UpdateSurface(UnwrapSurface(pSourceSurface), pSourceRect, UnwrapSurface(pDestinationSurface), pDestPoint);
	}

	STDMETHOD(UpdateTexture)(THIS_ IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture)
	{
		return g_OldDirect3DDevice9->UpdateTexture(UnwrapTexture(pSourceTexture), UnwrapTexture(pDestinationTexture));
	}
	
	STDMETHOD(GetRenderTargetData)(THIS_ IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface)
	{
		return g_OldDirect3DDevice9->GetRenderTargetData(UnwrapSurface(pRenderTarget), UnwrapSurface(pDestSurface));
	}

	STDMETHOD(GetFrontBufferData)(THIS_ UINT iSwapChain, IDirect3DSurface9* pDestSurface)
	{
		return g_OldDirect3DDevice9->GetFrontBufferData(iSwapChain, UnwrapSurface(pDestSurface));
	}

	STDMETHOD(StretchRect)(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter)
	{
		return g_OldDirect3DDevice9->StretchRect(UnwrapSurface(pSourceSurface), pSourceRect, UnwrapSurface(pDestSurface), pDestRect, Filter);
	}

	STDMETHOD(ColorFill)(THIS_ IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color)
	{
		return g_OldDirect3DDevice9->ColorFill(UnwrapSurface(pSurface), pRect, color);
	}
    
	STDMETHOD(CreateOffscreenPlainSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			if (Pool == D3DPOOL_MANAGED)
			{
				IDirect3DSurface9 * pSystemMemPool;
				HRESULT result = g_OldDirect3DDevice9->CreateOffscreenPlainSurface(Width, Height, Format, Pool, &pSystemMemPool, pSharedHandle);

				if (SUCCEEDED(result))
				{
					CAfxManagedOffscreenPlainSurface * surface = new CAfxManagedOffscreenPlainSurface(Width, Height, 0, Format, Pool, D3DMULTISAMPLE_NONE, 0, pSystemMemPool);

					if (FAILED(pSystemMemPool->SetPrivateData(IID_CAfxManagedOffscreenPlainSurface, &surface, sizeof(surface), 0)))
					{
						surface->Release();
						return D3DERR_NOTAVAILABLE;
					}

					*ppSurface = surface;
				}

				return result;
			}
		}
#endif

		return g_OldDirect3DDevice9->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
	}

	STDMETHOD(SetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			CSharedSurfaceHook * sharedSurfaceHook;

			if (GetSharedSurfaceHook(pRenderTarget, &sharedSurfaceHook))
			{
				HRESULT result = g_OldDirect3DDevice9->SetRenderTarget(RenderTargetIndex, sharedSurfaceHook->GetSharedSurface());

				AfxInterop::OnSetSharedRenderTarget(RenderTargetIndex, sharedSurfaceHook);

				return result;
			}

			HRESULT result = g_OldDirect3DDevice9->SetRenderTarget(RenderTargetIndex, UnwrapSurface(pRenderTarget));

			AfxInterop::OnSetSharedRenderTarget(RenderTargetIndex, nullptr);

			return result;
		}
#endif

		return g_OldDirect3DDevice9->SetRenderTarget(RenderTargetIndex, pRenderTarget);
	}

	STDMETHOD(GetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget)
	{
		HRESULT result = g_OldDirect3DDevice9->GetRenderTarget(RenderTargetIndex, ppRenderTarget);

		if (SUCCEEDED(result)) *ppRenderTarget = UnwrapSurfaceReverse(*ppRenderTarget, true);

		return result;
	}

	STDMETHOD(SetDepthStencilSurface)(THIS_ IDirect3DSurface9* pNewZStencil)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			CSharedSurfaceHook * sharedSurfaceHook;

			if (GetSharedSurfaceHook(pNewZStencil, &sharedSurfaceHook))
			{
				HRESULT result = g_OldDirect3DDevice9->SetDepthStencilSurface(sharedSurfaceHook->GetSharedSurface());

				AfxInterop::OnSetSharedDepthStencilSurface(sharedSurfaceHook);

				return result;
			}

			HRESULT result = g_OldDirect3DDevice9->SetDepthStencilSurface(UnwrapSurface(pNewZStencil));

			AfxInterop::OnSetSharedDepthStencilSurface(nullptr);

			return result;
		}
#endif

		return g_OldDirect3DDevice9->SetDepthStencilSurface(pNewZStencil);
	}

	STDMETHOD(GetDepthStencilSurface)(THIS_ IDirect3DSurface9** ppZStencilSurface)
	{
		HRESULT result = g_OldDirect3DDevice9->GetDepthStencilSurface(ppZStencilSurface);

		if (SUCCEEDED(result)) *ppZStencilSurface = UnwrapSurfaceReverse(*ppZStencilSurface, true);

		return result;
	}

	STDMETHOD(BeginScene)(THIS)
	{
		return g_OldDirect3DDevice9->BeginScene();
	}

    STDMETHOD(EndScene)(THIS)
	{
		Shared_Direct3DDevice9_EndScene();
		
		g_bD3D9DebugPrint = false;

		return g_OldDirect3DDevice9->EndScene();
	}

    STDMETHOD(Clear)(THIS_ DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil)
	{
		if(g_bD3D9DebugPrint)
		{
			Tier0_Msg("Clear:");
			Tier0_Msg(" Flags=");
			if(Flags & D3DCLEAR_STENCIL) Tier0_Msg("|D3DCLEAR_STENCIL");
			if(Flags & D3DCLEAR_TARGET) Tier0_Msg("|D3DCLEAR_TARGET");
			if(Flags & D3DCLEAR_ZBUFFER) Tier0_Msg("|D3DCLEAR_ZBUFFER");
			Tier0_Msg(" Color=0x%08x", Color);
			Tier0_Msg(" Z=%f",Z);
			Tier0_Msg("\n");
		}

		return g_OldDirect3DDevice9->Clear(Count, pRects, Flags, Color, Z, Stencil);
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, MultiplyTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetViewport);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetViewport);
    
	STDMETHOD(SetMaterial)(THIS_ CONST D3DMATERIAL9* pMaterial)
	{
		return g_OldDirect3DDevice9->SetMaterial(pMaterial);
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetMaterial);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetLight);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetLight);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, LightEnable);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetLightEnable);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetClipPlane);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetClipPlane);
    
    STDMETHOD(SetRenderState)(THIS_ D3DRENDERSTATETYPE State,DWORD Value)
	{
		if(g_bD3D9DebugPrint)
		{
			Tier0_Msg("SetRenderState: ");

			switch(State)
			{
			case D3DRS_ZWRITEENABLE:
				Tier0_Msg("D3DRS_ZWRITEENABLE: %s",
					Value & TRUE  ? "TRUE" : "false"
					);
				break;
			case D3DRS_ZFUNC:
				Tier0_Msg("D3DRS_ZFUNC: ");
				switch(Value)
				{
				case D3DCMP_NEVER:
					Tier0_Msg("D3DCMP_NEVER");
					break;
				case D3DCMP_LESS:
					Tier0_Msg("D3DCMP_LESS");
					break;
				case D3DCMP_EQUAL:
					Tier0_Msg("D3DCMP_EQUAL");
					break;
				case D3DCMP_LESSEQUAL:
					Tier0_Msg("D3DCMP_LESSEQUAL");
					break;
				case D3DCMP_GREATER:
					Tier0_Msg("D3DCMP_GREATER");
					break;
				case D3DCMP_NOTEQUAL:
					Tier0_Msg("D3DCMP_NOTEQUAL");
					break;
				case D3DCMP_GREATEREQUAL:
					Tier0_Msg("D3DCMP_GREATEREQUAL");
					break;
				case D3DCMP_ALWAYS:
					Tier0_Msg("D3DCMP_ALWAYS");
					break;
				default:
					Tier0_Msg("other");
				}
				break;
			case D3DRS_COLORWRITEENABLE:
				Tier0_Msg("D3DRS_COLORWRITEENABLE: R:%s G:%s B:%s A:%s",
					Value & D3DCOLORWRITEENABLE_RED ? "ON" : "off",
					Value & D3DCOLORWRITEENABLE_GREEN ? "ON" : "off",
					Value & D3DCOLORWRITEENABLE_BLUE ? "ON" : "off",
					Value & D3DCOLORWRITEENABLE_ALPHA ? "ON" : "off"
					);
				break;
			case D3DRS_MULTISAMPLEANTIALIAS:
				Tier0_Msg("D3DRS_MULTISAMPLEANTIALIAS: %s",Value & TRUE  ? "TRUE" : "false");
				break;
			case D3DRS_ANTIALIASEDLINEENABLE:
				Tier0_Msg("D3DRS_ANTIALIASEDLINEENABLE: %s",Value & TRUE  ? "TRUE" : "false");
				break;
			case D3DRS_POINTSIZE:
				Tier0_Msg("D3DRS_POINTSIZE: %f",*(float *)&Value);
				break;
			case D3DRS_POINTSIZE_MIN:
				Tier0_Msg("D3DRS_POINTSIZE_MIN: %f",*(float *)&Value);
				break;
			case D3DRS_POINTSIZE_MAX:
				Tier0_Msg("D3DRS_POINTSIZE_MAX: %f",*(float *)&Value);
				break;
			case D3DRS_SEPARATEALPHABLENDENABLE:
				Tier0_Msg("D3DRS_SEPARATEALPHABLENDENABLE: %i",Value);
				break;
			default:
				Tier0_Msg("other: %i: %i",State,Value);
			}

			Tier0_Msg("\n");

		}

		CAfxOverride & curOverride = m_OverrideStack.top();

		switch(State)
		{
		case D3DRS_SRCBLEND:
			m_D3DRS_SRCBLEND = Value;
			if(curOverride.m_Override_D3DRS_SRCBLEND)
				return D3D_OK;
			break;
		case D3DRS_DESTBLEND:
			m_D3DRS_DESTBLEND = Value;
			if(curOverride.m_Override_D3DRS_DESTBLEND)
				return D3D_OK;
			break;
		case D3DRS_SRGBWRITEENABLE:
			m_D3DRS_SRGBWRITEENABLE = Value;
			if(curOverride.m_Override_D3DRS_SRGBWRITEENABLE)
				return D3D_OK;
			break;
		case D3DRS_ZWRITEENABLE:
			m_D3DRS_ZWRITEENABLE = Value;
			if(curOverride.m_Override_D3DRS_ZWRITEENABLE)
				return D3D_OK;
			break;
		case D3DRS_ALPHABLENDENABLE:
			m_D3DRS_ALPHABLENDENABLE = Value;
			if(curOverride.m_Override_D3DRS_ALPHABLENDENABLE)
				return D3D_OK;
			break;
		}

		return g_OldDirect3DDevice9->SetRenderState(State, Value);;
	}

    STDMETHOD(GetRenderState)(THIS_ D3DRENDERSTATETYPE State,DWORD* pValue)
	{
		if(pValue)
		{
			CAfxOverride & curOverride = m_OverrideStack.top();

			switch(State)
			{
			case D3DRS_SRCBLEND:
				if(curOverride.m_Override_D3DRS_SRCBLEND)
				{
					*pValue = m_D3DRS_SRCBLEND;
					return D3D_OK;
				}
				break;
			case D3DRS_DESTBLEND:
				if(curOverride.m_Override_D3DRS_DESTBLEND)
				{
					*pValue = m_D3DRS_DESTBLEND;
					return D3D_OK;
				}
				break;
			case D3DRS_SRGBWRITEENABLE:
				if(curOverride.m_Override_D3DRS_SRGBWRITEENABLE)
				{
					*pValue = m_D3DRS_SRGBWRITEENABLE;
					return D3D_OK;
				}
				break;
			case D3DRS_ZWRITEENABLE:
				if(curOverride.m_Override_D3DRS_ZWRITEENABLE)
				{
					*pValue = m_D3DRS_ZWRITEENABLE;
					return D3D_OK;
				}
				break;
			case D3DRS_ALPHABLENDENABLE:
				{
					if(curOverride.m_Override_D3DRS_ALPHABLENDENABLE)
					{
						*pValue = m_D3DRS_ALPHABLENDENABLE;
						return D3D_OK;
					}
				}
				break;
			}
		}
			
		return g_OldDirect3DDevice9->GetRenderState(State, pValue);
	}

	STDMETHOD(CreateStateBlock)(THIS_ D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB)
	{
		HRESULT hResult = g_OldDirect3DDevice9->CreateStateBlock(Type, ppSB);

		if(SUCCEEDED(hResult) && ppSB && *ppSB)
		{
			*ppSB = new CAfxHookDirect3DStateBlock9(*ppSB);
		}

		return hResult;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, BeginStateBlock);
    
	STDMETHOD(EndStateBlock)(THIS_ IDirect3DStateBlock9** ppSB)
	{
		HRESULT hResult = g_OldDirect3DDevice9->EndStateBlock(ppSB);

		if(SUCCEEDED(hResult) && ppSB && *ppSB)
		{
			*ppSB = new CAfxHookDirect3DStateBlock9(*ppSB);
		}

		return hResult;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetClipStatus);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetClipStatus);
	
	STDMETHOD(GetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9** ppTexture)
	{
		HRESULT result = g_OldDirect3DDevice9->GetTexture(Stage, ppTexture);

		if (SUCCEEDED(result) && ppTexture)
		{
			*ppTexture = UnwrapTextureReverse(*ppTexture, true);
		}

		return result;
	}
	
	STDMETHOD(SetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9* pTexture)
	{
#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && pTexture)
		{
			pTexture = UnwrapTexture(pTexture);
		}
#endif

		return g_OldDirect3DDevice9->SetTexture(Stage, pTexture);
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetTextureStageState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetTextureStageState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetSamplerState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetSamplerState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, ValidateDevice);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetPaletteEntries);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetPaletteEntries);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetCurrentTexturePalette);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetCurrentTexturePalette);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetScissorRect);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetScissorRect);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetSoftwareVertexProcessing);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetSoftwareVertexProcessing);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetNPatchMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetNPatchMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawPrimitive);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawIndexedPrimitive);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawPrimitiveUP);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawIndexedPrimitiveUP);
	
	STDMETHOD(ProcessVertices)(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags)
	{
		return g_OldDirect3DDevice9->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, UnwrapVertexBuffer(pDestBuffer), pVertexDecl, Flags);
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, CreateVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetFVF);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetFVF);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, CreateVertexShader);  
    
    STDMETHOD(SetVertexShader)(THIS_ IDirect3DVertexShader9* pShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(g_bD3D9DumpVertexShader)
		{
			g_bD3D9DumpVertexShader = false;

			if(pShader)
			{
				UINT size;
				if(SUCCEEDED(pShader->GetFunction(0, &size)))
				{
					void * pData = malloc(size);

					if(pData && SUCCEEDED(pShader->GetFunction(pData,&size)))
					{
						FILE * f1 = fopen("AfxVertexShaderDump.fxo","wb");
						if(f1)
						{
							fwrite(pData,size,1,f1);
							fclose(f1);
						}
					}

					free(pData);
				}
			}
		}
		
		if(m_Original_VertexShader) m_Original_VertexShader->Release();
		m_Original_VertexShader = pShader;
		if(pShader) pShader->AddRef();

		return !curOverride.m_Override_VertexShader ? g_OldDirect3DDevice9->SetVertexShader(pShader) : D3D_OK;
	}

    STDMETHOD(GetVertexShader)(THIS_ IDirect3DVertexShader9** ppShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_VertexShader && ppShader)
		{
			*ppShader = m_Original_VertexShader;
			return D3D_OK;
		}

		return g_OldDirect3DDevice9->GetVertexShader(ppShader);
	}
	
    STDMETHOD(SetVertexShaderConstantF)(THIS_ UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
	{
		if(g_bD3D9DebugPrint)
		{
			int lo = StartRegister;
			int hi = StartRegister+Vector4fCount;
			bool inRange = lo <= 8 && 8 < hi || lo <= 9 && 9 < hi || lo <= 10 && 10 < hi || lo <= 11 && 11 < hi;

			if(inRange)
			{
				int min = lo;
				if(8>min) min = 8;

				int maxC = hi;
				if(12<maxC) maxC = 12;

				Tier0_Msg("SetVertexShaderConstantF:\n");
				for(int i=min; i<maxC; i++)
				{
					int idx = i - StartRegister;
					Tier0_Msg("\t%i: %f %f %f %f\n", i, pConstantData[4*idx+0], pConstantData[4*idx+1], pConstantData[4*idx+2], pConstantData[4*idx+3]);
				}
			}
		}

		return g_OldDirect3DDevice9->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
	}
	
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetVertexShaderConstantF);
	
    STDMETHOD(SetVertexShaderConstantI)(THIS_ UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount)
	{
		return g_OldDirect3DDevice9->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
	}
	
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetVertexShaderConstantI);
	
    STDMETHOD(SetVertexShaderConstantB)(THIS_ UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount)
	{
		return g_OldDirect3DDevice9->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
	}
	
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetVertexShaderConstantB);
    
	STDMETHOD(SetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
	{
		return g_OldDirect3DDevice9->SetStreamSource(StreamNumber, UnwrapVertexBuffer(pStreamData), OffsetInBytes, Stride);
	}

	STDMETHOD(GetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride)
	{
		HRESULT result = g_OldDirect3DDevice9->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);

		if (ppStreamData) *ppStreamData = UnwrapVertexBufferReverse(*ppStreamData, true);

		return result;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, SetStreamSourceFreq);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetStreamSourceFreq);
    
	STDMETHOD(SetIndices)(THIS_ IDirect3DIndexBuffer9* pIndexData)
	{
		return g_OldDirect3DDevice9->SetIndices(UnwrapIndexBuffer(pIndexData));
	}

	STDMETHOD(GetIndices)(THIS_ IDirect3DIndexBuffer9** ppIndexData)
	{
		HRESULT result = g_OldDirect3DDevice9->GetIndices(ppIndexData);

		if (ppIndexData) *ppIndexData = UnwrapIndexBufferReverse(*ppIndexData, true);

		return result;
	}


    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, CreatePixelShader);
    
    STDMETHOD(SetPixelShader)(THIS_ IDirect3DPixelShader9* pShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(g_bD3D9DumpPixelShader)
		{
			g_bD3D9DumpPixelShader = false;

			if(pShader)
			{
				UINT size;
				if(SUCCEEDED(pShader->GetFunction(0, &size)))
				{
					void * pData = malloc(size);

					if(pData && SUCCEEDED(pShader->GetFunction(pData,&size)))
					{
						FILE * f1 = fopen("AfxPixelShaderDump.fxo","wb");
						if(f1)
						{
							fwrite(pData,size,1,f1);
							fclose(f1);
						}
					}

					free(pData);
				}
			}
		}

		if(m_Original_PixelShader) m_Original_PixelShader->Release();
		m_Original_PixelShader = pShader;
		if(pShader) pShader->AddRef();

		return !curOverride.m_Override_PixelShader ? g_OldDirect3DDevice9->SetPixelShader(pShader) : D3D_OK;
	}
    
    STDMETHOD(GetPixelShader)(THIS_ IDirect3DPixelShader9** ppShader)
	{
		CAfxOverride & curOverride = m_OverrideStack.top();

		if(curOverride.m_Override_PixelShader && ppShader)
		{
			*ppShader = m_Original_PixelShader;
			return D3D_OK;
		}

		return g_OldDirect3DDevice9->GetPixelShader(ppShader);
	}
	
    STDMETHOD(SetPixelShaderConstantF)(THIS_ UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
	{
		HRESULT result = g_OldDirect3DDevice9->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);

		if(pConstantData)
		{
			CAfxOverride & curOverride = m_OverrideStack.top();

			if(StartRegister <= 0 && 0 < StartRegister+Vector4fCount)
			{
				m_OriginalValue_ps_c0[0] = pConstantData[4*(0 -StartRegister)+0];
				m_OriginalValue_ps_c0[1] = pConstantData[4*(0 -StartRegister)+1];
				m_OriginalValue_ps_c0[2] = pConstantData[4*(0 -StartRegister)+2];
				m_OriginalValue_ps_c0[3] = pConstantData[4*(0 -StartRegister)+3];

				if(curOverride.m_Override_ps_c0)
					g_OldDirect3DDevice9->SetPixelShaderConstantF(0, curOverride.m_OverrideValue_ps_c0, 1);
			}

			if(StartRegister <= 5 && 5 < StartRegister+Vector4fCount)
			{
				m_OriginalValue_ps_c5[0] = pConstantData[4*(5 -StartRegister)+0];
				m_OriginalValue_ps_c5[1] = pConstantData[4*(5 -StartRegister)+1];
				m_OriginalValue_ps_c5[2] = pConstantData[4*(5 -StartRegister)+2];
				m_OriginalValue_ps_c5[3] = pConstantData[4*(5 -StartRegister)+3];

				if(curOverride.m_Override_ps_c5)
					g_OldDirect3DDevice9->SetPixelShaderConstantF(5, curOverride.m_OverrideValue_ps_c5, 1);
			}

			if(StartRegister <= 12 && 12 < StartRegister+Vector4fCount)
			{
				m_OriginalValue_ps_c12[0] = pConstantData[4*(12 -StartRegister)+0];
				m_OriginalValue_ps_c12[1] = pConstantData[4*(12 -StartRegister)+1];
				m_OriginalValue_ps_c12[2] = pConstantData[4*(12 -StartRegister)+2];
				m_OriginalValue_ps_c12[3] = pConstantData[4*(12 -StartRegister)+3];

				if(curOverride.m_Override_ps_c12_y)
				{
					float tmp[4] = { m_OriginalValue_ps_c12[0], curOverride.m_OverrideValue_ps_c12_y, m_OriginalValue_ps_c12[2], m_OriginalValue_ps_c12[3] };
					g_OldDirect3DDevice9->SetPixelShaderConstantF(12, tmp, 1);
				}
			}

			if(StartRegister <= 29 && 29 < StartRegister+Vector4fCount)
			{
				m_OriginalValue_ps_c29[0] = pConstantData[4*(29 -StartRegister)+0];
				m_OriginalValue_ps_c29[1] = pConstantData[4*(29 -StartRegister)+1];
				m_OriginalValue_ps_c29[2] = pConstantData[4*(29 -StartRegister)+2];
				m_OriginalValue_ps_c29[3] = pConstantData[4*(29 -StartRegister)+3];

				if(curOverride.m_Override_ps_c29_w)
				{
					float tmp[4] = { m_OriginalValue_ps_c29[0], m_OriginalValue_ps_c29[1], m_OriginalValue_ps_c29[2], curOverride.m_OverrideValue_ps_c29_w };
					g_OldDirect3DDevice9->SetPixelShaderConstantF(29, tmp, 1);
				}
			}

			if(StartRegister <= 31 && 31 < StartRegister+Vector4fCount)
			{
				m_OriginalValue_ps_c31[0] = pConstantData[4*(31 -StartRegister)+0];
				m_OriginalValue_ps_c31[1] = pConstantData[4*(31 -StartRegister)+1];
				m_OriginalValue_ps_c31[2] = pConstantData[4*(31 -StartRegister)+2];
				m_OriginalValue_ps_c31[3] = pConstantData[4*(31 -StartRegister)+3];

				if(curOverride.m_Override_ps_c31)
					g_OldDirect3DDevice9->SetPixelShaderConstantF(31, curOverride.m_OverrideValue_ps_c31, 1);
			}

			if (StartRegister <= 1 && 1 < StartRegister + Vector4fCount)
			{
				m_OriginalValue_ps_c1[0] = pConstantData[4 * (1 - StartRegister) + 0];
				m_OriginalValue_ps_c1[1] = pConstantData[4 * (1 - StartRegister) + 1];
				m_OriginalValue_ps_c1[2] = pConstantData[4 * (1 - StartRegister) + 2];
				m_OriginalValue_ps_c1[3] = pConstantData[4 * (1 - StartRegister) + 3];

				if (curOverride.m_Override_ps_c1_xyz || curOverride.m_Override_ps_c1_w)
				{
					float tmp[4] = { curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[0] : m_OriginalValue_ps_c1[0], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[1] : m_OriginalValue_ps_c1[1], curOverride.m_Override_ps_c1_xyz ? curOverride.m_OverrideValue_ps_c1_xyz[2] : m_OriginalValue_ps_c1[2], curOverride.m_Override_ps_c1_w ? curOverride.m_OverrideValue_ps_c1_w : m_OriginalValue_ps_c1[3] };
					g_OldDirect3DDevice9->SetPixelShaderConstantF(1, tmp, 1);
				}
			}
		}

		return result;
	}

    STDMETHOD(GetPixelShaderConstantF)(THIS_ UINT StartRegister,float* pConstantData,UINT Vector4fCount)
	{
		HRESULT result = g_OldDirect3DDevice9->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);

		if(pConstantData)
		{
			if(StartRegister <= 0 && 0 < StartRegister+Vector4fCount)
			{
				pConstantData[4*(0 -StartRegister)+0] = m_OriginalValue_ps_c0[0];
				pConstantData[4*(0 -StartRegister)+1] = m_OriginalValue_ps_c0[1];
				pConstantData[4*(0 -StartRegister)+2] = m_OriginalValue_ps_c0[2];
				pConstantData[4*(0 -StartRegister)+3] = m_OriginalValue_ps_c0[3];
			}

			if(StartRegister <= 5 && 5 < StartRegister+Vector4fCount)
			{
				pConstantData[4*(5 -StartRegister)+0] = m_OriginalValue_ps_c5[0];
				pConstantData[4*(5 -StartRegister)+1] = m_OriginalValue_ps_c5[1];
				pConstantData[4*(5 -StartRegister)+2] = m_OriginalValue_ps_c5[2];
				pConstantData[4*(5 -StartRegister)+3] = m_OriginalValue_ps_c5[3];
			}

			if(StartRegister <= 12 && 12 < StartRegister+Vector4fCount)
			{
				pConstantData[4*(12 -StartRegister)+0] = m_OriginalValue_ps_c12[0];
				pConstantData[4*(12 -StartRegister)+1] = m_OriginalValue_ps_c12[1];
				pConstantData[4*(12 -StartRegister)+2] = m_OriginalValue_ps_c12[2];
				pConstantData[4*(12 -StartRegister)+3] = m_OriginalValue_ps_c12[3];
			}

			if(StartRegister <= 29 && 29 < StartRegister+Vector4fCount)
			{
				pConstantData[4*(29 -StartRegister)+0] = m_OriginalValue_ps_c29[0];
				pConstantData[4*(29 -StartRegister)+1] = m_OriginalValue_ps_c29[1];
				pConstantData[4*(29 -StartRegister)+2] = m_OriginalValue_ps_c29[2];
				pConstantData[4*(29 -StartRegister)+3] = m_OriginalValue_ps_c29[3];
			}

			if (StartRegister <= 31 && 31 < StartRegister + Vector4fCount)
			{
				pConstantData[4 * (31 - StartRegister) + 0] = m_OriginalValue_ps_c31[0];
				pConstantData[4 * (31 - StartRegister) + 1] = m_OriginalValue_ps_c31[1];
				pConstantData[4 * (31 - StartRegister) + 2] = m_OriginalValue_ps_c31[2];
				pConstantData[4 * (31 - StartRegister) + 3] = m_OriginalValue_ps_c31[3];
			}

			if (StartRegister <= 1 && 1 < StartRegister + Vector4fCount)
			{
				pConstantData[4 * (1 - StartRegister) + 0] = m_OriginalValue_ps_c1[0];
				pConstantData[4 * (1 - StartRegister) + 1] = m_OriginalValue_ps_c1[1];
				pConstantData[4 * (1 - StartRegister) + 2] = m_OriginalValue_ps_c1[2];
				pConstantData[4 * (1 - StartRegister) + 3] = m_OriginalValue_ps_c1[3];
			}
		}
		
		return result;
	}

    STDMETHOD(SetPixelShaderConstantI)(THIS_ UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount)
	{
		return g_OldDirect3DDevice9->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
	}
	
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetPixelShaderConstantI);
	
    STDMETHOD(SetPixelShaderConstantB)(THIS_ UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount)
	{
		return g_OldDirect3DDevice9->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
	}
	
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, GetPixelShaderConstantB);
    
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawRectPatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DrawTriPatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, DeletePatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9, CreateQuery);
} g_NewDirect3DDevice9;

IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, TestCooperativeLevel, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetAvailableTextureMem, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, EvictManagedResources, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetDirect3D, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetDeviceCaps, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetDisplayMode, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetCreationParameters, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetCursorPosition, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, ShowCursor, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, CreateAdditionalSwapChain, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetSwapChain, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetNumberOfSwapChains, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetRasterStatus, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetDialogBoxMode, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetGammaRamp, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetGammaRamp, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetTransform, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetTransform, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, MultiplyTransform, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetViewport, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetViewport, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetMaterial, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetLight, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetLight, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, LightEnable, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetLightEnable, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetClipPlane, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetClipPlane, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, BeginStateBlock, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetClipStatus, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetClipStatus, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetTextureStageState, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetTextureStageState, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetSamplerState, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetSamplerState, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, ValidateDevice, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetPaletteEntries, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetPaletteEntries, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetCurrentTexturePalette, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetCurrentTexturePalette, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetScissorRect, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetScissorRect, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetSoftwareVertexProcessing, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetSoftwareVertexProcessing, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetNPatchMode, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetNPatchMode, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawPrimitive, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawIndexedPrimitive, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawPrimitiveUP, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawIndexedPrimitiveUP, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, CreateVertexDeclaration, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetVertexDeclaration, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetVertexDeclaration, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetFVF, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetFVF, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, CreateVertexShader, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetVertexShaderConstantF, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetVertexShaderConstantI, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetVertexShaderConstantB, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, SetStreamSourceFreq, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetStreamSourceFreq, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, CreatePixelShader, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetPixelShaderConstantI, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, GetPixelShaderConstantB, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawRectPatch, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DrawTriPatch, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, DeletePatch, NewDirect3DDevice9, g_OldDirect3DDevice9);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9, CreateQuery, NewDirect3DDevice9, g_OldDirect3DDevice9);


COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE CAfxHookDirect3DStateBlock9::Apply(THIS)
{
	HRESULT hResult = m_Parent->Apply();

	g_NewDirect3DDevice9.On_AfxHookDirect3DStateBlock9_Applied();

	return hResult;
}

struct NewDirect3DDevice9Ex
{
public:
    /*** IUnknown methods ***/
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, QueryInterface);

	STDMETHOD_(ULONG,AddRef)(THIS)
	{
		ULONG result = g_OldDirect3DDevice9Ex->AddRef();

		++g_NewDirect3DDevice9Ex_RefCount;

		return result;
	}

    STDMETHOD_(ULONG,Release)(THIS)
	{
		--g_NewDirect3DDevice9Ex_RefCount;

		if(0 == g_NewDirect3DDevice9Ex_RefCount)
		{
			Shared_Direct3DDevice9_Shutdown();
		}

		return g_OldDirect3DDevice9Ex->Release();
	}

    /*** IDirect3DDevice9Ex methods ***/
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, TestCooperativeLevel);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetAvailableTextureMem);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, EvictManagedResources);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetDirect3D);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetDeviceCaps);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetDisplayMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetCreationParameters);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetCursorProperties);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetCursorPosition);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, ShowCursor);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateAdditionalSwapChain);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetSwapChain);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetNumberOfSwapChains);
    
	STDMETHOD(Reset)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters)
	{
		Shared_Direct3DDevice9_Reset_Before();

		HRESULT hResult = g_OldDirect3DDevice9Ex->Reset(pPresentationParameters);

		Shared_Direct3DDevice9_Reset_After();

		return hResult;
	}

	STDMETHOD(Present)(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
	{
		HRESULT hResult = g_OldDirect3DDevice9Ex->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

		Shared_Direct3DDevice9_Present(D3DERR_DEVICELOST == hResult, false);

		return hResult;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetBackBuffer);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetRasterStatus);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetDialogBoxMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetGammaRamp);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetGammaRamp);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateVolumeTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateCubeTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateVertexBuffer);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateIndexBuffer);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateRenderTarget);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateDepthStencilSurface);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, UpdateSurface);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, UpdateTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetRenderTargetData);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetFrontBufferData);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, StretchRect);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, ColorFill);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateOffscreenPlainSurface);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetRenderTarget);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetRenderTarget);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetDepthStencilSurface);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetDepthStencilSurface);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, BeginScene);
    
	STDMETHOD(EndScene)(THIS)
	{
		Shared_Direct3DDevice9_EndScene();

		return g_OldDirect3DDevice9Ex->EndScene();
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, Clear);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, MultiplyTransform);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetViewport);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetViewport);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetMaterial);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetMaterial);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetLight);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetLight);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, LightEnable);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetLightEnable);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetClipPlane);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetClipPlane);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetRenderState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetRenderState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateStateBlock);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, BeginStateBlock);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, EndStateBlock);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetClipStatus);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetClipStatus);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetTexture);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetTextureStageState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetTextureStageState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetSamplerState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetSamplerState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, ValidateDevice);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetPaletteEntries);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetPaletteEntries);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetCurrentTexturePalette);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetCurrentTexturePalette);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetScissorRect);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetScissorRect);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetSoftwareVertexProcessing);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetSoftwareVertexProcessing);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetNPatchMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetNPatchMode);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawPrimitive);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawIndexedPrimitive);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawPrimitiveUP);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawIndexedPrimitiveUP);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, ProcessVertices);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetVertexDeclaration);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetFVF);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetFVF);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateVertexShader);  
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetVertexShader);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetVertexShader);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetVertexShaderConstantF);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetVertexShaderConstantF);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetVertexShaderConstantI);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetVertexShaderConstantI);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetVertexShaderConstantB);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetVertexShaderConstantB);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetStreamSource);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetStreamSource);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetStreamSourceFreq);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetStreamSourceFreq);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetIndices);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetIndices);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreatePixelShader);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetPixelShader);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetPixelShader);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetPixelShaderConstantF);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetPixelShaderConstantF);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetPixelShaderConstantI);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetPixelShaderConstantI);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetPixelShaderConstantB);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetPixelShaderConstantB);
	IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawRectPatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DrawTriPatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, DeletePatch);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateQuery);

    /*** IDirect3DDevice9Ex methods ***/
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetConvolutionMonoKernel);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, ComposeRects);
    
	STDMETHOD(PresentEx)(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
	{
		HRESULT hResult = g_OldDirect3DDevice9Ex->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);

		Shared_Direct3DDevice9_Present(D3DERR_DEVICELOST == hResult, false);

		return hResult;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetGPUThreadPriority);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetGPUThreadPriority);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, WaitForVBlank);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CheckResourceResidency);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, SetMaximumFrameLatency);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetMaximumFrameLatency);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CheckDeviceState);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateRenderTargetEx);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateOffscreenPlainSurfaceEx);
    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, CreateDepthStencilSurfaceEx);
    
	STDMETHOD(ResetEx)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters,D3DDISPLAYMODEEX *pFullscreenDisplayMode)
	{
		Shared_Direct3DDevice9_Reset_Before();

		HRESULT hResult = g_OldDirect3DDevice9Ex->ResetEx(pPresentationParameters, pFullscreenDisplayMode);

		Shared_Direct3DDevice9_Reset_After();

		return hResult;
	}

    IFACE_PASSTHROUGH_DECL(IDirect3DDevice9Ex, GetDisplayModeEx);
} g_NewDirect3DDevice9Ex;

IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, QueryInterface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, TestCooperativeLevel, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetAvailableTextureMem, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, EvictManagedResources, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetDirect3D, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetDeviceCaps, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetDisplayMode, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetCreationParameters, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetCursorProperties, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetCursorPosition, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, ShowCursor, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateAdditionalSwapChain, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetSwapChain, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetNumberOfSwapChains, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetBackBuffer, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetRasterStatus, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetDialogBoxMode, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetGammaRamp, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetGammaRamp, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateVolumeTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateCubeTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateVertexBuffer, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateIndexBuffer, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateRenderTarget, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateDepthStencilSurface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, UpdateSurface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, UpdateTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetRenderTargetData, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetFrontBufferData, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, StretchRect, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, ColorFill, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateOffscreenPlainSurface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetRenderTarget, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetRenderTarget, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetDepthStencilSurface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetDepthStencilSurface, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, BeginScene, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, Clear, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetTransform, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetTransform, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, MultiplyTransform, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetViewport, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetViewport, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetMaterial, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetMaterial, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetLight, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetLight, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, LightEnable, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetLightEnable, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetClipPlane, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetClipPlane, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetRenderState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetRenderState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateStateBlock, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, BeginStateBlock, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, EndStateBlock, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetClipStatus, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetClipStatus, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetTexture, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetTextureStageState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetTextureStageState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetSamplerState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetSamplerState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, ValidateDevice, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetPaletteEntries, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetPaletteEntries, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetCurrentTexturePalette, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetCurrentTexturePalette, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetScissorRect, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetScissorRect, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetSoftwareVertexProcessing, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetSoftwareVertexProcessing, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetNPatchMode, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetNPatchMode, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawPrimitive, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawIndexedPrimitive, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawPrimitiveUP, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawIndexedPrimitiveUP, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, ProcessVertices, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateVertexDeclaration, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetVertexDeclaration, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetVertexDeclaration, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetFVF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetFVF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateVertexShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetVertexShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetVertexShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetVertexShaderConstantF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetVertexShaderConstantF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetVertexShaderConstantI, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetVertexShaderConstantI, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetVertexShaderConstantB, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetVertexShaderConstantB, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetStreamSource, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetStreamSource, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetStreamSourceFreq, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetStreamSourceFreq, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetIndices, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetIndices, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreatePixelShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetPixelShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetPixelShader, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetPixelShaderConstantF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetPixelShaderConstantF, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetPixelShaderConstantI, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetPixelShaderConstantI, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetPixelShaderConstantB, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetPixelShaderConstantB, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawRectPatch, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DrawTriPatch, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, DeletePatch, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateQuery, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetConvolutionMonoKernel, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, ComposeRects, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetGPUThreadPriority, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetGPUThreadPriority, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, WaitForVBlank, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CheckResourceResidency, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, SetMaximumFrameLatency, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetMaximumFrameLatency, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CheckDeviceState, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateRenderTargetEx, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateOffscreenPlainSurfaceEx, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, CreateDepthStencilSurfaceEx, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3DDevice9Ex, GetDisplayModeEx, NewDirect3DDevice9Ex, g_OldDirect3DDevice9Ex);


IDirect3D9 * g_OldDirect3D9;
IDirect3D9Ex * g_OldDirect3D9Ex = nullptr;

struct NewDirect3D9
{
    /*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) {

		if (riid == __uuidof(IDirect3D9Ex))
		{
			if (ppvObj) ppvObj = NULL;
			return E_NOINTERFACE; // If we shoved in D3D9Ex, make sure CS:GO does not notice it.
		}

		return g_OldDirect3D9->QueryInterface(riid, ppvObj);
	}

	IFACE_PASSTHROUGH_DECL(IDirect3D9, AddRef);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, Release);

    /*** IDirect3D9 methods ***/
	IFACE_PASSTHROUGH_DECL(IDirect3D9, RegisterSoftwareDevice);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetAdapterCount);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetAdapterIdentifier);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetAdapterModeCount);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, EnumAdapterModes);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetAdapterDisplayMode);

	STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			switch (BackBufferFormat)
			{
			//case D3DFMT_D16_LOCKABLE:
			case D3DFMT_D32:
			case D3DFMT_D15S1:
			case D3DFMT_D24S8:
			case D3DFMT_D24X8:
			case D3DFMT_D24X4S4:
			//case D3DFMT_D16:
			//case D3DFMT_D32F_LOCKABLE:
			case D3DFMT_D24FS8:
			case D3DFMT_D32_LOCKABLE:
			case D3DFMT_S8_LOCKABLE:
				return D3DERR_NOTAVAILABLE;
			}
			
		}
#endif

		return g_OldDirect3D9->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
	}

	IFACE_PASSTHROUGH_DECL(IDirect3D9, CheckDeviceFormat);
	
	STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			pQualityLevels = 0;

			if (D3DMULTISAMPLE_NONE != MultiSampleType)
				return D3DERR_NOTAVAILABLE;
		}
#endif

		return g_OldDirect3D9->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
	}

	IFACE_PASSTHROUGH_DECL(IDirect3D9, CheckDepthStencilMatch);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, CheckDeviceFormatConversion);
	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetDeviceCaps);

	IFACE_PASSTHROUGH_DECL(IDirect3D9, GetAdapterMonitor);

	STDMETHOD(CreateDevice)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface)
	{
		HRESULT hRet = D3DERR_NOTAVAILABLE;

#ifdef AFX_INTEROP
		if (AfxInterop::Enabled() && g_OldDirect3D9Ex && pPresentationParameters)
		{
			DeviceType = D3DDEVTYPE_HAL;
			BehaviorFlags = BehaviorFlags & ~(DWORD)(D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_SOFTWARE_VERTEXPROCESSING) | D3DCREATE_HARDWARE_VERTEXPROCESSING;

			FixPresentationparementers(pPresentationParameters);

			hRet = g_OldDirect3D9Ex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, nullptr, ppReturnedDeviceInterface != nullptr ? &g_OldDirect3DDevice9Ex : nullptr);

			if (SUCCEEDED(hRet) && pPresentationParameters && ppReturnedDeviceInterface)
			{
				g_OldDirect3DDevice9 = g_OldDirect3DDevice9Ex;

				*ppReturnedDeviceInterface = reinterpret_cast<IDirect3DDevice9 *>(&g_NewDirect3DDevice9);

				g_NewDirect3DDevice9.Init(pPresentationParameters);

				//

				Shared_Direct3DDevice9_Init(Adapter, pPresentationParameters->hDeviceWindow, g_OldDirect3DDevice9);
			}
			else
				return D3DERR_NOTAVAILABLE;
		}
		else
		{
#endif
			hRet = g_OldDirect3D9->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

			if (SUCCEEDED(hRet) && pPresentationParameters && ppReturnedDeviceInterface)
			{
				g_OldDirect3DDevice9 = *ppReturnedDeviceInterface;

				Shared_Direct3DDevice9_Init(Adapter, pPresentationParameters->hDeviceWindow, g_OldDirect3DDevice9);

				*ppReturnedDeviceInterface = reinterpret_cast<IDirect3DDevice9 *>(&g_NewDirect3DDevice9);
			}
#ifdef AFX_INTEROP
		}
#endif

		return hRet;
	}

} g_NewDirect3D9;

IFACE_PASSTHROUGH_DEF(IDirect3D9, AddRef, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, Release, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, RegisterSoftwareDevice, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetAdapterCount, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetAdapterIdentifier, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetAdapterModeCount, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, EnumAdapterModes, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetAdapterDisplayMode, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, CheckDeviceFormat, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, CheckDepthStencilMatch, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, CheckDeviceFormatConversion, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetDeviceCaps, NewDirect3D9, g_OldDirect3D9);
IFACE_PASSTHROUGH_DEF(IDirect3D9, GetAdapterMonitor, NewDirect3D9, g_OldDirect3D9);

struct NewDirect3D9Ex
{
    /*** IUnknown methods ***/
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, QueryInterface);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, AddRef);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, Release);

    /*** IDirect3D9 methods ***/
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, RegisterSoftwareDevice);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterCount);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterIdentifier);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterModeCount);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, EnumAdapterModes);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterDisplayMode);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, CheckDeviceType);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, CheckDeviceFormat);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, CheckDeviceMultiSampleType);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, CheckDepthStencilMatch);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, CheckDeviceFormatConversion);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetDeviceCaps);
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterMonitor);

    STDMETHOD(CreateDevice)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface)
	{
		HRESULT hRet = g_OldDirect3D9Ex->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

		if (SUCCEEDED(hRet) && pPresentationParameters && ppReturnedDeviceInterface)
		{
			g_OldDirect3DDevice9 = *ppReturnedDeviceInterface;

			Shared_Direct3DDevice9_Init(Adapter, pPresentationParameters->hDeviceWindow, g_OldDirect3DDevice9);

			*ppReturnedDeviceInterface = reinterpret_cast<IDirect3DDevice9 *>(&g_NewDirect3DDevice9);
		}
		
		return hRet;
	}

    /*** IDirect3D9Ex methods ***/
    IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterModeCountEx);
    IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, EnumAdapterModesEx);
    IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterDisplayModeEx);
    
	STDMETHOD(CreateDeviceEx)(THIS_ UINT Adapter,D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,D3DDISPLAYMODEEX* pFullscreenDisplayMode,IDirect3DDevice9Ex** ppReturnedDeviceInterface)
	{
		HRESULT hRet = g_OldDirect3D9Ex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, ppReturnedDeviceInterface);


		if (SUCCEEDED(hRet) && pPresentationParameters && ppReturnedDeviceInterface)
		{
			g_OldDirect3DDevice9Ex = *ppReturnedDeviceInterface;

			Shared_Direct3DDevice9_Init(Adapter, pPresentationParameters->hDeviceWindow, g_OldDirect3DDevice9Ex);

			*ppReturnedDeviceInterface = reinterpret_cast<IDirect3DDevice9Ex *>(&g_NewDirect3DDevice9Ex);
		}

		return hRet;
	}
    
	IFACE_PASSTHROUGH_DECL(IDirect3D9Ex, GetAdapterLUID);
} g_NewDirect3D9Ex;

IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, QueryInterface, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, AddRef, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, Release, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, RegisterSoftwareDevice, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterCount, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterIdentifier, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterModeCount, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, EnumAdapterModes, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterDisplayMode, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, CheckDeviceType, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, CheckDeviceFormat, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, CheckDeviceMultiSampleType, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, CheckDepthStencilMatch, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, CheckDeviceFormatConversion, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetDeviceCaps, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterMonitor, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterModeCountEx, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, EnumAdapterModesEx, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterDisplayModeEx, NewDirect3D9Ex, g_OldDirect3D9Ex);
IFACE_PASSTHROUGH_DEF(IDirect3D9Ex, GetAdapterLUID, NewDirect3D9Ex, g_OldDirect3D9Ex);

Direct3DCreate9_t old_Direct3DCreate9 = 0;
Direct3DCreate9Ex_t old_Direct3DCreate9Ex = 0;

IDirect3D9 * WINAPI new_Direct3DCreate9(UINT SDKVersion)
{
	if(D3D_SDK_VERSION == SDKVersion)
	{
#if AFX_INTEROP
		if (AfxInterop::Enabled())
		{
			IDirect3D9Ex * device = NULL;

			if (!old_Direct3DCreate9Ex)
			{
				HMODULE hD3D9Dll = GetModuleHandle("d3d9.dll");
				old_Direct3DCreate9Ex = (Direct3DCreate9Ex_t)GetProcAddress(hD3D9Dll, "Direct3DCreate9Ex");
			}

			old_Direct3DCreate9Ex(SDKVersion, &device);
			g_OldDirect3D9Ex = device;
			g_OldDirect3D9 = g_OldDirect3D9Ex;
		}
		else
		{
			g_OldDirect3D9 = old_Direct3DCreate9(SDKVersion);
		}
#else
		g_OldDirect3D9 = old_Direct3DCreate9(SDKVersion);
#endif

		return reinterpret_cast<IDirect3D9 *>(&g_NewDirect3D9);
	}

	return old_Direct3DCreate9(SDKVersion);
}

HRESULT WINAPI new_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3DDevice)
{
	if(D3D_SDK_VERSION == SDKVersion)
	{
		HRESULT hResult = old_Direct3DCreate9Ex(SDKVersion, &g_OldDirect3D9Ex);

		if(ppD3DDevice) *ppD3DDevice = 0 != g_OldDirect3D9Ex ? reinterpret_cast<IDirect3D9Ex *>(&g_NewDirect3D9Ex) : 0;

		return hResult;
	}

	return old_Direct3DCreate9Ex(SDKVersion, ppD3DDevice);
}

bool AfxD3D9_Check_Supports_R32F_With_Blending(void)
{
	if(g_OldDirect3D9 && g_OldDirect3DDevice9)
	{
		if((g_OldDirect3D9->CheckDeviceFormat(
			g_Adapter,
			D3DDEVTYPE_HAL,
			D3DFMT_R8G8B8,
			D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
			D3DRTYPE_SURFACE,
			D3DFMT_R32F
		)))
			return true;
	}

	return false;
}


void AfxD3D9PushOverrideState(void)
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.AfxPushOverrideState();

}

void AfxD3D9PopOverrideState(void)
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.AfxPopOverrideState();
}

void AfxD3D9OverrideBegin_ModulationColor(float const color[3])
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c1_xyz(color);
}

void AfxD3D9OverrideEnd_ModulationColor(void)
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c1_xyz();
}

void AfxD3D9OverrideBegin_ModulationBlend(float value)
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c1_w(value);

}

void AfxD3D9OverrideEnd_ModulationBlend(void)
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c1_w();
}

void AfxD3D9OverrideBegin_D3DRS_SRCBLEND(DWORD value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_D3DRS_SRCBLEND(value);
}

void AfxD3D9OverrideEnd_D3DRS_SRCBLEND(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_D3DRS_SRCBLEND();
}

void AfxD3D9OverrideBegin_D3DRS_DESTBLEND(DWORD value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_D3DRS_DESTBLEND(value);
}

void AfxD3D9OverrideEnd_D3DRS_DESTBLEND(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_D3DRS_DESTBLEND();
}

void AfxD3D9OverrideBegin_D3DRS_SRGBWRITEENABLE(DWORD value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_D3DRS_SRGBWRITEENABLE(value);
}

void AfxD3D9OverrideEnd_D3DRS_SRGBWRITEENABLE(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_D3DRS_SRGBWRITEENABLE();
}

void AfxD3D9OverrideBegin_D3DRS_ZWRITEENABLE(DWORD value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_D3DRS_ZWRITEENABLE(value);
}

void AfxD3D9OverrideEnd_D3DRS_ZWRITEENABLE(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_D3DRS_ZWRITEENABLE();
}

void AfxD3D9OverrideBegin_D3DRS_ALPHABLENDENABLE(DWORD value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_D3DRS_ALPHABLENDENABLE(value);
}

void AfxD3D9OverrideEnd_D3DRS_ALPHABLENDENABLE(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_D3DRS_ALPHABLENDENABLE();
}

void AfxD3D9_OverrideBegin_ps_c0(float const values[4])
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c0(values);
}

void AfxD3D9_OverrideEnd_ps_c0(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c0();
}

void AfxD3D9_OverrideBegin_ps_c5(float const values[4])
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c5(values);
}

void AfxD3D9_OverrideEnd_ps_c5(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c5();
}

void AfxD3D9_OverrideBegin_ps_c12_y(float value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c12_y(value);
}

void AfxD3D9_OverrideEnd_ps_c12_y(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c12_y();
}

void AfxD3D9_OverrideBegin_ps_c29_w(float value)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c29_w(value);
}

void AfxD3D9_OverrideEnd_ps_c29_w(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c29_w();
}

void AfxD3D9_OverrideBegin_ps_c31(float const values[4])
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_ps_c31(values);
}

void AfxD3D9_OverrideEnd_ps_c31(void)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_ps_c31();
}

void AfxD3D9_OverrideBegin_SetVertexShader(IDirect3DVertexShader9 * overrideShader)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_SetVertexShader(overrideShader);
}

void AfxD3D9_OverrideEnd_SetVertexShader()
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_SetVertexShader();
}

void AfxD3D9_OverrideBegin_SetPixelShader(IDirect3DPixelShader9 * overrideShader)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideBegin_SetPixelShader(overrideShader);
}

void AfxD3D9_OverrideEnd_SetPixelShader()
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.OverrideEnd_SetPixelShader();
}

void AfxD3D9_Block_Present(bool block)
{
	if(!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.Block_Present(block);
}

IDirect3DStateBlock9* oldState = NULL;

bool AfxD3D9_BeginOwnRender()
{
	if (!g_OldDirect3DDevice9) return false;

	// Backup the DX9 state
	IDirect3DStateBlock9* oldState = NULL;
	
	if (FAILED(g_NewDirect3DDevice9.CreateStateBlock(D3DSBT_ALL, &oldState)))
		return false;
	
	g_NewDirect3DDevice9.EndScene();
}

void AfxD3D9_EndOwnRender()
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.BeginScene();

	if (oldState)
	{
		oldState->Apply();
		oldState->Release();
		oldState = NULL;
	}
}

void AfxD3D_WaitForGPU()
{
	if (!g_OldDirect3DDevice9) return;

	g_NewDirect3DDevice9.AfxWaitForGPU();
}