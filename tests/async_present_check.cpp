// Lab for src/async_present.hpp: hand-off, waiting, result propagation, WAS_STILL_DRAWING of a paced mirror, stop.
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include "../src/async_present.hpp"
using namespace d2vr;
// _Exit: a failed check must not run static destructors (the instance's worker thread is still joinable, and std::terminate hid the
// message behind 0xC0000409 until 2026-11-20).
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::fflush(stdout);std::_Exit(1);}}while(0)

// The n-th Present returns results[n] (S_OK unless set before its kick). 2026-11-20: one shared "next result" raced: the test reset it
// while the worker was still inside the failing Present, which then returned S_OK, and the check at the next kick failed.
static std::atomic<int> presents{0};static std::atomic<DWORD> present_thread{0};static HRESULT results[16]{};
static HRESULT WINAPI fake_present(IDXGISwapChain*,UINT,UINT) {Sleep(5);present_thread=GetCurrentThreadId();const int n=presents++;return n<16?results[n]:S_OK;}

struct FakeChain final : IDXGISwapChain { // only AddRef/Release are used
    std::atomic<ULONG> refs{1};
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override {return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID,void**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Present(UINT,UINT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT,REFIID,void**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL,IDXGIOutput*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL*,IDXGIOutput**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT,UINT,UINT,DXGI_FORMAT,UINT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT*) override {return E_NOTIMPL;}
};

int main() {
    auto& a=AsyncPresent::instance();FakeChain chain;
    CHECK(!a.running() && !a.in_flight());
    a.wait(); // nothing in flight: returns at once
    CHECK(a.start(&fake_present) && a.running());
    // kick returns at once; the Present runs on the worker
    CHECK(a.kick(&chain,0,0,false)==S_OK);
    CHECK(a.in_flight() && chain.refs==2);
    a.wait();
    CHECK(!a.in_flight() && presents==1 && present_thread!=GetCurrentThreadId() && chain.refs==1);
    // an error reaches the caller one Present later
    results[1]=DXGI_ERROR_DEVICE_REMOVED;
    CHECK(a.kick(&chain,0,0,false)==S_OK);
    CHECK(a.kick(&chain,0,0,false)==DXGI_ERROR_DEVICE_REMOVED); // kick waits for the previous Present first
    a.wait();
    // a paced mirror's WAS_STILL_DRAWING is success
    results[3]=DXGI_ERROR_WAS_STILL_DRAWING;
    a.kick(&chain,0,8,true);a.wait();
    CHECK(a.kick(&chain,0,0,false)==S_OK);a.wait();
    CHECK(presents==5 && chain.refs==1);
    // guard waits are counted only when a Present is in flight
    a.wait(true);
    CHECK(a.json().find("\"guard_waits\":0")!=std::string::npos);
    a.kick(&chain,0,0,false);a.wait(true);
    CHECK(a.json().find("\"guard_waits\":1")!=std::string::npos && a.json().find("\"still_drawing\":1")!=std::string::npos);
    a.note_violation(); // v1410: the count-only safety net
    CHECK(a.json().find("\"guard_waits\":2")!=std::string::npos);
    a.stop();
    CHECK(!a.running() && presents==6);
    std::puts("async_present_check ok");
}
