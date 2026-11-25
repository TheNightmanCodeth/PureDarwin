// pd_msm_fb.c - the firmware's framebuffer as PDDRMFramebuffer's display client, so the gpu draws
// straight into what the display scans out. one buffer: the display engine isn't driven, so both
// halves of the flip pair are the buffer on screen and frames tear
// Copyright (c) 2026 The PureDarwin Project, SPDX-License-Identifier: MPL-2.0 OR BSD-2-Clause OR MIT

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/dma-buf.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "pd_fbsd_os.h"
#include "pd_drm_fb.h"
#include "pd_msm.h"

// ready by PDDRMFramebuffer. NULL leaves the display to IOGOPFramebuffer
struct pd_drm_fb_ops *pd_drm_fb;

int pd_msm_bootfb(uint64_t *phys, uint32_t *width, uint32_t *height, uint32_t *pitch,
                  uint32_t *depth);

static struct {
    struct pd_msm_dev       *mdev;
    struct pd_drm_fb_info   info;
    struct mutex            lock;
    struct pd_msm_bo        *bo;        // the framebuffer as a gem object, made on first use
    struct dma_buf          *dmabuf;    // its one dma-buf
    u64                     flips;
} sfb;

static int
fb_setup(struct pd_drm_fb_info *info)
{
    *info = sfb.info;
    return 0;
}

static int
fb_display_count(void)
{
    return 1;
}

static int
fb_on_screen(int display, struct pd_drm_fb_info *info)
{
    if (display != 0)
        return -ENODEV;
    *info = sfb.info;
    return 0;
}

static int
fb_mode_count(int display)
{
    return (display == 0 ? 1 : 0);
}

static int
fb_get_mode(int display, int index, struct pd_drm_fb_mode *mode)
{
    if (display != 0 || index != 0)
        return -EINVAL;
    mode->width = sfb.info.width;
    mode->height = sfb.info.height;
    mode->refresh = sfb.info.refresh;
    mode->preferred = 1;
    return 0;
}

static int
fb_set_mode(int display, int index, struct pd_drm_fb_info *info)
{
    if (display != 0 || index != 0)
        return -EINVAL;
    *info = sfb.info;
    return 0;
}

static void
fb_release(void)
{
}

// no second buffer the display could scan. both halves are the one on screen
static int
fb_buffer(int display, int index, uint64_t *phys)
{
    if (display != 0 || index < 0 || index > 1)
        return -EINVAL;
    *phys = sfb.info.phys;
    return 0;
}

// nothing to flip and no vblank to wait for. frames land as the gpu writes them
static int
fb_flip(int display, int index, uint64_t *sequence, uint64_t *ns)
{
    if (display != 0 || index < 0 || index > 1)
        return -EINVAL;
    *sequence = ++sfb.flips;
    *ns = ktime_get_ns();
    return 0;
}

// fb's gem obj, wrapped on first use (export and self-test share it)
struct pd_msm_bo *
pd_msm_fb_bo(u32 *width, u32 *height, u32 *pitch)
{
    struct pd_msm_bo *bo;
    
    if (pd_drm_fb == NULL)
        return NULL;
    mutex_lock(&sfb.lock);
    if (sfb.bo == NULL) {
        bo = pd_msm_bo_wrap(&sfb.mdev->drm, sfb.info.phys,
                            round_up((size_t)sfb.info.pitch * sfb.info.height, 0x1000));
        if (IS_ERR(bo))
            printf("pd msm: fb: wrapping the framebuffer: %ld\n", PTR_ERR(bo));
        else
            sfb.bo = bo;
    }
    bo = sfb.bo;
    mutex_unlock(&sfb.lock);
    *width = sfb.info.width;
    *height = sfb.info.height;
    *pitch = sfb.info.pitch;
    return (bo);
}

static int
fb_export_fd(int display, int index, int *fd)
{
    struct pd_msm_bo *bo;
    struct dma_buf *db;
    u32 width, height, pitch;
    int ret;
    
    if (display != 0 || index < 0 || index > 1)
        return -EINVAL;
    if ((bo = pd_msm_fb_bo(&width, &height, &pitch)) == NULL)
        return -ENOMEM;
    mutex_lock(&sfb.lock);
    if (sfb.dmabuf == NULL) {
        db = drm_gem_prime_export(&bo->base, O_RDWR);
        if (IS_ERR(db)) {
            mutex_unlock(&sfb.lock);
            return PTR_ERR(db);
        }
        sfb.dmabuf = db;
    }
    db = sfb.dmabuf;
    get_dma_buf(db);
    mutex_unlock(&sfb.lock);
    // descriptor takes over the reference just taken
    ret = dma_buf_fd(db, O_CLOEXEC);
    if (ret < 0) {
        dma_buf_put(db);
        return ret;
    }
    *fd = ret;
    return 0;
}

static struct pd_drm_fb_ops pd_msm_fb_ops = {
    .setup = fb_setup,
    .display_count = fb_display_count,
    .on_screen = fb_on_screen,
    .mode_count = fb_mode_count,
    .get_mode = fb_get_mode,
    .set_mode = fb_set_mode,
    .release = fb_release,
    .buffer = fb_buffer,
    .flip = fb_flip,
    .export_fd = fb_export_fd,
};

// probe time, before PDDRMmsm registers and PDDRMFramebufferMsm matches on it
void
pd_msm_fb_register(struct pd_msm_dev *mdev)
{
    uint64_t phys;
    uint32_t width, height, pitch, depth;
    
    if (pdos_bootarg_int("pdmsmfb", 0) == 0)
        return;
    if (pd_msm_bootfb(&phys, &width, &height, &pitch, &depth) != 0) {
        printf("pd msm: fb: no boot framebuffer\n");
        return;
    }
    printf("pd msm: fb: boot framebuffer %ux%u, %u bpp, pitch %u, at phys 0x%llx\n", width, height,
           depth, pitch, (unsigned long long)phys);
    // smmu maps 4 KB pages and freedreno imports a linear 32 bpp buffer only at a 64-byte
    // pitch (fd6_layout.c: pitchalign 6)
    if (depth != 32 || (phys & 0xfff) != 0 || (pitch & 63) != 0) {
        printf("pd msm: fb: not drawable by the gpu, left to the cpu\n");
        return;
    }
    mutex_init(&sfb.lock);
    sfb.mdev = mdev;
    sfb.info.phys = phys;
    sfb.info.width = width;
    sfb.info.height = height;
    sfb.info.pitch = pitch;
    sfb.info.refresh = 60;
    pd_drm_fb = &pd_msm_fb_ops;
}
