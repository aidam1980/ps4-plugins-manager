#ifndef PS4PE_PNG_DECODE_H
#define PS4PE_PNG_DECODE_H

// Small self-contained PNG decoder for common PS4 title icons (8-bit, non-interlaced PNG).
// Supports grayscale, grayscale+alpha, RGB and RGBA. It deliberately does not depend on zlib.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PngImage
{
    int width;
    int height;
    unsigned char *rgba;
};

namespace ps4pe_png
{
struct Bits
{
    const unsigned char *data;
    size_t size;
    size_t bitPos;
};

static unsigned readBits(Bits &b, int count)
{
    unsigned value = 0;
    for (int i = 0; i < count; ++i)
    {
        if (b.bitPos >= b.size * 8)
            return 0xFFFFFFFFu;
        value |= (unsigned)((b.data[b.bitPos >> 3] >> (b.bitPos & 7)) & 1u) << i;
        ++b.bitPos;
    }
    return value;
}

static void alignByte(Bits &b)
{
    b.bitPos = (b.bitPos + 7u) & ~((size_t)7u);
}

struct Huffman
{
    int count[16];
    int symbols[320];
    int symbolCount;
};

static bool buildHuffman(Huffman &h, const unsigned char *lengths, int n)
{
    memset(&h, 0, sizeof(h));
    for (int i = 0; i < n; ++i)
    {
        if (lengths[i] > 15)
            return false;
        if (lengths[i])
            ++h.count[lengths[i]];
    }

    int total = 0;
    for (int len = 1; len <= 15; ++len)
        total += h.count[len];
    if (total <= 0 || total > (int)(sizeof(h.symbols) / sizeof(h.symbols[0])))
        return false;
    h.symbolCount = total;

    int offsets[16];
    offsets[1] = 0;
    for (int len = 1; len < 15; ++len)
        offsets[len + 1] = offsets[len] + h.count[len];
    for (int sym = 0; sym < n; ++sym)
        if (lengths[sym])
            h.symbols[offsets[lengths[sym]]++] = sym;
    return true;
}

static int decodeSymbol(Bits &b, const Huffman &h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 15; ++len)
    {
        unsigned bit = readBits(b, 1);
        if (bit == 0xFFFFFFFFu)
            return -1;
        code |= (int)bit;
        int count = h.count[len];
        if (code >= first && code < first + count)
        {
            int at = index + code - first;
            if (at < 0 || at >= h.symbolCount)
                return -1;
            return h.symbols[at];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static bool inflateZlib(const unsigned char *src, size_t srcSize,
                        unsigned char *out, size_t outCapacity, size_t *outSize)
{
    if (!src || srcSize < 6 || !out || !outSize)
        return false;
    // The first two bytes are the zlib header and the last four are Adler-32.
    Bits b;
    b.data = src + 2;
    b.size = srcSize - 6;
    b.bitPos = 0;
    size_t used = 0;
    bool finalBlock = false;

    static const int lengthBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int lengthExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int distanceBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int distanceExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    size_t blockCount = 0;
    while (!finalBlock)
    {
        // A valid PNG stream needs very few DEFLATE blocks for the supported
        // icon sizes. Fail closed rather than hanging on a malformed stream.
        if (++blockCount > 65536) return false;
        unsigned finalBit = readBits(b, 1);
        unsigned blockType = readBits(b, 2);
        if (finalBit == 0xFFFFFFFFu || blockType == 0xFFFFFFFFu || blockType == 3)
            return false;
        finalBlock = finalBit != 0;

        if (blockType == 0)
        {
            alignByte(b);
            unsigned len = readBits(b, 16);
            unsigned nlen = readBits(b, 16);
            if (len == 0xFFFFFFFFu || nlen == 0xFFFFFFFFu || ((len ^ 0xFFFFu) & 0xFFFFu) != nlen)
                return false;
            if (b.bitPos + (size_t)len * 8u > b.size * 8u || used + len > outCapacity)
                return false;
            for (unsigned i = 0; i < len; ++i)
                out[used++] = (unsigned char)readBits(b, 8);
            continue;
        }

        unsigned char litLengths[288];
        unsigned char distLengths[32];
        Huffman litTree;
        Huffman distTree;

        if (blockType == 1)
        {
            for (int i = 0; i <= 287; ++i)
            {
                if (i <= 143) litLengths[i] = 8;
                else if (i <= 255) litLengths[i] = 9;
                else if (i <= 279) litLengths[i] = 7;
                else litLengths[i] = 8;
            }
            for (int i = 0; i < 32; ++i)
                distLengths[i] = 5;
            if (!buildHuffman(litTree, litLengths, 288) || !buildHuffman(distTree, distLengths, 32))
                return false;
        }
        else
        {
            unsigned hlit = readBits(b, 5) + 257;
            unsigned hdist = readBits(b, 5) + 1;
            unsigned hclen = readBits(b, 4) + 4;
            if (hlit > 286 || hdist > 32 || hclen > 19)
                return false;
            static const unsigned char order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            unsigned char codeLengths[19];
            memset(codeLengths, 0, sizeof(codeLengths));
            for (unsigned i = 0; i < hclen; ++i)
            {
                unsigned v = readBits(b, 3);
                if (v == 0xFFFFFFFFu)
                    return false;
                codeLengths[order[i]] = (unsigned char)v;
            }
            Huffman codeTree;
            if (!buildHuffman(codeTree, codeLengths, 19))
                return false;

            unsigned char allLengths[320];
            unsigned total = hlit + hdist;
            unsigned got = 0;
            while (got < total)
            {
                int sym = decodeSymbol(b, codeTree);
                if (sym < 0)
                    return false;
                if (sym <= 15)
                    allLengths[got++] = (unsigned char)sym;
                else if (sym == 16)
                {
                    if (got == 0)
                        return false;
                    unsigned repeat = readBits(b, 2) + 3;
                    if (repeat == 0xFFFFFFFFu || got + repeat > total)
                        return false;
                    unsigned char prior = allLengths[got - 1];
                    while (repeat--)
                        allLengths[got++] = prior;
                }
                else if (sym == 17)
                {
                    unsigned repeat = readBits(b, 3) + 3;
                    if (repeat == 0xFFFFFFFFu || got + repeat > total)
                        return false;
                    while (repeat--)
                        allLengths[got++] = 0;
                }
                else if (sym == 18)
                {
                    unsigned repeat = readBits(b, 7) + 11;
                    if (repeat == 0xFFFFFFFFu || got + repeat > total)
                        return false;
                    while (repeat--)
                        allLengths[got++] = 0;
                }
                else
                    return false;
            }
            if (allLengths[256] == 0)
                return false;
            if (!buildHuffman(litTree, allLengths, (int)hlit) ||
                !buildHuffman(distTree, allLengths + hlit, (int)hdist))
                return false;
        }

        size_t symbolsDecoded = 0;
        for (;;)
        {
            if (++symbolsDecoded > outCapacity + 1) return false;
            int sym = decodeSymbol(b, litTree);
            if (sym < 0)
                return false;
            if (sym < 256)
            {
                if (used >= outCapacity)
                    return false;
                out[used++] = (unsigned char)sym;
            }
            else if (sym == 256)
                break;
            else
            {
                if (sym < 257 || sym > 285)
                    return false;
                int li = sym - 257;
                unsigned extraLen = (unsigned)lengthExtra[li] ? readBits(b, lengthExtra[li]) : 0;
                if (extraLen == 0xFFFFFFFFu)
                    return false;
                size_t length = (size_t)lengthBase[li] + extraLen;
                int dsym = decodeSymbol(b, distTree);
                if (dsym < 0 || dsym > 29)
                    return false;
                unsigned extraDist = (unsigned)distanceExtra[dsym] ? readBits(b, distanceExtra[dsym]) : 0;
                if (extraDist == 0xFFFFFFFFu)
                    return false;
                size_t distance = (size_t)distanceBase[dsym] + extraDist;
                if (distance == 0 || distance > used || used + length > outCapacity)
                    return false;
                for (size_t i = 0; i < length; ++i)
                {
                    out[used] = out[used - distance];
                    ++used;
                }
            }
        }
    }
    *outSize = used;
    return true;
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned char paeth(unsigned char a, unsigned char b, unsigned char c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a), pb = abs(p - (int)b), pc = abs(p - (int)c);
    return (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
}

static bool load(const char *path, PngImage *image)
{
    if (!path || !image)
        return false;
    image->width = image->height = 0;
    image->rgba = NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long fileLen = ftell(f);
    if (fileLen < 33 || fileLen > 8 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }
    unsigned char *file = (unsigned char *)malloc((size_t)fileLen);
    if (!file)
    {
        fclose(f);
        return false;
    }
    size_t readLen = fread(file, 1, (size_t)fileLen, f);
    fclose(f);
    if (readLen != (size_t)fileLen || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0)
    {
        free(file);
        return false;
    }

    uint32_t w = 0, h = 0;
    unsigned char bitDepth = 0, colorType = 0, interlace = 0;
    unsigned char *idat = NULL;
    size_t idatSize = 0, idatCapacity = 0;
    unsigned char palette[768] = {};
    unsigned char paletteAlpha[256];
    memset(paletteAlpha, 255, sizeof(paletteAlpha));
    size_t paletteEntries = 0, paletteAlphaCount = 0;
    size_t pos = 8;
    bool gotHeader = false, gotEnd = false;
    while (pos + 12 <= (size_t)fileLen)
    {
        uint32_t length = be32(file + pos);
        const unsigned char *type = file + pos + 4;
        pos += 8;
        if ((size_t)length > (size_t)fileLen - pos - 4)
            break;
        const unsigned char *data = file + pos;
        if (memcmp(type, "IHDR", 4) == 0 && length >= 13)
        {
            w = be32(data);
            h = be32(data + 4);
            bitDepth = data[8];
            colorType = data[9];
            interlace = data[12];
            gotHeader = true;
        }
        else if (memcmp(type, "PLTE", 4) == 0 && length >= 3 && length <= sizeof(palette) && length % 3 == 0)
        {
            memcpy(palette, data, length);
            paletteEntries = length / 3;
        }
        else if (memcmp(type, "tRNS", 4) == 0 && length <= sizeof(paletteAlpha))
        {
            memcpy(paletteAlpha, data, length);
            paletteAlphaCount = length;
        }
        else if (memcmp(type, "IDAT", 4) == 0)
        {
            if (idatSize + (size_t)length > 8 * 1024 * 1024)
                break;
            if (idatSize + (size_t)length > idatCapacity)
            {
                size_t cap = idatCapacity ? idatCapacity : 4096;
                while (cap < idatSize + (size_t)length)
                    cap *= 2;
                unsigned char *next = (unsigned char *)realloc(idat, cap);
                if (!next)
                    break;
                idat = next;
                idatCapacity = cap;
            }
            memcpy(idat + idatSize, data, length);
            idatSize += length;
        }
        else if (memcmp(type, "IEND", 4) == 0)
            gotEnd = true;
        pos += (size_t)length + 4; // skip chunk CRC
        if (gotEnd)
            break;
    }

    bool ok = false;
    int channels = colorType == 2 ? 3 : (colorType == 6 ? 4 : (colorType == 0 || colorType == 3 ? 1 : (colorType == 4 ? 2 : 0)));
    bool supportedColor = colorType == 0 || colorType == 2 || colorType == 3 || colorType == 4 || colorType == 6;
    bool paletteValid = colorType != 3 || paletteEntries > 0;
    if (gotHeader && gotEnd && idat && idatSize >= 6 && w > 0 && h > 0 &&
        w <= 1024 && h <= 1024 && bitDepth == 8 && interlace == 0 && channels > 0 && supportedColor && paletteValid)
    {
        size_t rowBytes = (size_t)w * (size_t)channels;
        size_t rawSize = (rowBytes + 1) * (size_t)h;
        if (rawSize <= 8 * 1024 * 1024)
        {
            unsigned char *raw = (unsigned char *)malloc(rawSize);
            unsigned char *rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
            size_t inflated = 0;
            if (raw && rgba && inflateZlib(idat, idatSize, raw, rawSize, &inflated) && inflated >= rawSize)
            {
                bool validFilters = true;
                for (uint32_t y = 0; y < h; ++y)
                {
                    unsigned char *row = raw + (rowBytes + 1) * y + 1;
                    unsigned char *prior = y ? (raw + (rowBytes + 1) * (y - 1) + 1) : NULL;
                    unsigned char filter = row[-1];
                    if (filter > 4) { validFilters = false; break; }
                    for (size_t x = 0; x < rowBytes; ++x)
                    {
                        unsigned char left = x >= (size_t)channels ? row[x - channels] : 0;
                        unsigned char up = prior ? prior[x] : 0;
                        unsigned char upperLeft = (prior && x >= (size_t)channels) ? prior[x - channels] : 0;
                        if (filter == 1) row[x] = (unsigned char)(row[x] + left);
                        else if (filter == 2) row[x] = (unsigned char)(row[x] + up);
                        else if (filter == 3) row[x] = (unsigned char)(row[x] + ((unsigned)left + (unsigned)up) / 2u);
                        else if (filter == 4) row[x] = (unsigned char)(row[x] + paeth(left, up, upperLeft));
                    }
                    unsigned char *dst = rgba + (size_t)y * w * 4u;
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        if (colorType == 2)
                        {
                            dst[x*4+0] = row[x*3+0]; dst[x*4+1] = row[x*3+1]; dst[x*4+2] = row[x*3+2]; dst[x*4+3] = 255;
                        }
                        else if (colorType == 3)
                        {
                            unsigned int paletteIndex = row[x];
                            if (paletteIndex >= paletteEntries) { validFilters = false; break; }
                            dst[x*4+0] = palette[paletteIndex * 3];
                            dst[x*4+1] = palette[paletteIndex * 3 + 1];
                            dst[x*4+2] = palette[paletteIndex * 3 + 2];
                            dst[x*4+3] = paletteIndex < paletteAlphaCount ? paletteAlpha[paletteIndex] : 255;
                        }
                        else if (colorType == 6)
                        {
                            dst[x*4+0] = row[x*4+0]; dst[x*4+1] = row[x*4+1]; dst[x*4+2] = row[x*4+2]; dst[x*4+3] = row[x*4+3];
                        }
                        else if (colorType == 0)
                        {
                            dst[x*4+0] = dst[x*4+1] = dst[x*4+2] = row[x]; dst[x*4+3] = 255;
                        }
                        else
                        {
                            dst[x*4+0] = dst[x*4+1] = dst[x*4+2] = row[x*2+0]; dst[x*4+3] = row[x*2+1];
                        }
                    }
                }
                if (validFilters)
                {
                    image->width = (int)w;
                    image->height = (int)h;
                    image->rgba = rgba;
                    rgba = NULL;
                    ok = true;
                }
            }
            if (rgba) free(rgba);
            if (raw) free(raw);
        }
    }
    if (idat) free(idat);
    free(file);
    return ok;
}
}

static bool LoadPngRGBA(const char *path, PngImage *image)
{
    return ps4pe_png::load(path, image);
}

static void FreePngImage(PngImage *image)
{
    if (!image) return;
    if (image->rgba) free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

#endif
