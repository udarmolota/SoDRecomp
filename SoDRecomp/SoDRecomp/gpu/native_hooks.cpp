// The native renderer's hooks in the game's Direct3D (XDroid360, only in a build with the renderer:
// XDROID360_NATIVE_RENDERER). The renderer draws from the GPU ring, and two things it can't read
// there come from the game thread (native-renderer/src/nfsmw_nativo_ganchos.h):
//   - which shader container each D3D shader object was made from (the microcode in the ring is
//     already patched by D3D, so it can't be looked up by content);
//   - which shaders each Draw* had bound, with its primitive type and count, so the ring can pair
//     the draw with its shaders.
// (SoD) DAORecomp's gpu/native_hooks.cpp with State of Decay's addresses: the XDK's D3D is linked
// into the game as in Dragon Age, and each function was found by matching DAO's instruction by
// instruction (CreateVertexShader/CreatePixelShader 98 %, DrawVertices/DrawIndexedVertices 95 %,
// the UP draws and Set*Shader 100 %).
//   CreateVertexShader sub_83764598, CreatePixelShader sub_837643B0: r3 the container on entry,
//     the new object in r3 on return (0 on failure).
//   DrawVertices sub_8376CDB0 (dev, prim, start, count), DrawIndexedVertices sub_8376D1C8 (dev, prim,
//     base, start, count), DrawVerticesUP sub_8376C788 (dev, prim, count, data, stride),
//     DrawIndexedVerticesUP sub_8376CD10 (dev, prim, min index, vertices, count, ...).
//   SoD's device is laid out 0x170 bytes further than DAO's: the bound vertex shader is at +0x3310
//     and the pixel shader at +0x330C (SetVertexShader sub_83762A98, SetPixelShader sub_83762890).
#include "stdafx.h"

#ifdef XDROID360_NATIVE_RENDERER

#include "kernel/function.h"
#include "kernel/game_build.h"

#include "nfsmw_nativo_ganchos.h"

namespace
{
    namespace nativo = nfsmw::nativo;

    constexpr uint32_t kBoundVertexShader = 0x3310;
    constexpr uint32_t kBoundPixelShader = 0x330C;

    // The device offsets, set once when the library loads (before the game makes a device).
    struct ConfigureDevice
    {
        ConfigureDevice() { nativo::ConfigurarDispositivoD3D(kBoundVertexShader, kBoundPixelShader); }
    } s_configureDevice;

    template <bool kVertex>
    void CreateShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        const auto* entry = nativo::ganchos_detalle::IdentificarCreacion(base, ctx.r3.u32, kVertex);
        original(ctx, base);
        if (ctx.r3.u32 != 0)
            nativo::ganchos_detalle::RecordarCreacion(ctx.r3.u32, entry, kVertex);
    }

    // r3 the device; r4..r7 as the call has them (the renderer reads type and count from them).
    template <nativo::FuncionDibujo kFunction>
    void DrawHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        nativo::AnotarDibujo(kFunction, base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
        original(ctx, base);
        // A record its FlushState didn't take goes to the queue.
        nativo::TerminarDibujo();
    }

    void CreateVertexShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        CreateShaderHook<true>(ctx, base, original);
    }
    void CreatePixelShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        CreateShaderHook<false>(ctx, base, original);
    }
    void DrawVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kVertices>(ctx, base, original);
    }
    void DrawIndexedVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kIndexados>(ctx, base, original);
    }
    void DrawVerticesUPHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kVerticesUP>(ctx, base, original);
    }
    void DrawIndexedVerticesUPHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kIndexadosUP>(ctx, base, original);
    }
}

GAME_HOOK_ONE(sub_83764598, CreateVertexShaderHook)
GAME_HOOK_ONE(sub_837643B0, CreatePixelShaderHook)
GAME_HOOK_ONE(sub_8376CDB0, DrawVerticesHook)
GAME_HOOK_ONE(sub_8376D1C8, DrawIndexedVerticesHook)
GAME_HOOK_ONE(sub_8376C788, DrawVerticesUPHook)
GAME_HOOK_ONE(sub_8376CD10, DrawIndexedVerticesUPHook)

#endif // XDROID360_NATIVE_RENDERER
