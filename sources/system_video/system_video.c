#include "system_video.h"

#include "system_physical_memory.h"

typedef struct _SYSTEM_VIDEO
{
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT8 PixelFormat;
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
    UINT32 PixelsPerScanLine;
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
} SYSTEM_VIDEO;

static SYSTEM_VIDEO SystemVideo;
static UINT64 NumberOfApplications;
static CONST APPLICATION_VIEW **Applications;
static BOOLEAN IsInitialized = FALSE;

static STATUS API SystemVideoDrawApplicationViewInfo(IN CONST APPLICATION_VIEW_INFO *View);

STATUS API SystemVideoInit(IN CONST VIDEO_ADAPTER *VideoAdapter)
{
    STATUS Status = E_OK;
    UINT32 *FrameBuffer = NULL_PTR;

    if (NULL_PTR == VideoAdapter)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (TRUE == IsInitialized)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SystemVideo.Version = VideoAdapter->Info.Version;
    SystemVideo.HorizontalResolution = VideoAdapter->Info.HorizontalResolution;
    SystemVideo.VerticalResolution = VideoAdapter->Info.VerticalResolution;
    SystemVideo.PixelFormat = VideoAdapter->Info.PixelFormat;
    SystemVideo.RedMask = VideoAdapter->Info.PixelInformation.RedMask;
    SystemVideo.GreenMask = VideoAdapter->Info.PixelInformation.GreenMask;
    SystemVideo.BlueMask = VideoAdapter->Info.PixelInformation.BlueMask;
    SystemVideo.ReservedMask = VideoAdapter->Info.PixelInformation.ReservedMask;
    SystemVideo.PixelsPerScanLine = VideoAdapter->Info.PixelsPerScanLine;
    SystemVideo.FrameBufferBase = VideoAdapter->FrameBufferBase;
    SystemVideo.FrameBufferSize = VideoAdapter->FrameBufferSize;

    FrameBuffer = (UINT32 *)SystemVideo.FrameBufferBase;
    for (UINT64 i = 0; i < SystemVideo.PixelsPerScanLine * SystemVideo.HorizontalResolution; ++i)
    {
        FrameBuffer[i] = 0x0000FF00;
    }

    NumberOfApplications = 0;
    Applications = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&Applications, 10 * sizeof(*Applications) * 4096);
    IsInitialized = TRUE;

Cleanup:
    if (E_OK != Status)
    {
        SystemVideo.Version = 0;
        SystemVideo.VerticalResolution = 0;
        SystemVideo.PixelFormat = 0;
        SystemVideo.RedMask = 0;
        SystemVideo.GreenMask = 0;
        SystemVideo.BlueMask = 0;
        SystemVideo.ReservedMask = 0;
        SystemVideo.PixelsPerScanLine = 0;
        SystemVideo.FrameBufferBase = 0;
        SystemVideo.FrameBufferSize = 0;
    }

    return Status;
}

STATUS API SystemVideoRegisterApplicationView(IN CONST APPLICATION_VIEW *ApplicationView)
{
    STATUS Status = E_OK;

    if (FALSE == IsInitialized)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (NULL_PTR == ApplicationView)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Applications[NumberOfApplications++] = ApplicationView;

Cleanup:
    return Status;
}

STATUS API SystemVideoRedraw(VOID)
{
    STATUS Status = E_OK;

    if (FALSE == IsInitialized)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    for (UINT64 i = 0; i < NumberOfApplications; ++i)
    {
        if (E_OK != SystemVideoDrawApplicationViewInfo(Applications[i]->Info))
        {
            Status = E_NOT_OK;
        }
    }

Cleanup:
    return Status;
}

static STATUS API SystemVideoDrawApplicationViewInfo(IN CONST APPLICATION_VIEW_INFO *View)
{
    STATUS Status = E_OK;
    UINT32 *FrameBuffer = NULL_PTR;
    UINT64 DisplaysXy = 0;
    UINT64 ViewPixelBufferIndex = 0;

    if (View->PositionY + View->Height > SystemVideo.HorizontalResolution)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    if (View->PositionY + View->Width > SystemVideo.PixelsPerScanLine)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    FrameBuffer = (UINT32 *)SystemVideo.FrameBufferBase;
    if (NULL_PTR == FrameBuffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    DisplaysXy = View->PositionY * SystemVideo.PixelsPerScanLine + View->PositionX;
    for (UINT64 i = 0; i < View->Height; ++i)
    {
        for (UINT64 j = 0; j < View->Width; ++j)
        {
            FrameBuffer[DisplaysXy + j] = View->FrameBuffer[ViewPixelBufferIndex++];
        }
        DisplaysXy += SystemVideo.PixelsPerScanLine;
    }

Cleanup:
    return Status;
}
