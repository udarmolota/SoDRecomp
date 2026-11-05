// Unit checks for the instructions SoD added to XenonRecomp (scripts/patch_recompiler_sod.py).
// Each <name>_10000 function is XenonRecomp's translation of one instruction (gen/<name>.cpp,
// from scripts/make_insn_tests.py). The references below are written from the PowerPC /
// VMX128 definitions (as Xenia implements them) on guest-order values, independently of
// the generated code's element-reversed register layout.

#define PPC_CONFIG_H_INCLUDED
#include <ppc_context.h>

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#define TEST_FUNCS(X) \
    X(ldux) X(stfdux) X(lhbrx) X(lhbrx_ra0) X(mulhd) X(mulhdu) X(fctiw) X(fctiwz) X(fctid) X(fctidz) X(fnmadd) \
    X(rlwimi_dot) X(rldicl_dot) X(blrl) X(vminsw) X(vsubuwm) X(vpkshss) X(vpkshss128) \
    X(vcmpgtuw) X(vcmpgtuw_dot) X(vcmpgtsw) X(vcmpgtsw_dot) X(vavgsw) X(vslh) X(vsrh) \
    X(vsl) X(vslo) X(vslo128) X(vrlw128) X(stvebx) X(vupkd3d128_f16x4) \
    X(vupkd3d128_f16x2) X(vpkd3d128_f16x2) X(vsldoi128_misdecode)

#define DECLARE(name) PPC_EXTERN_FUNC(name##_10000);
TEST_FUNCS(DECLARE)

// ---------------------------------------------------------------- harness

alignas(0x1000) static uint8_t g_memory[0x20000];
static int g_checks, g_failures;
static std::map<std::string, std::pair<int, int>> g_perTest; // test -> (checks, failures)

static void Count(const char* test, bool ok)
{
    auto& t = g_perTest[test];
    t.first++;
    g_checks++;
    if (!ok)
    {
        t.second++;
        g_failures++;
    }
}
static uint32_t g_indirectTarget, g_indirectLr;

// PPC_CALL_INDIRECT_FUNC is defined to this for the generated code (CMakeLists.txt).
void TestIndirectCall(PPCContext& ctx, uint8_t*, uint32_t target)
{
    g_indirectTarget = target;
    g_indirectLr = uint32_t(ctx.lr);
}

template<typename T>
static void Check(const char* test, const char* what, T actual, std::type_identity_t<T> expected)
{
    Count(test, actual == expected);
    if (actual != expected)
    {
        std::printf("FAIL %s: %s: got 0x%llX, expected 0x%llX\n", test, what,
            (unsigned long long)actual, (unsigned long long)expected);
    }
}

static uint32_t FloatBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t DoubleBits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static void CheckFloat(const char* test, const char* what, float actual, float expected)
{
    // NaNs compare by class (any NaN), everything else bit for bit (keeps -0.0 apart).
    if (std::isnan(expected) && std::isnan(actual))
        return Count(test, true);
    Check(test, what, FloatBits(actual), FloatBits(expected));
}

static void CheckDouble(const char* test, const char* what, double actual, double expected)
{
    if (std::isnan(expected) && std::isnan(actual))
        return Count(test, true);
    Check(test, what, DoubleBits(actual), DoubleBits(expected));
}

// Guest-order vector access: guest element i of a vector of N lanes is host lane N-1-i.
using V4 = std::array<uint32_t, 4>;
using V8 = std::array<uint16_t, 8>;
using V16 = std::array<uint8_t, 16>;
using F4 = std::array<float, 4>;

static PPCVRegister FromWords(V4 e) { PPCVRegister v{}; for (int i = 0; i < 4; i++) v.u32[3 - i] = e[i]; return v; }
static PPCVRegister FromHalves(V8 e) { PPCVRegister v{}; for (int i = 0; i < 8; i++) v.u16[7 - i] = e[i]; return v; }
static PPCVRegister FromBytes(V16 e) { PPCVRegister v{}; for (int i = 0; i < 16; i++) v.u8[15 - i] = e[i]; return v; }
static PPCVRegister FromFloats(F4 e) { PPCVRegister v{}; for (int i = 0; i < 4; i++) v.f32[3 - i] = e[i]; return v; }
static V4 Words(const PPCVRegister& v) { V4 e; for (int i = 0; i < 4; i++) e[i] = v.u32[3 - i]; return e; }
static V8 Halves(const PPCVRegister& v) { V8 e; for (int i = 0; i < 8; i++) e[i] = v.u16[7 - i]; return e; }
static V16 Bytes(const PPCVRegister& v) { V16 e; for (int i = 0; i < 16; i++) e[i] = v.u8[15 - i]; return e; }
static F4 Floats(const PPCVRegister& v) { F4 e; for (int i = 0; i < 4; i++) e[i] = v.f32[3 - i]; return e; }

template<typename A>
static void CheckLanes(const char* test, const A& actual, const A& expected)
{
    char what[32];
    for (size_t i = 0; i < actual.size(); i++)
    {
        std::snprintf(what, sizeof(what), "element %zu", i);
        if constexpr (std::is_same_v<typename A::value_type, float>)
            CheckFloat(test, what, actual[i], expected[i]);
        else
            Check(test, what, uint64_t(actual[i]), uint64_t(expected[i]));
    }
}

static PPCContext NewContext()
{
    PPCContext ctx{};
    ctx.fpscr.loadFromHost();
    ctx.fpscr.storeFromGuest(PPC_ROUND_NEAREST);
    return ctx;
}

static uint8_t* const base = g_memory;

static void StoreBE64(uint32_t a, uint64_t v) { for (int i = 0; i < 8; i++) base[a + i] = uint8_t(v >> (56 - 8 * i)); }
static uint64_t LoadBE64(uint32_t a) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | base[a + i]; return v; }

// ---------------------------------------------------------------- references

static uint64_t MulHighU(uint64_t a, uint64_t b)
{
    // schoolbook on 32-bit limbs
    uint64_t al = uint32_t(a), ah = a >> 32, bl = uint32_t(b), bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + uint32_t(lh) + uint32_t(hl);
    return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

static int64_t MulHighS(int64_t a, int64_t b)
{
    uint64_t h = MulHighU(uint64_t(a), uint64_t(b));
    if (a < 0) h -= uint64_t(b);
    if (b < 0) h -= uint64_t(a);
    return int64_t(h);
}

static uint32_t Rotl32(uint32_t v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
static uint64_t Rotl64(uint64_t v, int n) { n &= 63; return n ? (v << n) | (v >> (64 - n)) : v; }

// Rounds per FPSCR[RN] mode.
static double RoundRef(double x, int mode)
{
    double r;
    switch (mode)
    {
    case PPC_ROUND_TOWARD_ZERO: r = std::trunc(x); break;
    case PPC_ROUND_UP: r = std::ceil(x); break;
    case PPC_ROUND_DOWN: r = std::floor(x); break;
    default:
    {
        double f = std::floor(x), d = x - f;
        r = d > 0.5 ? f + 1 : d < 0.5 ? f : (std::fmod(f, 2.0) == 0 ? f : f + 1);
        break;
    }
    }
    return r;
}

// fctiw (fctiwz: mode toward zero): round, then saturate to int32 (NaN -> 0x80000000).
static uint32_t Fctiw(double x, int mode)
{
    if (std::isnan(x))
        return 0x80000000;
    double r = RoundRef(x, mode);
    if (r > 2147483647.0) return 0x7FFFFFFF;
    if (r < -2147483648.0) return 0x80000000;
    return uint32_t(int32_t(r));
}

// fctid (fctidz: mode toward zero): round, then saturate to int64 (NaN -> 0x8000000000000000).
static uint64_t Fctid(double x, int mode)
{
    if (std::isnan(x))
        return 0x8000000000000000ull;
    double r = RoundRef(x, mode);
    if (r >= 9223372036854775808.0) return 0x7FFFFFFFFFFFFFFFull;
    if (r < -9223372036854775808.0) return 0x8000000000000000ull;
    return uint64_t(int64_t(r));
}

// IEEE binary16 -> float, from the definition.
static float HalfToFloat(uint16_t h)
{
    float sign = (h & 0x8000) ? -1.0f : 1.0f;
    int exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    if (exponent == 31)
        return mantissa ? std::numeric_limits<float>::quiet_NaN() : sign * INFINITY;
    if (exponent == 0)
        return sign * std::ldexp(float(mantissa), -24);
    return sign * std::ldexp(float(1024 + mantissa), exponent - 25);
}

// vpkd3d128's float -> half (XenonRecomp's float16_4 pack, which SoD's float16_2 reuses):
// toward zero; NaN and |x| > 65504 saturate to 0x7FFF; the sign is kept.
static uint16_t FloatToHalfTrunc(float f)
{
    uint16_t sign = (FloatBits(f) >> 16) & 0x8000;
    float a = std::fabs(f);
    if (std::isnan(f) || a > 65504.0f)
        return sign | 0x7FFF;
    uint16_t lo = 0, hi = 0x7BFF; // largest half not above a
    while (lo < hi)
    {
        uint16_t mid = uint16_t((lo + hi + 1) / 2);
        if (HalfToFloat(mid) <= a) lo = mid; else hi = mid - 1;
    }
    return sign | lo;
}

// ---------------------------------------------------------------- tests

static void TestLoadsStores()
{
    {
        PPCContext ctx = NewContext();
        StoreBE64(0x1008, 0x1122334455667788ull);
        ctx.r4.u64 = 0x1000; ctx.r5.u64 = 8; ctx.r3.u64 = 0xDEAD;
        ldux_10000(ctx, base);
        Check("ldux", "rD", ctx.r3.u64, 0x1122334455667788ull);
        Check("ldux", "rA updated", ctx.r4.u64, 0x1008ull);
    }
    {
        PPCContext ctx = NewContext();
        StoreBE64(0x1100, 0);
        ctx.f1.f64 = -1.5; ctx.r4.u64 = 0x10F0; ctx.r5.u64 = 0x10;
        stfdux_10000(ctx, base);
        Check("stfdux", "memory", LoadBE64(0x1100), DoubleBits(-1.5));
        Check("stfdux", "rA updated", ctx.r4.u64, 0x1100ull);
    }
    {
        PPCContext ctx = NewContext();
        base[0x1203] = 0x12; base[0x1204] = 0x34;
        ctx.r4.u64 = 0x1200; ctx.r5.u64 = 3; ctx.r3.u64 = ~0ull;
        lhbrx_10000(ctx, base);
        Check("lhbrx", "rD (byte-reversed, zero-extended)", ctx.r3.u64, 0x3412ull);
        ctx.r5.u64 = 0x1203; ctx.r3.u64 = ~0ull; ctx.r0.u64 = 0x5000; // rA=0 means 0, not r0
        lhbrx_ra0_10000(ctx, base);
        Check("lhbrx rA=0", "rD", ctx.r3.u64, 0x3412ull);
    }
    {
        // stvebx stores the byte element selected by EA & 15 and nothing else
        const V16 bytes = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF };
        for (uint32_t ea : { 0x1300u, 0x1307u, 0x130Fu, 0x1311u })
        {
            PPCContext ctx = NewContext();
            memset(base + 0x1300, 0x55, 0x20);
            ctx.v3 = FromBytes(bytes); ctx.r4.u64 = 0x1000; ctx.r5.u64 = ea - 0x1000;
            stvebx_10000(ctx, base);
            for (uint32_t a = 0x1300; a < 0x1320; a++)
                Check("stvebx", "memory byte", uint64_t(base[a]), uint64_t(a == ea ? bytes[ea & 15] : 0x55));
        }
    }
}

static void TestInteger()
{
    const int64_t s[] = { 0, 1, -1, 3, -7, 0x7FFFFFFFFFFFFFFFll, (int64_t)0x8000000000000000ull, 0x123456789ABCDEFll, -0x123456789ABCDEFll };
    for (int64_t a : s)
        for (int64_t b : s)
        {
            PPCContext ctx = NewContext();
            ctx.r4.s64 = a; ctx.r5.s64 = b;
            mulhd_10000(ctx, base);
            Check("mulhd", "rD", ctx.r3.u64, uint64_t(MulHighS(a, b)));
            ctx.r4.s64 = a; ctx.r5.s64 = b;
            mulhdu_10000(ctx, base);
            Check("mulhdu", "rD", ctx.r3.u64, MulHighU(uint64_t(a), uint64_t(b)));
        }

    // rlwimi. r30,r11,16,0,15: r30's high halfword (bits 0-15) := r11's low halfword.
    struct { uint64_t r30, r11; } rl[] = { { 0x1234, 0 }, { 0, 0 }, { 0xFFFFFFFF00001234ull, 0x8000 }, { 0x0000ABCD, 0x7FFF } };
    for (auto& c : rl)
    {
        PPCContext ctx = NewContext();
        ctx.r30.u64 = c.r30; ctx.r11.u64 = c.r11;
        rlwimi_dot_10000(ctx, base);
        const uint32_t mask = 0xFFFF0000;
        uint64_t expected = (c.r30 & ~uint64_t(mask)) | (Rotl32(uint32_t(c.r11), 16) & mask);
        Check("rlwimi.", "rA", ctx.r30.u64, expected);
        int32_t low = int32_t(expected);
        Check("rlwimi.", "cr0.lt", ctx.cr0.lt, uint8_t(low < 0));
        Check("rlwimi.", "cr0.gt", ctx.cr0.gt, uint8_t(low > 0));
        Check("rlwimi.", "cr0.eq", ctx.cr0.eq, uint8_t(low == 0));
    }

    // rldicl. r11,r11,32,32: CR0 compares all 64 bits (0x80000000 is positive).
    for (uint64_t v : { 0x8000000000000000ull, 0x00000000FFFFFFFFull, 0x1234567800000000ull, 0ull })
    {
        PPCContext ctx = NewContext();
        ctx.r11.u64 = v;
        rldicl_dot_10000(ctx, base);
        uint64_t expected = Rotl64(v, 32) & 0x00000000FFFFFFFFull;
        Check("rldicl.", "rA", ctx.r11.u64, expected);
        Check("rldicl.", "cr0.lt", ctx.cr0.lt, uint8_t(int64_t(expected) < 0));
        Check("rldicl.", "cr0.gt", ctx.cr0.gt, uint8_t(int64_t(expected) > 0));
        Check("rldicl.", "cr0.eq", ctx.cr0.eq, uint8_t(expected == 0));
    }

    {
        // blrl: call to the old LR, LR = the next instruction
        PPCContext ctx = NewContext();
        ctx.lr = 0x82001230;
        g_indirectTarget = g_indirectLr = 0;
        blrl_10000(ctx, base);
        Check("blrl", "call target", g_indirectTarget, 0x82001230u);
        Check("blrl", "lr during call", g_indirectLr, 0x10004u);
    }
}

static void TestFloat()
{
    const double values[] = { 0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 3.5, 2.7, -2.7, 2.1, -2.1, 1e-300,
        2147483647.0, 2147483647.4, 2147483647.6, -2147483648.0, -2147483648.4, -2147483648.6,
        3e9, -3e9, 1e300, INFINITY, -INFINITY, NAN };
    const int modes[] = { PPC_ROUND_NEAREST, PPC_ROUND_TOWARD_ZERO, PPC_ROUND_UP, PPC_ROUND_DOWN };
    const char* const modeNames[] = { "fctiw (round to nearest)", "fctiw (round toward zero)", "fctiw (round up)", "fctiw (round down)" };
    for (int mode : modes)
        for (double x : values)
        {
            PPCContext ctx = NewContext();
            ctx.fpscr.storeFromGuest(mode);
            ctx.f2.f64 = x;
            fctiw_10000(ctx, base);
            char what[64];
            std::snprintf(what, sizeof(what), "mode %d value %g", mode, x);
            Check(modeNames[mode], what, ctx.f1.u32, Fctiw(x, mode));
            // fctiwz truncates whatever the mode
            ctx.f2.f64 = x;
            fctiwz_10000(ctx, base);
            Check("fctiwz", what, ctx.f1.u32, Fctiw(x, PPC_ROUND_TOWARD_ZERO));
            ctx.fpscr.storeFromGuest(PPC_ROUND_NEAREST);
        }

    const double values64[] = { 0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 2.7, -2.7, 2.1, -2.1, 3e9, -3e9,
        4503599627370495.5, -4503599627370495.5, 9223372036854774784.0, -9223372036854774784.0,
        9223372036854775808.0, -9223372036854775808.0, 1e19, -1e19, 1e300, INFINITY, -INFINITY, NAN };
    const char* const modeNames64[] = { "fctid (round to nearest)", "fctid (round toward zero)", "fctid (round up)", "fctid (round down)" };
    for (int mode : modes)
        for (double x : values64)
        {
            PPCContext ctx = NewContext();
            ctx.fpscr.storeFromGuest(mode);
            char what[64];
            std::snprintf(what, sizeof(what), "mode %d value %.17g", mode, x);
            ctx.f2.f64 = x;
            fctid_10000(ctx, base);
            Check(modeNames64[mode], what, ctx.f1.u64, Fctid(x, mode));
            ctx.f2.f64 = x;
            fctidz_10000(ctx, base);
            Check("fctidz", what, ctx.f1.u64, Fctid(x, PPC_ROUND_TOWARD_ZERO));
            ctx.fpscr.storeFromGuest(PPC_ROUND_NEAREST);
        }

    struct { double a, c, b, r; } fm[] = {
        { 2, 3, 4, -10 }, { 2, -3, 4, 2 }, { 0, 5, -0.0, -0.0 }, { -1, 1, 1, -0.0 },
        { INFINITY, 0, 1, NAN }, { NAN, 1, 1, NAN }, { 1e308, 10, 0, -INFINITY }, { 0.5, 0.25, 0.125, -0.25 } };
    for (auto& c : fm)
    {
        PPCContext ctx = NewContext();
        ctx.f2.f64 = c.a; ctx.f3.f64 = c.c; ctx.f4.f64 = c.b;
        fnmadd_10000(ctx, base);
        CheckDouble("fnmadd", "frD", ctx.f1.f64, c.r);
    }
}

static void TestVectorInteger()
{
    const int32_t I32[] = { 0, 1, -1, 2, -3, 1000, INT32_MAX, INT32_MIN, 0x12345678, -0x12345678 };
    for (int32_t a : I32)
        for (int32_t b : I32)
        {
            V4 va = { uint32_t(a), uint32_t(b), uint32_t(a) ^ 0x5A5Au, uint32_t(b) + 7u }; // unsigned: no signed overflow
            V4 vb = { uint32_t(b), uint32_t(a), uint32_t(b) - 3u, uint32_t(a) };
            V4 mn, sub, avg, gtu, gts;
            for (int i = 0; i < 4; i++)
            {
                int32_t x = int32_t(va[i]), y = int32_t(vb[i]);
                mn[i] = uint32_t(x < y ? x : y);
                sub[i] = va[i] - vb[i];
                avg[i] = uint32_t(int32_t((int64_t(x) + int64_t(y) + 1) >> 1));
                gtu[i] = va[i] > vb[i] ? ~0u : 0;
                gts[i] = x > y ? ~0u : 0;
            }
            PPCContext ctx = NewContext();
            ctx.v4 = FromWords(va); ctx.v5 = FromWords(vb);
            vminsw_10000(ctx, base); CheckLanes("vminsw", Words(ctx.v3), mn);
            vsubuwm_10000(ctx, base); CheckLanes("vsubuwm", Words(ctx.v3), sub);
            vavgsw_10000(ctx, base); CheckLanes("vavgsw", Words(ctx.v3), avg);
            vcmpgtuw_10000(ctx, base); CheckLanes("vcmpgtuw", Words(ctx.v3), gtu);
            vcmpgtsw_10000(ctx, base); CheckLanes("vcmpgtsw", Words(ctx.v3), gts);
        }

    // Record forms: CR6 lt = all true, eq = all false.
    struct { V4 a, b; } rec[] = {
        { { 5, 6, 7, 8 }, { 1, 2, 3, 4 } },                  // all greater
        { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } },                  // none
        { { 0xFFFFFFFF, 0, 9, 9 }, { 1, 0, 3, 10 } },        // mixed; 0xFFFFFFFF > 1 unsigned only
        { { 0xFFFFFFFF, 0x80000000, 2, 3 }, { 1, 1, 1, 1 } } // all unsigned, two signed
    };
    for (auto& c : rec)
    {
        bool allU = true, noneU = true, allS = true, noneS = true;
        for (int i = 0; i < 4; i++)
        {
            bool u = c.a[i] > c.b[i], sg = int32_t(c.a[i]) > int32_t(c.b[i]);
            allU &= u; noneU &= !u; allS &= sg; noneS &= !sg;
        }
        PPCContext ctx = NewContext();
        ctx.v4 = FromWords(c.a); ctx.v5 = FromWords(c.b);
        vcmpgtuw_dot_10000(ctx, base);
        Check("vcmpgtuw.", "cr6.lt (all)", ctx.cr6.lt, uint8_t(allU));
        Check("vcmpgtuw.", "cr6.eq (none)", ctx.cr6.eq, uint8_t(noneU));
        vcmpgtsw_dot_10000(ctx, base);
        Check("vcmpgtsw.", "cr6.lt (all)", ctx.cr6.lt, uint8_t(allS));
        Check("vcmpgtsw.", "cr6.eq (none)", ctx.cr6.eq, uint8_t(noneS));
    }

    // vpkshss: saturating signed halfword -> signed byte; vA fills bytes 0-7, vB 8-15.
    {
        V8 a = { 0, 1, 127, 128, 0x7FFF, 0xFFFF, 0xFF80, 0xFF7F };   // 0,1,127,128,32767,-1,-128,-129
        V8 b = { 0x8000, 0xFFFE, 100, 0x00FF, 0x0100, 0xFF00, 5, 0xFFF0 };
        auto sat = [](uint16_t h) { int v = int16_t(h); return uint8_t(int8_t(v > 127 ? 127 : v < -128 ? -128 : v)); };
        V16 expected;
        for (int i = 0; i < 8; i++) { expected[i] = sat(a[i]); expected[8 + i] = sat(b[i]); }
        PPCContext ctx = NewContext();
        ctx.v4 = FromHalves(a); ctx.v5 = FromHalves(b);
        vpkshss_10000(ctx, base);
        CheckLanes("vpkshss", Bytes(ctx.v3), expected);
        ctx.v100 = FromHalves(a); ctx.v3 = FromHalves(b);
        vpkshss128_10000(ctx, base);
        CheckLanes("vpkshss128", Bytes(ctx.v70), expected);
    }

    // vslh / vsrh: per halfword, count = low 4 bits of vB's halfword.
    {
        V8 a = { 0x8001, 0x1234, 0xFFFF, 1, 0x00F0, 0x4000, 0x0101, 0xABCD };
        V8 b = { 1, 4, 15, 16, 17, 0, 0xFFF3, 8 };
        V8 sl, sr;
        for (int i = 0; i < 8; i++) { sl[i] = uint16_t(a[i] << (b[i] & 15)); sr[i] = uint16_t(a[i] >> (b[i] & 15)); }
        PPCContext ctx = NewContext();
        ctx.v4 = FromHalves(a); ctx.v5 = FromHalves(b);
        vslh_10000(ctx, base); CheckLanes("vslh", Halves(ctx.v3), sl);
        vsrh_10000(ctx, base); CheckLanes("vsrh", Halves(ctx.v3), sr);
    }

    // vrlw128 v11,v60,v58: per word rotate left by vB & 31.
    {
        V4 a = { 0x80000001, 0x12345678, 0xFFFF0000, 0xDEADBEEF };
        V4 b = { 1, 32, 31, 0x104 };
        V4 expected;
        for (int i = 0; i < 4; i++) expected[i] = Rotl32(a[i], int(b[i] & 31));
        PPCContext ctx = NewContext();
        ctx.v60 = FromWords(a); ctx.v58 = FromWords(b);
        vrlw128_10000(ctx, base);
        CheckLanes("vrlw128", Words(ctx.v11), expected);
    }
}

static V16 ShiftLeftBits(const V16& a, int n)
{
    V16 r{};
    for (int i = 0; i < 16; i++)
        r[i] = uint8_t((a[i] << n) | (i + 1 < 16 && n ? a[i + 1] >> (8 - n) : 0));
    return r;
}

static void TestVectorShifts()
{
    const V16 a = { 0x81, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xF0, 0x0F, 0x55, 0xAA, 0x01, 0x80, 0x7F, 0xFE };
    // vsl: by vB byte 15 & 7 bits (the ISA wants every byte equal; set them all)
    for (int n = 0; n < 8; n++)
    {
        V16 b; b.fill(uint8_t(n | 0xF8)); // high bits must be ignored
        PPCContext ctx = NewContext();
        ctx.v4 = FromBytes(a); ctx.v5 = FromBytes(b);
        vsl_10000(ctx, base);
        CheckLanes("vsl", Bytes(ctx.v3), ShiftLeftBits(a, n));
    }
    // vslo / vslo128: by (vB byte 15 >> 3) & 15 bytes
    for (int n : { 0, 1, 7, 8, 15 })
    {
        V16 b{}; b[15] = uint8_t((n << 3) | 0x87); // bit 7 and low 3 bits ignored
        V16 expected{};
        for (int i = 0; i + n < 16; i++) expected[i] = a[i + n];
        PPCContext ctx = NewContext();
        ctx.v4 = FromBytes(a); ctx.v5 = FromBytes(b);
        vslo_10000(ctx, base);
        CheckLanes("vslo", Bytes(ctx.v3), expected);
        ctx.v12 = FromBytes(a); ctx.v60 = FromBytes(b);
        vslo128_10000(ctx, base);
        CheckLanes("vslo128", Bytes(ctx.v54), expected);
    }
    // vsldoi128 v30,v0,v62,12 (the word that decoded as mullhwu.)
    {
        V16 b; for (int i = 0; i < 16; i++) b[i] = uint8_t(0x10 + i);
        V16 expected;
        for (int i = 0; i < 16; i++) expected[i] = i + 12 < 16 ? a[i + 12] : b[i + 12 - 16];
        PPCContext ctx = NewContext();
        ctx.v0 = FromBytes(a); ctx.v62 = FromBytes(b);
        vsldoi128_misdecode_10000(ctx, base);
        CheckLanes("vsldoi128 (was mullhwu.)", Bytes(ctx.v30), expected);
    }
}

static void TestD3D()
{
    // vupkd3d128 type 5 (FLOAT16_4): halfwords 4-7 -> x, y, z, w.
    const uint16_t halves[] = { 0x3C00, 0xC000, 0x7BFF, 0x0001, 0x8000, 0x7C00, 0xFC00, 0x7E00, 0x03FF, 0x0400, 0x3555, 0x0000 };
    for (size_t k = 0; k + 4 <= std::size(halves); k++)
    {
        V8 in = { 0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD, halves[k], halves[k + 1], halves[k + 2], halves[k + 3] };
        F4 expected = { HalfToFloat(in[4]), HalfToFloat(in[5]), HalfToFloat(in[6]), HalfToFloat(in[7]) };
        PPCContext ctx = NewContext();
        ctx.v0 = FromHalves(in);
        vupkd3d128_f16x4_10000(ctx, base);
        CheckLanes("vupkd3d128 float16_4", Floats(ctx.v63), expected);
    }
    // vupkd3d128 type 3 (FLOAT16_2): word 3 = x:y -> (x, y, 0, 1); vD == vB allowed
    for (size_t k = 0; k + 2 <= std::size(halves); k++)
    {
        V4 in = { 0x11111111, 0x22222222, 0x33333333, uint32_t(halves[k]) << 16 | halves[k + 1] };
        F4 expected = { HalfToFloat(halves[k]), HalfToFloat(halves[k + 1]), 0.0f, 1.0f };
        PPCContext ctx = NewContext();
        ctx.v62 = FromWords(in);
        vupkd3d128_f16x2_10000(ctx, base);
        CheckLanes("vupkd3d128 float16_2", Floats(ctx.v63), expected);
    }
    // vpkd3d128 type 3 (FLOAT16_2), pack 1, shift 3: word 0 := half(x):half(y), rest kept.
    const float floats[] = { 1.0f, -2.0f, 65504.0f, 65505.0f, -70000.0f, 0.1f, -0.0f, 6.0e-8f, 1.0e-10f,
        6.1035156e-5f, 3.0e-5f, INFINITY, -INFINITY, NAN, 0.33333334f, 1000.7f };
    for (size_t k = 0; k + 2 <= std::size(floats); k++)
    {
        F4 in = { floats[k], floats[k + 1], 7.0f, 8.0f };
        V4 old = { 0xDEADBEEF, 0x01234567, 0x89ABCDEF, 0x0F0F0F0F };
        V4 expected = old;
        expected[0] = uint32_t(FloatToHalfTrunc(in[0])) << 16 | FloatToHalfTrunc(in[1]);
        PPCContext ctx = NewContext();
        ctx.v63 = FromFloats(in); ctx.v64 = FromWords(old);
        vpkd3d128_f16x2_10000(ctx, base);
        CheckLanes("vpkd3d128 float16_2", Words(ctx.v64), expected);
    }
}

int main()
{
    TestLoadsStores();
    TestInteger();
    TestFloat();
    TestVectorInteger();
    TestVectorShifts();
    TestD3D();
    for (auto& [name, counts] : g_perTest)
        std::printf("%s %-30s %5d checks, %d failures\n", counts.second ? "FAIL" : "PASS", name.c_str(), counts.first, counts.second);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
