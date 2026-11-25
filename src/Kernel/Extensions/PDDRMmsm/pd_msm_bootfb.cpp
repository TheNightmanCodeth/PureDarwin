// pd_msm_bootfb.cpp - the firmware's framebuffer, as the platform expert keeps it for the console
// Copyright (c) 2026 The PureDarwin Project, SPDX-License-Identifier: MPL-2.0 OR BSD-2-Clause OR MIT

#include <IOKit/IOPlatformExpert.h>

extern "C" int pd_msm_bootfb(uint64_t *phys, uint32_t *width, uint32_t *height, uint32_t *pitch,
                             uint32_t *depth);

extern "C" int
pd_msm_bootfb(uint64_t *phys, uint32_t *width, uint32_t *height, uint32_t *pitch, uint32_t *depth)
{
    IOPlatformExpert *pe = IOService::getPlatform();
    PE_Video video;
    
    if (pe == NULL || pe->getConsoleInfo(&video) != kIOReturnSuccess || video.v_baseAddr == 0)
        return -1;
    // initialize_screen tags a physical framebuffer in the low bits
    *phys = video.v_baseAddr & ~3UL;
    *width = (uint32_t)video.v_width;
    *height = (uint32_t)video.v_height;
    *pitch = (uint32_t)video.v_rowBytes;
    *depth = (uint32_t)video.v_depth;
    return 0;
}
