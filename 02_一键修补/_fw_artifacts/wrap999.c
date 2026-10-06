/* 用完整 LZO 库（不是 minilzo），因为需要 LZO1X-999 压缩器。
 * LZO1X-999 是原固件用的压缩等级，压出来的块大小和原版接近，
 * 这样重打包后 zImage 不会变大（kernel 分区里 zImage 后面紧跟着 DTB，不能变大）。 */
#include <lzo/lzo1x.h>

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

__declspec(dllexport) int lzo_enc(const unsigned char *src, unsigned int slen,
                                  unsigned char *dst, unsigned int *dlen)
{
    static unsigned char wk[LZO1X_1_MEM_COMPRESS];
    lzo_uint out = (lzo_uint)*dlen;
    int r = lzo1x_1_compress((const lzo_bytep)src, (lzo_uint)slen,
                             (lzo_bytep)dst, &out, wk);
    *dlen = (unsigned int)out;
    return r;
}

/* ★ LZO1X-999（原固件用的等级） */
__declspec(dllexport) int lzo_enc999(const unsigned char *src, unsigned int slen,
                                     unsigned char *dst, unsigned int *dlen)
{
    static unsigned char wk[LZO1X_999_MEM_COMPRESS];
    lzo_uint out = (lzo_uint)*dlen;
    int r = lzo1x_999_compress((const lzo_bytep)src, (lzo_uint)slen,
                               (lzo_bytep)dst, &out, wk);
    *dlen = (unsigned int)out;
    return r;
}

/* 指定等级的 999 压缩，方便试 level 7/8/9 */
__declspec(dllexport) int lzo_enc999_level(const unsigned char *src, unsigned int slen,
                                           unsigned char *dst, unsigned int *dlen,
                                           int level)
{
    static unsigned char wk[LZO1X_999_MEM_COMPRESS];
    lzo_uint out = (lzo_uint)*dlen;
    int r = lzo1x_999_compress_level((const lzo_bytep)src, (lzo_uint)slen,
                                     (lzo_bytep)dst, &out, wk,
                                     NULL, 0, 0, level);
    *dlen = (unsigned int)out;
    return r;
}

__declspec(dllexport) unsigned int lzo_ver(void)
{
    return (unsigned int)lzo_version();
}
