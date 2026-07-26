# Testing the CTL performance branches

Three branches stack as fast-forwards.  **Unless you have a specific
reason to isolate a subset, check out the top branch
(`ship/moduleTestFramework-v1`) and test everything from there.**

```
ship/moduleTestFramework-v1      recommended, all the below + module test framework
ship/gpu-metal                   everything in cpu-perf plus the Apple Silicon Metal GPU backend
ship/cpu-perf                    this branch: CPU performance work only
master                           pre-branch baseline
```

Each branch is a proper superset of the one below, so:

    git checkout ship/moduleTestFramework-v1

gives you everything.  The subset branches exist for downstream
integrators who want to review or adopt the work in smaller pieces.

---

## If you're on this branch (`ship/cpu-perf`)

This branch contains CPU-side performance work only: vectorized
stdlib transcendentals (Apple Accelerate or sleef), batched
broadcast for two-arg math when one argument is uniform, contiguous
fast path for whole-array assignment, threaded tile dispatch,
file-level `-jobs`, host-native arch + thin LTO + hidden visibility
+ profile-guided optimization, a `-pixel` CLI mode for single-pixel
transforms, and the `-no-batch-math` runtime toggle described
below.  No Metal GPU backend.

### Reference math via `-no-batch-math`

`ctlrender -no-batch-math ...` makes the SIMD interpreter skip the
Accelerate/SLEEF batched transcendental dispatch and route every
stdlib math call through the per-element scalar libm path instead.
Useful as a same-binary reference baseline when diffing against the
optimized path:

```bash
./build/ctlrender/ctlrender                 -ctl <your.ctl> in.exr ref.tiff      # batched math (default)
./build/ctlrender/ctlrender -no-batch-math  -ctl <your.ctl> in.exr scalar.tiff   # scalar libm path
```

The flag is parsed but warns-and-ignores on a Metal-backend build,
since it only affects CPU SIMD dispatch.  This is the runtime
counterpart of the compile-time `-DCTL_USE_ACCELERATE=OFF
-DCTL_USE_SLEEF=OFF` opt-out -- same effect, no rebuild.

### Build and test

```bash
git clone <your-fork>/CTL.git && cd CTL
git checkout ship/cpu-perf
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && ctest)                                   # should be green
```

### Run from the build tree -- do not `make install`

Test against your own workloads using the binary in the build
directory directly:

```bash
./build/ctlrender/ctlrender -ctl <your.ctl> in.exr out.exr
```

`make install` is not necessary and is not recommended while you're
evaluating; it would place the binary in system directories
(`/usr/local/bin/...`) where it could shadow a production install.
Keep everything inside your checkout.

### Quick single-pixel check

```bash
./build/ctlrender/ctlrender -ctl <your.ctl> -pixel 0.5,0.25,0.1,1.0
./build/ctlrender/ctlrender -help pixel
```

### Opt-outs if you hit a problem

```bash
-DCTL_PGO=OFF                # disable profile-guided optimization
-DCTL_NATIVE_ARCH=OFF        # portable-arch build (older hardware)
-DCTL_LTO=OFF                # disable thin LTO
-DCTL_HIDDEN_VISIBILITY=OFF  # if you're building SHARED libs for an external ABI
-DCTL_USE_ACCELERATE=OFF -DCTL_USE_SLEEF=OFF   # scalar libm at compile time
```

For an A/B without a rebuild, pass `-no-batch-math` on the
command line -- same effect as the `-DCTL_USE_*=OFF` build flags
but selectable per invocation.

### Reporting results

Please include:

```bash
/usr/bin/time -p ./build/ctlrender/ctlrender -ctl ... in.exr out.exr
```

plus your hardware (CPU model), input image size + format, CTL
transform used, and the measured wall-time.  File an issue at
`<your-repo>/issues` with the numbers.

### Known limits

- Builds on every tier-1 target (Linux, Windows, macOS Intel, macOS
  Apple Silicon).
- Bit-exact against pre-branch master on the default build (no
  Accelerate / sleef), and bit-exact again at runtime under
  `-no-batch-math`.
- <=1 ULP per transcendental on the vectorized path.
