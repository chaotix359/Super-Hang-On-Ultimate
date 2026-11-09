/* Direct3D 11 presentation for the Windows frontend.
 * The game image is rendered by the CPU core (it costs ~1 ms); the GPU's job is presentation:
 * vsync-locked flip-model swaps for smooth pacing, optional tearing for VRR displays, and
 * high-quality scaling (sharp / smooth / sharp-bilinear) to any resolution. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <string.h>
#include <stdio.h>
#include "win32_d3d11.h"

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static IDXGISwapChain1 *sc;
static ID3D11RenderTargetView *rtv;
static ID3D11Texture2D *tex;
static ID3D11ShaderResourceView *srv;
static ID3D11SamplerState *samp_point, *samp_linear;
static ID3D11VertexShader *vs;
static ID3D11PixelShader *ps;
static ID3D11Buffer *cbuf;
static int tex_w, tex_h, bb_w, bb_h, tearing_ok;
static HWND g_wnd;

typedef HRESULT (WINAPI *PFN_D3DCOMPILE)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char *shader_src =
"cbuffer C : register(b0) { float2 src_size; float2 tex_size; float2 out_size; float mode; float pad; };\n"
"Texture2D t : register(t0); SamplerState sp : register(s0); SamplerState sl : register(s1);\n"
"struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
"V vs_main(uint id : SV_VertexID) { V o; float2 p = float2((id << 1) & 2, id & 2);\n"
"  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = p; return o; }\n"
"float4 ps_main(V i) : SV_Target {\n"
"  float2 texel = i.uv * src_size;\n"
"  if (mode < 0.5) return float4(t.Sample(sp, (floor(texel) + 0.5) / tex_size).rgb, 1);\n"     /* sharp: nearest */
"  if (mode < 1.5) return float4(t.Sample(sl, texel / tex_size).rgb, 1);\n"                   /* smooth: bilinear */
"  float2 scale = max(floor(out_size / src_size), 1.0);\n"                                     /* sharp-bilinear */
"  float2 tf = floor(texel); float2 s = frac(texel);\n"
"  float2 rr = 0.5 - 0.5 / scale; float2 cd = s - 0.5;\n"
"  float2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;\n"
"  return float4(t.Sample(sl, (tf + f) / tex_size).rgb, 1); }\n";

typedef struct { float src_w, src_h, tex_w, tex_h, out_w, out_h, mode, pad; } consts;

static void release_all(void) {
#define REL(x) if (x) { IUnknown_Release((IUnknown *)(x)); x = NULL; }
    REL(srv); REL(tex); REL(rtv); REL(cbuf); REL(ps); REL(vs); REL(samp_point); REL(samp_linear); REL(sc); REL(ctx); REL(dev);
#undef REL
}

static int make_rtv(void) {
    ID3D11Texture2D *bb = NULL;
    if (FAILED(IDXGISwapChain1_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return 0;
    HRESULT hr = ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv);
    D3D11_TEXTURE2D_DESC d; ID3D11Texture2D_GetDesc(bb, &d); bb_w = (int)d.Width; bb_h = (int)d.Height;
    ID3D11Texture2D_Release(bb);
    return SUCCEEDED(hr);
}

int d3d_init(HWND hwnd, int max_w, int max_h) {
    g_wnd = hwnd;
    D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, 3,
                                 D3D11_SDK_VERSION, &dev, NULL, &ctx))) return 0;
    IDXGIDevice1 *xdev = NULL; IDXGIAdapter *ad = NULL; IDXGIFactory2 *fac = NULL;
    if (FAILED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice1, (void **)&xdev))) goto fail;
    IDXGIDevice1_SetMaximumFrameLatency(xdev, 1);                  /* low input latency */
    IDXGIDevice1_GetAdapter(xdev, &ad);
    IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory2, (void **)&fac);
    IDXGIAdapter_Release(ad); IDXGIDevice1_Release(xdev);
    if (!fac) goto fail;
    { IDXGIFactory5 *f5 = NULL; BOOL allow = FALSE;
      if (SUCCEEDED(IDXGIFactory2_QueryInterface(fac, &IID_IDXGIFactory5, (void **)&f5))) {
          if (SUCCEEDED(IDXGIFactory5_CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)) && allow) tearing_ok = 1;
          IDXGIFactory5_Release(f5); } }
    DXGI_SWAP_CHAIN_DESC1 sd; memset(&sd, 0, sizeof sd);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_NONE;
    sd.Flags = tearing_ok ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(fac, (IUnknown *)dev, hwnd, &sd, NULL, NULL, &sc);
    if (FAILED(hr)) { sd.Scaling = DXGI_SCALING_STRETCH; hr = IDXGIFactory2_CreateSwapChainForHwnd(fac, (IUnknown *)dev, hwnd, &sd, NULL, NULL, &sc); }
    if (SUCCEEDED(hr)) IDXGIFactory2_MakeWindowAssociation(fac, hwnd, DXGI_MWA_NO_ALT_ENTER);   /* we handle Alt+Enter */
    IDXGIFactory2_Release(fac);
    if (FAILED(hr) || !make_rtv()) goto fail;

    /* source texture */
    tex_w = max_w; tex_h = max_h;
    D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof td);
    td.Width = (UINT)tex_w; td.Height = (UINT)tex_h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &tex))) goto fail;
    if (FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)tex, NULL, &srv))) goto fail;

    D3D11_SAMPLER_DESC ss; memset(&ss, 0, sizeof ss);
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; ss.MaxLOD = D3D11_FLOAT32_MAX;
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; ID3D11Device_CreateSamplerState(dev, &ss, &samp_point);
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; ID3D11Device_CreateSamplerState(dev, &ss, &samp_linear);

    /* shaders, compiled at run time by the system's d3dcompiler_47.dll */
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    PFN_D3DCOMPILE compile = dc ? (PFN_D3DCOMPILE)(void *)GetProcAddress(dc, "D3DCompile") : NULL;
    if (!compile) goto fail;
    ID3DBlob *vb = NULL, *pb = NULL, *err = NULL;
    if (FAILED(compile(shader_src, strlen(shader_src), "present", NULL, NULL, "vs_main", "vs_4_0", 0, 0, &vb, &err))) goto fail;
    if (FAILED(compile(shader_src, strlen(shader_src), "present", NULL, NULL, "ps_main", "ps_4_0", 0, 0, &pb, &err))) goto fail;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vb), ID3D10Blob_GetBufferSize(vb), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(pb), ID3D10Blob_GetBufferSize(pb), NULL, &ps);
    ID3D10Blob_Release(vb); ID3D10Blob_Release(pb);
    if (!vs || !ps) goto fail;

    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof(consts); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &cbuf))) goto fail;
    return 1;
fail:
    release_all();
    return 0;
}

int d3d_tearing_supported(void) { return tearing_ok; }

void d3d_resize(void) {
    if (!sc) return;
    if (rtv) { ID3D11RenderTargetView_Release(rtv); rtv = NULL; }
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
    IDXGISwapChain1_ResizeBuffers(sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, tearing_ok ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    make_rtv();
}

int d3d_present(const uint32_t *fb, int src_w, int src_h, double aspect, int filter, int sync_interval, int allow_tearing) {
    if (!sc || !rtv) return 0;
    RECT rc; GetClientRect(g_wnd, &rc);
    if (rc.right != bb_w || rc.bottom != bb_h) d3d_resize();
    /* upload with one duplicated column on the right so filtering never reads stale texels */
    static uint32_t padded[1024 * 256];
    if (src_w > tex_w || src_h > tex_h) return 0;
    int up_w = src_w < tex_w && (src_w + 1) * src_h <= 1024 * 256 ? src_w + 1 : src_w;   /* (the launcher's full-size picture goes up as it is) */
    const uint32_t *up = fb;
    if (up_w > src_w) {
        for (int y = 0; y < src_h; y++) {
            memcpy(padded + y * up_w, fb + y * src_w, (size_t)src_w * 4);
            padded[y * up_w + src_w] = fb[y * src_w + src_w - 1];
        }
        up = padded;
    }
    D3D11_BOX box = { 0, 0, 0, (UINT)up_w, (UINT)src_h, 1 };
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)tex, 0, &box, up, (UINT)up_w * 4, 0);

    /* letterbox to the requested display aspect */
    float vw = (float)bb_w, vh = (float)bb_h, x = 0, y = 0;
    if (aspect > 0) {
        if (vw / vh > aspect) { vw = (float)(bb_h * aspect); x = (bb_w - vw) * 0.5f; }
        else { vh = (float)(bb_w / aspect); y = (bb_h - vh) * 0.5f; }
    }
    vw = (float)(int)(vw + 0.5f); vh = (float)(int)(vh + 0.5f); x = (float)(int)x; y = (float)(int)y;
    const float black[4] = { 0, 0, 0, 1 };
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    D3D11_VIEWPORT vp = { x, y, vw, vh, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    consts c = { (float)src_w, (float)src_h, (float)tex_w, (float)tex_h, vw, vh, (float)filter, 0 };
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cbuf, 0, NULL, &c, 0, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cbuf);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &srv);
    ID3D11SamplerState *ss[2] = { samp_point, samp_linear };
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 2, ss);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    UINT flags = (sync_interval == 0 && allow_tearing && tearing_ok) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = IDXGISwapChain1_Present(sc, (UINT)sync_interval, flags);
    return hr == S_OK ? 1 : (hr == DXGI_STATUS_OCCLUDED ? 2 : 0);
}

void d3d_shutdown(void) { release_all(); }
