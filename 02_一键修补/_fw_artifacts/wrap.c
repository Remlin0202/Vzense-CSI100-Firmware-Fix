/* ctypes-facing wrapper around miniLZO (LZO 2.10).
 * 解压用 lzo1x_decompress_safe；压缩用 lzo1x_1_compress（LZO1X-1）。
 * 两者产生的/接受的位流格式相同，可以和 LZO1X-999 的数据混在同一条 lzop 流里。 */
#include "minilzo.h"

__declspec(dllexport) int lzo_init_w(void)
{
    return lzo_init();
}

__declspec(dllexport) int lzo_dec(const unsigned char *src, unsigned int slen,
                                  unsigned char *dst, unsigned int *dlen)
{
    lzo_uint out = (lzo_uint)*dlen;
    int r = lzo1x_decompress_safe((const lzo_bytep)src, (lzo_uint)slen,
                                  (lzo_bytep)dst, &out, NULL);
    *dlen = (unsigned int)out;
    return r;
}

/* 压缩。dst 必须至少有 slen + slen/16 + 64 + 3 字节 */
__declspec(dllexport) int lzo_enc(const unsigned char *src, unsigned int slen,
                                  unsigned char *dst, unsigned int *dlen)
{
    static unsigned char wrkmem[LZO1X_1_MEM_COMPRESS];
    lzo_uint out = (lzo_uint)*dlen;
    int r = lzo1x_1_compress((const lzo_bytep)src, (lzo_uint)slen,
                             (lzo_bytep)dst, &out, wrkmem);
    *dlen = (unsigned int)out;
    return r;
}

__declspec(dllexport) unsigned int lzo_ver(void)
{
    return (unsigned int)lzo_version();
}
