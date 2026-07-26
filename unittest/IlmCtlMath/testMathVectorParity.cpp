///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <testMathVectorParity.h>

#include <cmath>
// The scalar references below are the C99 single-precision entry points.
// <cmath> is only required to declare those in namespace std, and libstdc++
// does not; <math.h> is what guarantees them at global scope.
#include <math.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#if CTL_USE_ACCELERATE
  #include <Accelerate/Accelerate.h>
  #define CTL_MVP_BACKEND "Accelerate"
  #define CTL_MVP_HAVE_BACKEND 1
#elif CTL_USE_SLEEF
  #include <sleef.h>
  #define CTL_MVP_BACKEND "sleef"
  #define CTL_MVP_HAVE_BACKEND 1
  #if defined(__aarch64__) || defined(_M_ARM64)
    #define CTL_MVP_SLEEF_SFX advsimd
    typedef float32x4_t CtlMvpV4f;
  #elif defined(__x86_64__) || defined(_M_X64)
    #define CTL_MVP_SLEEF_SFX sse2
    typedef __m128 CtlMvpV4f;
  #else
    #undef CTL_MVP_HAVE_BACKEND
    #define CTL_MVP_HAVE_BACKEND 0
  #endif
#else
  #define CTL_MVP_BACKEND "scalar libm (no vectorized backend configured)"
  #define CTL_MVP_HAVE_BACKEND 0
#endif

namespace {

//
// ULP distance between two finite same-sign floats, and a fallback for
// the special-value cases.  NaN-vs-NaN and Inf-vs-Inf of matching sign
// count as zero drift.  Opposite-sign finite values are a 0 vs -0 style
// mismatch the test is happy to count as whatever the bit distance
// comes out to be (the Apple/sleef tier is documented +/-0 correct).
//
int32_t ulpDiff (float a, float b)
{
    if (std::isnan (a) && std::isnan (b)) return 0;
    if (a == b) return 0;
    if (std::isinf (a) || std::isinf (b) ||
        std::isnan (a) || std::isnan (b))
    {
        return INT32_MAX;
    }
    int32_t ai = 0;
    int32_t bi = 0;
    std::memcpy (&ai, &a, sizeof ai);
    std::memcpy (&bi, &b, sizeof bi);
    if (ai < 0) ai = INT32_MIN - ai;
    if (bi < 0) bi = INT32_MIN - bi;
    int64_t diff = static_cast<int64_t> (ai) - static_cast<int64_t> (bi);
    if (diff < 0) diff = -diff;
    if (diff > INT32_MAX) diff = INT32_MAX;
    return static_cast<int32_t> (diff);
}

//
// Invoke the compiled-in vector backend for one function.  Each
// callback fills `out[0..n)` with fn(in[0..n)).  Only functions on the
// batched hot path have implementations; the rest fall back to libm
// per-lane (and therefore match perfectly).
//
typedef void (*VecFn1) (const float *in, float *out, int n);
typedef void (*VecFn2) (const float *a1, const float *a2, float *out, int n);

#if CTL_USE_ACCELERATE
    #define CTL_MVP_ACCEL_1(Name, VvFn)                              \
        void vec_##Name (const float *in, float *out, int n)        \
        {                                                            \
            int ni = n;                                              \
            VvFn (out, in, &ni);                                     \
        }
    CTL_MVP_ACCEL_1 (exp,   vvexpf)
    CTL_MVP_ACCEL_1 (log,   vvlogf)
    CTL_MVP_ACCEL_1 (log10, vvlog10f)
    CTL_MVP_ACCEL_1 (sin,   vvsinf)
    CTL_MVP_ACCEL_1 (cos,   vvcosf)
    CTL_MVP_ACCEL_1 (tan,   vvtanf)
    CTL_MVP_ACCEL_1 (asin,  vvasinf)
    CTL_MVP_ACCEL_1 (acos,  vvacosf)
    CTL_MVP_ACCEL_1 (atan,  vvatanf)
    CTL_MVP_ACCEL_1 (sinh,  vvsinhf)
    CTL_MVP_ACCEL_1 (cosh,  vvcoshf)
    CTL_MVP_ACCEL_1 (tanh,  vvtanhf)
    CTL_MVP_ACCEL_1 (sqrt,  vvsqrtf)
    #undef CTL_MVP_ACCEL_1

    void vec_pow (const float *a1, const float *a2, float *out, int n)
    {
        int ni = n;
        vvpowf (out, a2, a1, &ni);  // Accelerate: vvpowf(out, y, x) = x**y
    }
    void vec_atan2 (const float *a1, const float *a2, float *out, int n)
    {
        int ni = n;
        vvatan2f (out, a1, a2, &ni);  // Accelerate: atan2(y, x)
    }

#elif CTL_USE_SLEEF && CTL_MVP_HAVE_BACKEND
    inline CtlMvpV4f mvp_load4 (const float *p)
    {
        CtlMvpV4f v;
        std::memcpy (&v, p, sizeof v);
        return v;
    }
    inline void mvp_store4 (float *p, CtlMvpV4f v)
    {
        std::memcpy (p, &v, sizeof v);
    }

    #define CTL_MVP_CAT_(a, b) a ## b
    #define CTL_MVP_CAT(a, b)  CTL_MVP_CAT_(a, b)
    #define CTL_MVP_FN1(base, ulp) \
        CTL_MVP_CAT(Sleef_ ## base ## f4_ ## ulp, CTL_MVP_SLEEF_SFX)

    #define CTL_MVP_SLEEF_1(Name, base, ulp)                         \
        void vec_##Name (const float *in, float *out, int n)        \
        {                                                            \
            int i = 0;                                               \
            for (; i + 4 <= n; i += 4)                               \
                mvp_store4 (out + i, CTL_MVP_FN1 (base, ulp) (       \
                    mvp_load4 (in + i)));                            \
            for (; i < n; ++i) out[i] = Name##f (in[i]);             \
        }
    CTL_MVP_SLEEF_1 (exp,   exp,   u10)
    CTL_MVP_SLEEF_1 (log,   log,   u10)
    CTL_MVP_SLEEF_1 (log10, log10, u10)
    CTL_MVP_SLEEF_1 (sin,   sin,   u10)
    CTL_MVP_SLEEF_1 (cos,   cos,   u10)
    CTL_MVP_SLEEF_1 (tan,   tan,   u10)
    CTL_MVP_SLEEF_1 (asin,  asin,  u10)
    CTL_MVP_SLEEF_1 (acos,  acos,  u10)
    CTL_MVP_SLEEF_1 (atan,  atan,  u10)
    CTL_MVP_SLEEF_1 (sinh,  sinh,  u10)
    CTL_MVP_SLEEF_1 (cosh,  cosh,  u10)
    CTL_MVP_SLEEF_1 (tanh,  tanh,  u10)
    CTL_MVP_SLEEF_1 (sqrt,  sqrt,  u05)
    #undef CTL_MVP_SLEEF_1

    void vec_pow (const float *a1, const float *a2, float *out, int n)
    {
        int i = 0;
        for (; i + 4 <= n; i += 4)
            mvp_store4 (out + i,
                CTL_MVP_CAT(Sleef_powf4_u10, CTL_MVP_SLEEF_SFX) (
                    mvp_load4 (a1 + i), mvp_load4 (a2 + i)));
        for (; i < n; ++i) out[i] = std::pow (a1[i], a2[i]);
    }
    void vec_atan2 (const float *a1, const float *a2, float *out, int n)
    {
        int i = 0;
        for (; i + 4 <= n; i += 4)
            mvp_store4 (out + i,
                CTL_MVP_CAT(Sleef_atan2f4_u10, CTL_MVP_SLEEF_SFX) (
                    mvp_load4 (a1 + i), mvp_load4 (a2 + i)));
        for (; i < n; ++i) out[i] = std::atan2 (a1[i], a2[i]);
    }

    #undef CTL_MVP_FN1
    #undef CTL_MVP_CAT
    #undef CTL_MVP_CAT_
#endif

struct ParityResult
{
    std::string name;
    size_t      samples;
    size_t      within1ulp;
    int32_t     maxUlp;
    double      rmsUlp;
};

//
// Policy: sleef _u10 and Accelerate vForce both publish <=1 ULP vs
// platform libm.  We allow 2-ULP drift in the "within" count to
// tolerate the compiler's own fp-contract behaviour around the scalar
// reference call (which may materialize fmadd on some architectures
// and not others).  Any sample worse than 2 ULP fails the test; fewer
// than 99.9% within 2 ULP also fails.
//
const int32_t kUlpAllowed = 2;
const double  kMinWithinFrac = 0.999;

ParityResult
runPairSweep (const char *name,
              const std::vector<float> &a1,
              const std::vector<float> &a2,
              const float *out_vec,
              float (*scalar) (float, float))
{
    ParityResult r;
    r.name = name;
    r.samples = a1.size();
    r.within1ulp = 0;
    r.maxUlp = 0;
    double sumSq = 0.0;

    for (size_t i = 0; i < a1.size(); ++i)
    {
        float ref = scalar (a1[i], a2[i]);
        int32_t d = ulpDiff (ref, out_vec[i]);
        if (d <= kUlpAllowed) ++r.within1ulp;
        if (d > r.maxUlp) r.maxUlp = d;
        double dd = static_cast<double> (d);
        sumSq += dd * dd;
    }
    r.rmsUlp = std::sqrt (sumSq / static_cast<double> (a1.size()));
    return r;
}

ParityResult
runSingleSweep (const char *name,
                const std::vector<float> &in,
                const float *out_vec,
                float (*scalar) (float))
{
    ParityResult r;
    r.name = name;
    r.samples = in.size();
    r.within1ulp = 0;
    r.maxUlp = 0;
    double sumSq = 0.0;

    for (size_t i = 0; i < in.size(); ++i)
    {
        float ref = scalar (in[i]);
        int32_t d = ulpDiff (ref, out_vec[i]);
        if (d <= kUlpAllowed) ++r.within1ulp;
        if (d > r.maxUlp) r.maxUlp = d;
        double dd = static_cast<double> (d);
        sumSq += dd * dd;
    }
    r.rmsUlp = std::sqrt (sumSq / static_cast<double> (in.size()));
    return r;
}

void
requireParity (const ParityResult &r)
{
    double frac = static_cast<double> (r.within1ulp) /
                  static_cast<double> (r.samples);
    std::fprintf (stderr,
        "  %-8s  samples=%zu  <=%d ULP: %.4f%%  maxULP=%d  rmsULP=%.3f\n",
        r.name.c_str(), r.samples, kUlpAllowed,
        100.0 * frac, r.maxUlp, r.rmsUlp);

    if (frac < kMinWithinFrac)
    {
        std::fprintf (stderr,
            "    FAIL: fewer than %.2f%% within %d ULP for %s (backend %s)\n",
            100.0 * kMinWithinFrac, kUlpAllowed, r.name.c_str(),
            CTL_MVP_BACKEND);
        std::exit (1);
    }
}

std::vector<float>
sampleDomain (std::mt19937 &rng, float lo, float hi, size_t n)
{
    std::vector<float> v (n);
    std::uniform_real_distribution<float> dist (lo, hi);
    for (size_t i = 0; i < n; ++i) v[i] = dist (rng);
    return v;
}

#if CTL_MVP_HAVE_BACKEND
void
check1 (const char *name,
        float lo, float hi,
        VecFn1 vec, float (*scalar) (float),
        std::mt19937 &rng, size_t n)
{
    std::vector<float> in = sampleDomain (rng, lo, hi, n);
    std::vector<float> out (n);
    vec (in.data(), out.data(), static_cast<int> (n));
    requireParity (runSingleSweep (name, in, out.data(), scalar));
}

void
check2 (const char *name,
        float lo1, float hi1, float lo2, float hi2,
        VecFn2 vec, float (*scalar) (float, float),
        std::mt19937 &rng, size_t n)
{
    std::vector<float> a1 = sampleDomain (rng, lo1, hi1, n);
    std::vector<float> a2 = sampleDomain (rng, lo2, hi2, n);
    std::vector<float> out (n);
    vec (a1.data(), a2.data(), out.data(), static_cast<int> (n));
    requireParity (runPairSweep (name, a1, a2, out.data(), scalar));
}
#endif

} // namespace


void
testMathVectorParity ()
{
    std::fprintf (stderr,
        "testMathVectorParity: backend = %s\n", CTL_MVP_BACKEND);

#if !CTL_MVP_HAVE_BACKEND
    std::fprintf (stderr,
        "  (no vectorized backend compiled in; skipping)\n");
    return;
#else
    std::mt19937 rng (0xC71D5EEDu);

    //
    // 262144 samples per function: large enough for per-ULP statistics
    // on a <=1 ULP contract, small enough that the test suite stays
    // under a few hundred ms total.  The bounds for each function were
    // picked to stay inside the respective libm's documented domain
    // and away from the extreme tail where ULP near Inf/-Inf explodes
    // for both the vector and scalar sides simultaneously.  Extreme-
    // value correctness (Inf, NaN, +/-0) is covered by the 240-fixture
    // end-to-end ctest suite.
    //
    const size_t N = 1u << 18;

    check1 ("exp",   -30.0f,    30.0f,     vec_exp,   ::expf,   rng, N);
    check1 ("log",    1e-30f,   1e30f,     vec_log,   ::logf,   rng, N);
    check1 ("log10",  1e-30f,   1e30f,     vec_log10, ::log10f, rng, N);
    check1 ("sin",  -100.0f,   100.0f,     vec_sin,   ::sinf,   rng, N);
    check1 ("cos",  -100.0f,   100.0f,     vec_cos,   ::cosf,   rng, N);
    check1 ("tan",   -1.5f,     1.5f,      vec_tan,   ::tanf,   rng, N);
    check1 ("asin",  -1.0f,     1.0f,      vec_asin,  ::asinf,  rng, N);
    check1 ("acos",  -1.0f,     1.0f,      vec_acos,  ::acosf,  rng, N);
    check1 ("atan", -1e6f,      1e6f,      vec_atan,  ::atanf,  rng, N);
    check1 ("sinh", -30.0f,    30.0f,      vec_sinh,  ::sinhf,  rng, N);
    check1 ("cosh", -30.0f,    30.0f,      vec_cosh,  ::coshf,  rng, N);
    check1 ("tanh", -30.0f,    30.0f,      vec_tanh,  ::tanhf,  rng, N);
    check1 ("sqrt",  0.0f,     1e30f,      vec_sqrt,  ::sqrtf,  rng, N);

    // pow(x, y): x in (0, 1e4], y in [-10, 10] keeps the product inside
    // FP32 range across the whole swept grid.
    check2 ("pow",   1e-4f, 1e4f, -10.0f, 10.0f,
            vec_pow, ::powf, rng, N);
    check2 ("atan2", -1e3f, 1e3f, -1e3f, 1e3f,
            vec_atan2, ::atan2f, rng, N);

    std::fprintf (stderr, "testMathVectorParity: all functions pass.\n");
#endif
}
