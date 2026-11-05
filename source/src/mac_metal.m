/* Metal presentation for the macOS frontend (the Mac twin of win32_d3d11.c).
 * The game image is rendered by the CPU core; the GPU's job is presentation: vsync'd presents and
 * high-quality scaling (sharp / smooth / sharp-bilinear) to any resolution. The shader is compiled at run
 * time from source, so building needs only the Command Line Tools, not full Xcode. */
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>
#include <string.h>
#include "mac_metal.h"

#define NBUF 3                                     /* textures in flight */

static id<MTLDevice> dev;
static id<MTLCommandQueue> queue;
static id<MTLRenderPipelineState> pipeline;
static id<MTLSamplerState> samp_point, samp_linear;
static id<MTLTexture> tex[NBUF];
static CAMetalLayer *mlayer;
static dispatch_semaphore_t inflight;
static int tex_w, tex_h, cur;
static uint32_t *padded;

/* same maths as the Direct3D 11 shader */
static NSString *const shader_src =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct C { float2 src_size; float2 tex_size; float2 out_size; float mode; float pad; };\n"
     "struct V { float4 pos [[position]]; float2 uv; };\n"
     "vertex V vs_main(uint id [[vertex_id]]) {\n"
     "  V o; float2 p = float2(float((id << 1) & 2u), float(id & 2u));\n"
     "  o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); o.uv = p; return o; }\n"
     "fragment float4 ps_main(V i [[stage_in]], constant C &c [[buffer(0)]], texture2d<float> t [[texture(0)]],\n"
     "                        sampler sp [[sampler(0)]], sampler sl [[sampler(1)]]) {\n"
     "  float2 texel = i.uv * c.src_size;\n"
     "  if (c.mode < 0.5) return float4(t.sample(sp, (floor(texel) + 0.5) / c.tex_size).rgb, 1.0);\n"   /* sharp: nearest */
     "  if (c.mode < 1.5) return float4(t.sample(sl, texel / c.tex_size).rgb, 1.0);\n"                 /* smooth: bilinear */
     "  float2 scale = max(floor(c.out_size / c.src_size), float2(1.0));\n"                            /* sharp-bilinear */
     "  float2 tf = floor(texel); float2 s = fract(texel);\n"
     "  float2 rr = 0.5 - 0.5 / scale; float2 cd = s - 0.5;\n"
     "  float2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;\n"
     "  return float4(t.sample(sl, (tf + f) / c.tex_size).rgb, 1.0); }\n";

typedef struct { float src_w, src_h, tex_w, tex_h, out_w, out_h, mode, pad; } consts;

int mtl_init(CAMetalLayer *layer, int max_w, int max_h) {
    dev = MTLCreateSystemDefaultDevice();
    if (!dev) return 0;
    queue = [dev newCommandQueue];
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:shader_src options:nil error:&err];
    if (!lib) { NSLog(@"Super Hang-On Ultimate: Metal shader failed: %@", err); dev = nil; return 0; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs_main"];
    pd.fragmentFunction = [lib newFunctionWithName:@"ps_main"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pipeline) { NSLog(@"Super Hang-On Ultimate: Metal pipeline failed: %@", err); dev = nil; return 0; }

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge; sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.minFilter = MTLSamplerMinMagFilterNearest; sd.magFilter = MTLSamplerMinMagFilterNearest;
    samp_point = [dev newSamplerStateWithDescriptor:sd];
    sd.minFilter = MTLSamplerMinMagFilterLinear; sd.magFilter = MTLSamplerMinMagFilterLinear;
    samp_linear = [dev newSamplerStateWithDescriptor:sd];

    /* source textures: 0x00RRGGBB little-endian = B, G, R, x bytes = BGRA8 (the shader forces alpha to 1) */
    tex_w = max_w; tex_h = max_h;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:(NSUInteger)tex_w height:(NSUInteger)tex_h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    for (int i = 0; i < NBUF; i++) { tex[i] = [dev newTextureWithDescriptor:td]; if (!tex[i]) { dev = nil; return 0; } }
    padded = (uint32_t *)calloc((size_t)tex_w * tex_h, 4);
    inflight = dispatch_semaphore_create(NBUF);

    mlayer = layer;
    layer.device = dev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.opaque = YES;
    if (@available(macOS 10.13, *)) layer.displaySyncEnabled = YES;
    return 1;
}

int mtl_present(const uint32_t *fb, int src_w, int src_h, int filter, double vx, double vy, double vw, double vh) {
    if (!dev || !mlayer || src_w > tex_w || src_h > tex_h) return 0;
    CGSize ds = mlayer.drawableSize;
    if (ds.width < 1 || ds.height < 1 || vw < 1 || vh < 1) return 0;
    /* never wait long: a hidden window gets no drawables, and the game must keep running (audio) */
    if (dispatch_semaphore_wait(inflight, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC)) != 0) return 0;
    int ok = 0;
    @autoreleasepool {
        id<CAMetalDrawable> d = [mlayer nextDrawable];
        if (d) {
            /* upload with one duplicated column on the right so filtering never reads stale texels */
            int up_w = src_w < tex_w ? src_w + 1 : src_w;
            for (int y = 0; y < src_h; y++) {
                memcpy(padded + y * up_w, fb + y * src_w, (size_t)src_w * 4);
                if (up_w > src_w) padded[y * up_w + src_w] = fb[y * src_w + src_w - 1];
            }
            [tex[cur] replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)up_w, (NSUInteger)src_h) mipmapLevel:0
                          withBytes:padded bytesPerRow:(NSUInteger)up_w * 4];

            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = d.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
            /* keep the viewport inside the drawable */
            if (vx < 0) { vw += vx; vx = 0; }
            if (vy < 0) { vh += vy; vy = 0; }
            if (vx + vw > ds.width) vw = ds.width - vx;
            if (vy + vh > ds.height) vh = ds.height - vy;
            MTLViewport vp = { vx, vy, vw, vh, 0.0, 1.0 };
            [e setViewport:vp];
            [e setRenderPipelineState:pipeline];
            consts c = { (float)src_w, (float)src_h, (float)tex_w, (float)tex_h, (float)vw, (float)vh, (float)filter, 0 };
            [e setFragmentBytes:&c length:sizeof c atIndex:0];
            [e setFragmentTexture:tex[cur] atIndex:0];
            [e setFragmentSamplerState:samp_point atIndex:0];
            [e setFragmentSamplerState:samp_linear atIndex:1];
            [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [e endEncoding];
            [cb presentDrawable:d];
            dispatch_semaphore_t s = inflight;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> b) { (void)b; dispatch_semaphore_signal(s); }];
            [cb commit];
            cur = (cur + 1) % NBUF;
            ok = 1;
        }
    }
    if (!ok) dispatch_semaphore_signal(inflight);
    return ok;
}
