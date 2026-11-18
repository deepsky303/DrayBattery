// Xbox-Mode GIP battery reader, adapted from tested local C++ probe.
// Protocol derives from 2rx2/Razer-Wolverine-Battery-Widget (MIT).
// DrayBattery / Wolverine v3 Pro battery transport probe.
// Native Windows Gaming Input Custom factory based on the MIT-licensed
// research in 2rx2/Razer-Wolverine-Battery-Widget (app/src/gip.rs, protocol.rs).
// Read-only power request commands, no firmware writes.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.custom.h>
#include <windows.gaming.input.h>
#include <atomic>
#include <cstdio>
#include <array>
#include <mutex>
#include <vector>
#include <cwchar>
#include <new>
#include "wolverine_xbox.h"
#include "drivers/wolverine_xbox_cache.h"

namespace C = ABI::Windows::Gaming::Input::Custom;
namespace GI = ABI::Windows::Gaming::Input;

// Windows runtime internals: IGameControllerImpl is not declared in public SDK.
// This exact interface GUID and five methods match the independently verified
// Xbox controller sink implementation in 2rx2/Razer-Wolverine-Battery-Widget.
struct __declspec(uuid("06e58977-7684-4dc5-bad1-cda52a4aa06d")) IGameControllerImpl : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Initialize(void*,void*,void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved1(void*,void*,void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved2(void*,void*,void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved3(void*,void*,void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved4(void*,void*,void*) = 0;
};
const GUID AGILE_IID={0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
std::atomic<int> g_providerEvents{0};
std::atomic<int> g_gipEvents{0};
std::atomic<int> g_rawValue{-1};
std::atomic<int> g_charging{-1};
std::atomic<HWND> g_popup{nullptr};
std::mutex g_providerMutex;
C::IGipGameControllerProvider* g_gip=nullptr;
std::atomic<bool> g_readable{false};

void SendPower() {
    C::IGipGameControllerProvider* provider=nullptr;
    {
        std::lock_guard<std::mutex> lk(g_providerMutex);
        if(g_gip){g_gip->AddRef();provider=g_gip;}
    }
    if(!provider)return;
    for(BYTE command: {BYTE(0x80),BYTE(0x84)}){
        BYTE report[60]{};
        report[1]=0x1F;report[5]=2;report[6]=7;report[7]=command;
        BYTE crc=0;for(int i=2;i<58;i++)crc ^= report[i];report[58]=crc;
        HRESULT hr=provider->SendMessage(C::GipMessageClass_Command,0x0F,60,report);
        std::printf("POWER_REQUEST cmd=%02X hr=%08lX\n",command,(unsigned long)hr);
        std::fflush(stdout);
    }
    provider->Release();
}

struct Controller final : C::IGameControllerInputSink,
                          C::IGipGameControllerInputSink,
                          IGameControllerImpl {
    std::atomic<ULONG> refs{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IInspectable)||IsEqualIID(iid,AGILE_IID))
            *out=static_cast<C::IGameControllerInputSink*>(this);
        else if(IsEqualIID(iid,__uuidof(C::IGameControllerInputSink)))
            *out=static_cast<C::IGameControllerInputSink*>(this);
        else if(IsEqualIID(iid,__uuidof(C::IGipGameControllerInputSink)))
            *out=static_cast<C::IGipGameControllerInputSink*>(this);
        else if(IsEqualIID(iid,__uuidof(IGameControllerImpl)))
            *out=static_cast<IGameControllerImpl*>(this);
        if(!*out)return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=--refs;if(!n)delete this;return n;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** value) override {
        if(!count||!value)return E_POINTER;
        *count=2;
        *value=(IID*)CoTaskMemAlloc(sizeof(IID)*2);
        if(!*value)return E_OUTOFMEMORY;
        (*value)[0]=__uuidof(C::IGameControllerInputSink);
        (*value)[1]=__uuidof(C::IGipGameControllerInputSink);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        return WindowsCreateString(L"DrayBattery.Wolverine",(UINT32)wcslen(L"DrayBattery.Wolverine"),name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trust) override {
        if(!trust)return E_POINTER;*trust=BaseTrust;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInputResumed(UINT64) override {
        g_readable.store(true);
        std::printf("INPUT_RESUMED\n");std::fflush(stdout);
        SendPower();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInputSuspended(UINT64) override {
        g_readable.store(false);
        std::printf("INPUT_SUSPENDED\n");std::fflush(stdout);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyReceived(UINT64,BYTE,boolean) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE OnMessageReceived(UINT64,C::GipMessageClass,
        BYTE msg,BYTE,UINT32 len,BYTE* buf) override {
        ++g_gipEvents;
        if(!buf||len<60||msg!=0x10)return S_OK;
        if(buf[0]!=2||buf[1]!=0x1f||buf[6]!=7)return S_OK;
        BYTE sum=0;for(int i=2;i<58;i++)sum ^= buf[i];
        if(sum!=buf[58])return S_OK;
        if(buf[7]==0x80){
            g_rawValue.store(buf[9]);
            if(HWND h=g_popup.load())PostMessageW(h,WM_APP+7,0,0);
        }else if(buf[7]==0x84){
            g_charging.store(buf[9]!=0);
        }
        std::fflush(stdout);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(void*,void*,void*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Reserved1(void*,void*,void*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Reserved2(void*,void*,void*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Reserved3(void*,void*,void*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Reserved4(void*,void*,void*) override{return E_NOTIMPL;}
};

struct Factory final : C::ICustomGameControllerFactory {
    std::atomic<ULONG> refs{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IInspectable)||IsEqualIID(iid,__uuidof(C::ICustomGameControllerFactory))||IsEqualIID(iid,AGILE_IID))
            *out=static_cast<C::ICustomGameControllerFactory*>(this);
        if(!*out)return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** value) override {
        if(!count||!value)return E_POINTER;*count=1;
        *value=(IID*)CoTaskMemAlloc(sizeof(IID));
        if(!*value)return E_OUTOFMEMORY;
        **value=__uuidof(C::ICustomGameControllerFactory);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        return WindowsCreateString(L"DrayBattery.Factory",(UINT32)wcslen(L"DrayBattery.Factory"),name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trust) override {
        if(!trust)return E_POINTER;*trust=BaseTrust;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateGameController(C::IGameControllerProvider* p,IInspectable** out) override {
        ++g_providerEvents;
        if(!out)return E_POINTER;*out=nullptr;
        UINT16 vid=0,pid=0;
        if(p){p->get_HardwareVendorId(&vid);p->get_HardwareProductId(&pid);}
        C::IGipGameControllerProvider* gip=nullptr;
        HRESULT hr=p ? p->QueryInterface(__uuidof(C::IGipGameControllerProvider),(void**)&gip) : E_NOINTERFACE;
        std::printf("FACTORY_CALLBACK VID=%04X PID=%04X GIP=%08lX\n",vid,pid,(unsigned long)hr);
        std::fflush(stdout);
        if(SUCCEEDED(hr)&&gip){
            std::lock_guard<std::mutex> lk(g_providerMutex);
            if(g_gip)g_gip->Release();
            g_gip=gip;
        }
        Controller* c=new(std::nothrow) Controller;
        if(!c)return E_OUTOFMEMORY;
        *out=static_cast<C::IGameControllerInputSink*>(c);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnGameControllerAdded(GI::IGameController*) override {
        std::puts("CONTROLLER_ADDED");std::fflush(stdout);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnGameControllerRemoved(GI::IGameController*) override {
        std::puts("CONTROLLER_REMOVED");std::fflush(stdout);return S_OK;
    }
};


LRESULT CALLBACK XboxReaderWndProc(HWND w, UINT msg, WPARAM a, LPARAM b) {
    if(msg==WM_CLOSE) {DestroyWindow(w);return 0;}
    if(msg==WM_APP+7) {InvalidateRect(w,nullptr,TRUE);return 0;}
    if(msg==WM_PAINT){
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(w,&ps);
        RECT r{};
        GetClientRect(w,&r);
        FillRect(dc,&r,GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(32,42,54));
        HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HGDIOBJ old=SelectObject(dc,font);
        wchar_t message[180];
        int raw=g_rawValue.load();
        if(raw>=0)
            swprintf_s(message,L"Razer Wolverine V3 Pro: %d%% (Xbox GIP)\nEstimated from controller data; saved in DrayBattery.",(raw*100+127)/255);
        else
            wcscpy_s(message,L"Reading Wolverine V3 Pro battery...\nWaiting for Xbox GIP response.");
        RECT t=r;
        t.left+=20;t.top+=16;t.right-=20;t.bottom-=12;
        DrawTextW(dc,message,-1,&t,DT_LEFT|DT_TOP|DT_WORDBREAK);
        SelectObject(dc,old);
        EndPaint(w,&ps);
        return 0;
    }
    return DefWindowProcW(w,msg,a,b);
}

int RunWolverineXboxRefresh() {
    if(!IsWolverineXboxPresent()) return 2;
    g_rawValue.store(-1);
    g_charging.store(-1);
    g_readable.store(false);
    HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
    if(FAILED(hr))return 3;

    HSTRING name=nullptr;
    const wchar_t* cls=L"Windows.Gaming.Input.Custom.GameControllerFactoryManager";
    WindowsCreateString(cls,(UINT32)wcslen(cls),&name);
    C::IGameControllerFactoryManagerStatics* statics=nullptr;
    hr=RoGetActivationFactory(name,__uuidof(C::IGameControllerFactoryManagerStatics),(void**)&statics);
    WindowsDeleteString(name);
    if(FAILED(hr) || !statics){RoUninitialize();return 4;}

    Factory* factory=new(std::nothrow) Factory;
    if(!factory){statics->Release();RoUninitialize();return 5;}
    hr=statics->RegisterCustomFactoryForHardwareId(factory,0x1532,0x0A3F);
    if(FAILED(hr)){factory->Release();statics->Release();RoUninitialize();return 6;}

    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance=instance;
    wc.lpszClassName=L"DrayBatteryXboxReader";
    wc.lpfnWndProc=XboxReaderWndProc;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&wc);
    HWND popup=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,
        L"DrayBattery - Xbox Battery",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        320,210,465,138,nullptr,nullptr,instance,nullptr);
    if(popup){
        g_popup.store(popup);
        ShowWindow(popup,SW_SHOWNORMAL);
        UpdateWindow(popup);
        SetForegroundWindow(popup);
    }
    int raw=-1;
    for(int tick=0;tick<150;tick++){
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if(popup && !IsWindow(popup))break;
        if(g_readable.load() && (tick==12 || tick==35))SendPower();
        raw=g_rawValue.load();
        if(raw>=0){
            // Allow the paired charging query to arrive before saving.
            Sleep(200);
            break;
        }
        Sleep(60);
    }
    const bool success=raw>=0 && SaveWolverineXboxReading(raw,g_charging.load());
    if(success && popup && IsWindow(popup)){
        InvalidateRect(popup,nullptr,TRUE);
        UpdateWindow(popup);
        Sleep(550);
    }
    if(popup && IsWindow(popup))DestroyWindow(popup);
    g_popup.store(nullptr);
    if(statics)statics->Release();
    factory->Release();
    {
        std::lock_guard<std::mutex> lk(g_providerMutex);
        if(g_gip)g_gip->Release();
        g_gip=nullptr;
    }
    RoUninitialize();
    return success?0:7;
}
