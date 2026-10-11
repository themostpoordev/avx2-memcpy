#include <immintrin.h>
#include <stddef.h>
void *cpy(void *d, const void *s, size_t n){
    __m256i a=_mm256_loadu_si256((const __m256i*)s);
    _mm256_storeu_si256((__m256i*)d,a);
    __m256i b=_mm256_stream_load_si256((const __m256i*)s);
    _mm256_stream_si256((__m256i*)d,b);
    _mm_sfence(); _mm_prefetch((const char*)s+64,_MM_HINT_T0);
    (void)n; return d;
}
