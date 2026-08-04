/* config.h stub for i586 Win9x build.
   Bun's DirectBuild compiles libwebp without autotools.
   SSE2 is the only SIMD ISA available on the i586 target.
   SSE4.1/AVX2/NEON etc. are intentionally absent - the target
   (-march=pentium4) does not support them, and cpu.h's _MSC_VER
   detection would incorrectly auto-enable them via WEBP_MSC_SSE41
   and WEBP_MSC_AVX2 under clang-cl. */
#define WEBP_HAVE_SSE2 1
