#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <new>

#include <string>

#include "graphics.h"
#include "log.h"
#include "ps4plugineditor_builtin_font.h"

Scene2D::Scene2D(int w, int h, int pixelDepth)
#ifdef GRAPHICS_USES_FONT
    : ftLib(nullptr),
#else
    :
#endif
      width(w),
      height(h),
      depth(pixelDepth),
      video(-1),
      directMemOff(0),
      directMemAllocationSize(0),
      videoMemSP(0),
      videoMem(nullptr),
      frameBuffers(nullptr),
      flipQueue(0),
      frameBufferSize(w * h * pixelDepth),
      frameBufferCount(0),
      activeFrameBufferIdx(0),
      buffersRegistered(false)
{
}

Scene2D::~Scene2D()
{
    // The render loop waits for every submitted flip before it exits, so no
    // frame should still be in use when this destructor runs.
    printf("[EXIT] Scene2D: begin cleanup\n");

    // Stop the display from referencing our frame buffers before freeing them.
    if (this->video >= 0 && this->buffersRegistered)
    {
        printf("[EXIT] Scene2D: unregister buffers\n");
        const int rc = sceVideoOutUnregisterBuffers(this->video, 0);
        printf("[EXIT] Scene2D: unregister buffers rc=%d\n", rc);
        this->buffersRegistered = false;
    }

    // OpenOrbis currently exposes sceVideoOutDeleteFlipEvent only as an
    // unprototyped placeholder (void, no parameters). Do not call that
    // uncertain entry point. Closing the event queue removes its registrations.
    if (this->flipQueue != 0)
    {
        printf("[EXIT] Scene2D: delete flip queue\n");
        const int rc = sceKernelDeleteEqueue(this->flipQueue);
        printf("[EXIT] Scene2D: delete flip queue rc=%d\n", rc);
        this->flipQueue = 0;
    }

    if (this->video >= 0)
    {
        printf("[EXIT] Scene2D: close video\n");
        const int rc = sceVideoOutClose(this->video);
        printf("[EXIT] Scene2D: close video rc=%d\n", rc);
        this->video = -1;
    }

#ifdef GRAPHICS_USES_FONT
    if (this->ftLib)
    {
        printf("[EXIT] Scene2D: shutdown FreeType\n");
        FT_Done_FreeType(this->ftLib);
        this->ftLib = nullptr;
        printf("[EXIT] Scene2D: FreeType stopped\n");
    }
#endif

    printf("[EXIT] Scene2D: release video memory\n");
    deallocateVideoMem();
    printf("[EXIT] Scene2D: cleanup complete\n");
}

bool Scene2D::Init(size_t memSize, int numFrameBuffers)
{
    if (numFrameBuffers < 1 || this->width <= 0 || this->height <= 0 || this->depth <= 0)
    {
        DEBUGLOG << "Invalid graphics initialization parameters";
        return false;
    }

    this->frameBufferCount = numFrameBuffers;
    this->activeFrameBufferIdx = 0;
    this->videoMem = nullptr;
    this->frameBuffers = nullptr;

    const size_t requiredBufferMemory = (size_t)this->frameBufferSize * (size_t)numFrameBuffers;
    if (this->frameBufferSize <= 0 || requiredBufferMemory > memSize)
    {
        DEBUGLOG << "Video memory size is too small for frame buffers";
        return false;
    }

    int rc = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    this->video = rc;
    if (this->video < 0)
    {
        DEBUGLOG << "Failed to open a video out handle: " << std::string(strerror(errno));
        return false;
    }

#ifdef GRAPHICS_USES_FONT
    rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL);
    if (rc < 0)
    {
        DEBUGLOG << "Failed to load freetype: " << std::string(strerror(errno));
        return false;
    }

    rc = FT_Init_FreeType(&this->ftLib);
    if (rc != 0)
    {
        DEBUGLOG << "Failed to initialize freetype: " << std::string(strerror(errno));
        return false;
    }
#endif

    if (!initFlipQueue())
    {
        DEBUGLOG << "Failed to initialize flip queue: " << std::string(strerror(errno));
        return false;
    }

    if (!allocateVideoMem(memSize, 0x200000))
    {
        DEBUGLOG << "Failed to allocate video memory: " << std::string(strerror(errno));
        return false;
    }

    if (!allocateFrameBuffers(numFrameBuffers))
    {
        DEBUGLOG << "Failed to allocate frame buffers";
        deallocateVideoMem();
        return false;
    }

    sceVideoOutSetFlipRate(this->video, 0);
    return true;
}

bool Scene2D::initFlipQueue()
{
    int rc = sceKernelCreateEqueue(&flipQueue, "homebrew flip queue");
    if (rc < 0)
        return false;

    rc = sceVideoOutAddFlipEvent(flipQueue, this->video, 0);
    if (rc < 0)
    {
        sceKernelDeleteEqueue(flipQueue);
        flipQueue = 0;
        return false;
    }

    return true;
}

bool Scene2D::allocateFrameBuffers(int num)
{
    if (num < 1 || !this->videoMem || this->frameBufferSize <= 0)
        return false;

    this->frameBuffers = new (std::nothrow) char*[num]();
    if (!this->frameBuffers)
        return false;

    for (int i = 0; i < num; i++)
    {
        this->frameBuffers[i] = this->allocateDisplayMem((size_t)this->frameBufferSize);
        if (!this->frameBuffers[i])
        {
            delete[] this->frameBuffers;
            this->frameBuffers = nullptr;
            return false;
        }
    }

    sceVideoOutSetBufferAttribute(&this->attr, 0x80000000, 1, 0,
                                  this->width, this->height, this->width);

    const int registerResult = sceVideoOutRegisterBuffers(this->video, 0,
                                      (void **)this->frameBuffers, num,
                                      &this->attr);
    if (registerResult != 0)
        return false;

    this->buffersRegistered = true;
    return true;
}

char *Scene2D::allocateDisplayMem(size_t size)
{
	// Essentially just bump allocation
	char *allocatedPtr = (char *)videoMemSP;
	videoMemSP += size;

	return allocatedPtr;
}

bool Scene2D::allocateVideoMem(size_t size, int alignment)
{
	int rc;
	
	// Align the allocation size
	this->directMemAllocationSize = (size + alignment - 1) / alignment * alignment;
	
	// Allocate memory for display buffer
	rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), this->directMemAllocationSize, alignment, 3, &this->directMemOff);
	
	if(rc < 0)
	{
		this->directMemAllocationSize = 0;
		return false;
	}
	
	// Map the direct memory
	rc = sceKernelMapDirectMemory(&this->videoMem, this->directMemAllocationSize, 0x33, 0, this->directMemOff, alignment);
	
	if(rc < 0)
	{
		sceKernelReleaseDirectMemory(this->directMemOff, this->directMemAllocationSize);
		
		this->directMemOff = 0;
		this->directMemAllocationSize = 0;
		
		return false;
	}
	
	// Set the stack pointer to the beginning of the buffer
	this->videoMemSP = (uintptr_t)this->videoMem;
	return true;
}

void Scene2D::deallocateVideoMem()
{
    bool mayReleaseDirectMemory = true;

    // Unmap the virtual view first. If unmapping fails, leave the backing
    // direct-memory allocation to process teardown rather than releasing it
    // while a virtual mapping may still refer to it.
    if (this->videoMem && this->directMemAllocationSize > 0)
    {
        const int rc = sceKernelMunmap(this->videoMem, this->directMemAllocationSize);
        printf("[EXIT] Scene2D: munmap rc=%d\n", rc);
        mayReleaseDirectMemory = (rc == 0);
    }

    if (this->directMemAllocationSize > 0 && mayReleaseDirectMemory)
    {
        const int rc = sceKernelReleaseDirectMemory(this->directMemOff,
                                                    this->directMemAllocationSize);
        printf("[EXIT] Scene2D: release direct memory rc=%d\n", rc);
    }
    else if (this->directMemAllocationSize > 0)
    {
        printf("[EXIT] Scene2D: keeping direct memory for process teardown\n");
    }

    this->videoMem = nullptr;
    this->videoMemSP = 0;
    this->directMemOff = 0;
    this->directMemAllocationSize = 0;

    delete[] this->frameBuffers;
    this->frameBuffers = nullptr;
    this->frameBufferCount = 0;
    this->activeFrameBufferIdx = 0;
    this->buffersRegistered = false;
}

void Scene2D::SetActiveFrameBuffer(int index)
{
    if (index >= 0 && index < this->frameBufferCount)
        this->activeFrameBufferIdx = index;
}

void Scene2D::SubmitFlip(int frameID)
{
    if (this->video >= 0 && this->frameBuffers &&
        this->activeFrameBufferIdx >= 0 && this->activeFrameBufferIdx < this->frameBufferCount)
        sceVideoOutSubmitFlip(this->video, this->activeFrameBufferIdx,
                              ORBIS_VIDEO_OUT_FLIP_VSYNC, frameID);
}

void Scene2D::FrameWait(int frameID)
{
	OrbisKernelEvent evt;
	int count;
	
	// If the video handle is not initialized, bail out. This is mostly a failsafe, this should never happen.
    if (this->video < 0)
        return;
		
	for(;;)
	{
		OrbisVideoOutFlipStatus flipStatus;
		
		// Get the flip status and check the arg for the given frame ID
		sceVideoOutGetFlipStatus(video, &flipStatus);
		
		if(flipStatus.flipArg == frameID)
			break;
			
		// Wait on next flip event
		if(sceKernelWaitEqueue(this->flipQueue, &evt, 1, &count, 0) != 0)
			break;
	}
}

void Scene2D::FrameBufferSwap()
{
    if (this->frameBufferCount > 0)
        this->activeFrameBufferIdx = (this->activeFrameBufferIdx + 1) % this->frameBufferCount;
}

void Scene2D::FrameBufferClear()
{
	// Clear the screen with a white frame buffer
	Color blank = { 255, 255, 255 };
	FrameBufferFill(blank);
}

#ifdef GRAPHICS_USES_FONT
bool Scene2D::InitFont(FT_Face *face, const char *fontPath, int fontSize)
{
	int rc;
	
	rc = FT_New_Face(this->ftLib, fontPath, 0, face);
	
	if(rc < 0)
		return false;

	rc = FT_Set_Pixel_Sizes(*face, 0, fontSize);
	
	if(rc < 0)
		return false;
	
	return true;
}
#endif

void Scene2D::FrameBufferFill(Color color)
{
    if (!this->frameBuffers || this->frameBufferCount <= 0 ||
        this->activeFrameBufferIdx < 0 ||
        this->activeFrameBufferIdx >= this->frameBufferCount ||
        !this->frameBuffers[this->activeFrameBufferIdx] ||
        this->width <= 0 || this->height <= 0)
        return;

    const uint32_t encodedColor = 0x80000000u |
        ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
    uint32_t *pixels = (uint32_t *)this->frameBuffers[this->activeFrameBufferIdx];
    const size_t pixelCount = (size_t)this->width * (size_t)this->height;

    for (size_t i = 0; i < pixelCount; ++i)
        pixels[i] = encodedColor;
}

void Scene2D::DrawPixel(int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= this->width || y >= this->height ||
        !this->frameBuffers || this->frameBufferCount <= 0 ||
        this->activeFrameBufferIdx < 0 || this->activeFrameBufferIdx >= this->frameBufferCount ||
        !this->frameBuffers[this->activeFrameBufferIdx])
        return;

    const size_t pixel = (size_t)y * (size_t)this->width + (size_t)x;
    const uint32_t encodedColor = 0x80000000u |
        ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
    ((uint32_t *)this->frameBuffers[this->activeFrameBufferIdx])[pixel] = encodedColor;
}

void Scene2D::DrawRectangle(int x, int y, int w, int h, Color color)
{
    if (w <= 0 || h <= 0 || this->width <= 0 || this->height <= 0 ||
        !this->frameBuffers || this->frameBufferCount <= 0 ||
        this->activeFrameBufferIdx < 0 || this->activeFrameBufferIdx >= this->frameBufferCount ||
        !this->frameBuffers[this->activeFrameBufferIdx])
        return;

    // Clip to the visible screen and avoid signed overflow in x+w / y+h.
    long long left = x;
    long long top = y;
    long long right = (long long)x + (long long)w;
    long long bottom = (long long)y + (long long)h;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > this->width) right = this->width;
    if (bottom > this->height) bottom = this->height;
    if (left >= right || top >= bottom) return;

    const uint32_t encodedColor = 0x80000000u |
        ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
    uint32_t *pixels = (uint32_t *)this->frameBuffers[this->activeFrameBufferIdx];
    for (int yPos = (int)top; yPos < (int)bottom; ++yPos)
    {
        uint32_t *row = pixels + (size_t)yPos * (size_t)this->width + (size_t)left;
        for (int xPos = (int)left; xPos < (int)right; ++xPos)
            row[xPos - (int)left] = encodedColor;
    }
}

void Scene2D::DrawRGBAImage(const unsigned char *rgba, int imageWidth, int imageHeight,
                            int x, int y, int w, int h, Color background)
{
    if (!rgba || imageWidth <= 0 || imageHeight <= 0 || w <= 0 || h <= 0 ||
        !this->frameBuffers || this->frameBufferCount <= 0 ||
        this->activeFrameBufferIdx < 0 || this->activeFrameBufferIdx >= this->frameBufferCount ||
        !this->frameBuffers[this->activeFrameBufferIdx])
        return;

    long long left = x, top = y;
    long long right = (long long)x + w, bottom = (long long)y + h;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > this->width) right = this->width;
    if (bottom > this->height) bottom = this->height;
    if (left >= right || top >= bottom) return;

    uint32_t *pixels = (uint32_t *)this->frameBuffers[this->activeFrameBufferIdx];
    for (int dy = (int)top; dy < (int)bottom; ++dy)
    {
        int sy = (int)(((long long)(dy - y) * imageHeight) / h);
        if (sy < 0) sy = 0;
        if (sy >= imageHeight) sy = imageHeight - 1;
        for (int dx = (int)left; dx < (int)right; ++dx)
        {
            int sx = (int)(((long long)(dx - x) * imageWidth) / w);
            if (sx < 0) sx = 0;
            if (sx >= imageWidth) sx = imageWidth - 1;
            const unsigned char *p = rgba + ((size_t)sy * (size_t)imageWidth + (size_t)sx) * 4u;
            unsigned a = p[3];
            uint8_t r = (uint8_t)((p[0] * a + background.r * (255u - a)) / 255u);
            uint8_t g = (uint8_t)((p[1] * a + background.g * (255u - a)) / 255u);
            uint8_t b = (uint8_t)((p[2] * a + background.b * (255u - a)) / 255u);
            pixels[(size_t)dy * (size_t)this->width + (size_t)dx] =
                0x80000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

#ifdef GRAPHICS_USES_FONT
void Scene2D::DrawText(char *txt, FT_Face face, int startX, int startY, Color bgColor, Color fgColor)
{
    // Render from a pre-rasterized bitmap atlas rather than depending on the
    // PS4 FreeType glyph bitmap layout at runtime. The atlas was generated
    // from the same Gontserrat-Regular.ttf asset for the two UI sizes (27/42).
    if (!txt || !this->frameBuffers || this->frameBufferCount <= 0 ||
        this->activeFrameBufferIdx < 0 || this->activeFrameBufferIdx >= this->frameBufferCount ||
        !this->frameBuffers[this->activeFrameBufferIdx] || this->width <= 0 || this->height <= 0)
        return;

    bool large = false;
    if (face && face->size && face->size->metrics.y_ppem >= 34)
        large = true;

    const PS4PE_BitmapGlyph *glyphs = large ? PS4PE_LARGE_GLYPHS : PS4PE_SMALL_GLYPHS;
    const uint8_t *fontData = large ? PS4PE_LARGE_DATA : PS4PE_SMALL_DATA;
    const int lineHeight = large ? PS4PE_LARGE_LINE_HEIGHT : PS4PE_SMALL_LINE_HEIGHT;

    uint32_t *pixels = (uint32_t *)this->frameBuffers[this->activeFrameBufferIdx];
    int xOffset = 0;
    int yOffset = 0;

    for (size_t n = 0; txt[n] != '\0';)
    {
        unsigned codepoint = (unsigned char)txt[n++];
        if (codepoint == '\n')
        {
            xOffset = 0;
            yOffset += lineHeight;
            continue;
        }
        if (codepoint == '\r')
            continue;
        if (codepoint == '\t')
        {
            xOffset += glyphs[0].advance * 4;
            continue;
        }
        // Decode the few non-ASCII UI symbols used by main.cpp (UTF-8).
        if ((codepoint & 0xE0u) == 0xC0u && txt[n] != '\0')
        {
            codepoint = ((codepoint & 0x1Fu) << 6) | ((unsigned char)txt[n++] & 0x3Fu);
        }
        else if ((codepoint & 0xF0u) == 0xE0u && txt[n] != '\0' && txt[n+1] != '\0')
        {
            codepoint = ((codepoint & 0x0Fu) << 12) |
                        (((unsigned char)txt[n] & 0x3Fu) << 6) |
                        ((unsigned char)txt[n+1] & 0x3Fu);
            n += 2;
        }

        unsigned glyphIndex;
        if (codepoint >= 32u && codepoint <= 126u)
            glyphIndex = codepoint - 32u;
        else if (codepoint == 0x2022u)
            glyphIndex = 95u; // bullet
        else if (codepoint == 0x2014u)
            glyphIndex = 96u; // em dash
        else
            glyphIndex = (unsigned)'?' - 32u;

        const PS4PE_BitmapGlyph &g = glyphs[glyphIndex];
        for (int gy = 0; gy < (int)g.height; ++gy)
        {
            const int y = startY + yOffset + (int)g.top + gy;
            if (y < 0 || y >= this->height) continue;
            for (int gx = 0; gx < (int)g.width; ++gx)
            {
                const int x = startX + xOffset + (int)g.left + gx;
                if (x < 0 || x >= this->width) continue;
                const uint32_t pixelIndex = g.offset + (uint32_t)gy * g.width + (uint32_t)gx;
                const uint8_t packed = fontData[pixelIndex >> 1];
                const unsigned cov4 = (pixelIndex & 1u) ? (packed & 0x0Fu) : (packed >> 4);
                if (cov4 == 0) continue;
                const unsigned coverage = cov4 * 17u;
                const unsigned inv = 255u - coverage;
                const uint8_t r = (uint8_t)((fgColor.r * coverage + bgColor.r * inv) / 255u);
                const uint8_t gch = (uint8_t)((fgColor.g * coverage + bgColor.g * inv) / 255u);
                const uint8_t b = (uint8_t)((fgColor.b * coverage + bgColor.b * inv) / 255u);
                pixels[(size_t)y * (size_t)this->width + (size_t)x] =
                    0x80000000u | ((uint32_t)r << 16) | ((uint32_t)gch << 8) | (uint32_t)b;
            }
        }
        xOffset += (int)g.advance;
    }
}
#endif
