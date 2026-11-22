// pd_msm_gpu.c - what every adreno generation shares: registers, the iommu, clocks, the first-open start, fences
// Copyright (c) 2026 The PureDarwin Project, SPDX-License-Identifier: MPL-2.0 OR BSD-2-Clause OR MIT

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/ktime.h>

#include "pd_fbsd_os.h"
#include "pd_msm.h"

// MARK: - DEV: DELETEME
int pd_msm_stage(void)
{
    return (pdos_bootarg_int("pdmsmstage", 0));
}

int
pd_msm_op_enter(struct pd_msm_dev *mdev)
{
    struct pd_msm_gpu *g = &mdev->gpu;
    int state = pdos_wait_lock();
    int error = g->fence_error;
    
    if (error == 0)
        g->active_ops++;
    
    pdos_wait_unlock(state);
    return error;
}

void
pd_msm_op_leave(struct pd_msm_dev *mdev)
{
    struct pd_msm_gpu *g = &mdev->gpu;
    int state = pdos_wait_lock();
    
    if (--g->active_ops == 0)
        pdos_wakeup(&g->drain_event, 1);
    
    pdos_wait_unlock(state);
}

static void
selftest(struct pd_msm_dev *mdev)
{
    const struct pd_msm_gpu_funcs *f = mdev->info->funcs;
    struct pd_msm_bo *ib, *dst;
    u32 seqno, n, got;
    u64 iova;
    int error;
    
    if (f->selftest_ib == NULL || pdos_bootarg_int("pdmsmtest", 0) == 0)
        return;
    
    ib = pd_msm_bo_new(&mdev->drm, PAGE_SIZE, PD_MSM_BO_KERNEL);
    dst = pd_msm_bo_new(&mdev->drm, PAGE_SIZE, PD_MSM_BO_KERNEL);
    if (IS_ERR(ib) || IS_ERR(dst) || pd_msm_bo_iova(mdev, ib) != 0 || pd_msm_bo_iova(mdev, dst) != 0) {
        printf("pd msm: selftest: no buffers\n");
        return;
    }
    n = f->selftest_ib(ib->cpu, dst->iova, 0xc0ffee01);
    iova = ib->iova;
    wmb();
    if ((error = pd_msm_gpu_submit(mdev, NULL, &iova, &n, 1, &seqno)) != 0) {
        printf("pd msm: selftest: submit %d\n", error);
        return;
    }
    error = pd_msm_gpu_wait(mdev, seqno, 1000000000);
    rmb();
    got = READ_ONCE(*(volatile u32 *)dst->cpu);
    printf("pd msm: selftest: ib 0x%llx %u dwords, seqno %u %s, marker 0x%08x (%s)\n",
           (unsigned long long)iova, n, seqno, error == 0 ? "signalled" : "timed out", got,
           got == 0xc0ffee01 ? "written" : "not written");
}


// the iommu kinds this driver knows, matched against the node the gpu's iommus property names
static const struct pd_msm_iommu *const iommus[] = {
	&pd_msm_iommu_v1,
    &pd_msm_iommu_smmu500,
};

static int
iommu_attach(struct pd_msm_dev *mdev)
{
	struct device *dev = &mdev->pdev->dev;
	struct of_phandle_args args;

	if (of_parse_phandle_with_args(dev->of_node, "iommus", "#iommu-cells", 0, &args) != 0) {
		dev_err(dev, "no iommus property\n");
		return (-ENODEV);
	}
	for (int i = 0; i < ARRAY_SIZE(iommus); i++) {
		if (!of_device_is_compatible(args.np, iommus[i]->compatible))
			continue;
		mdev->gpu.iommu = iommus[i];
		mdev->gpu.iommu_priv = iommus[i]->attach(dev);
		return (mdev->gpu.iommu_priv != NULL ? 0 : -ENODEV);
	}
	dev_err(dev, "no driver for the gpu's iommu\n");
	return (-ENODEV);
}

int
pd_msm_iommu_map(struct pd_msm_dev *mdev, u64 iova, u64 phys, size_t size)
{
	return (mdev->gpu.iommu->map(mdev->gpu.iommu_priv, iova, phys, size));
}

void
pd_msm_iommu_unmap(struct pd_msm_dev *mdev, u64 iova, size_t size)
{
	mdev->gpu.iommu->unmap(mdev->gpu.iommu_priv, iova, size);
}

// spec 2: the core clock at the chip's top rate, then every clock the node lists. The power
// domain came on before probe, the iommu's clocks with the iommu
static int
power_on(struct pd_msm_dev *mdev)
{
	struct device *dev = &mdev->pdev->dev;
	int error;

	mdev->nclks = devm_clk_bulk_get_all(dev, &mdev->clks);
	if (mdev->nclks < 0)
		return (mdev->nclks);
	for (int i = 0; i < mdev->nclks; i++) {
		if (mdev->info->core_clk != NULL && mdev->clks[i].id != NULL &&
            strcmp(mdev->clks[i].id, mdev->info->core_clk) == 0)
			mdev->core_clk = mdev->clks[i].clk;
	}
	if (mdev->core_clk != NULL && (error = clk_set_rate(mdev->core_clk, mdev->info->max_freq)) != 0)
		dev_err(dev, "core clock rate: %d\n", error);
	return (clk_bulk_prepare_enable(mdev->nclks, mdev->clks));
}

// probe time, nothing from disk: the root filesystem may not be there yet
int
pd_msm_gpu_init(struct pd_msm_dev *mdev)
{
	struct pd_msm_gpu *g = &mdev->gpu;
	struct device *dev = &mdev->pdev->dev;
	int error;

	mutex_init(&g->lock);
	mutex_init(&g->start_lock);
	g->va_next = mdev->info->va_start;
	g->mmio = devm_platform_ioremap_resource(mdev->pdev, 0);
	if (IS_ERR(g->mmio)) {
		dev_err(dev, "cannot map the gpu registers: %ld\n", PTR_ERR(g->mmio));
		return (PTR_ERR(g->mmio));
	}
	if ((error = iommu_attach(mdev)) != 0)
		return (error);
	if ((error = power_on(mdev)) != 0) {
		dev_err(dev, "clocks did not start: %d\n", error);
		return (error);
	}
	return (mdev->info->funcs->init(mdev));
}

// runs once, from the first open, when microcode on the root filesystem can be read
int
pd_msm_gpu_start(struct pd_msm_dev *mdev)
{
	struct pd_msm_gpu *g = &mdev->gpu;
    bool first = false;
	int error = 0;

	mutex_lock(&g->start_lock);
    mutex_lock(&g->lock);
    if (g->stopping) {
        error = -ENODEV;
    } else if (!g->running) {
        mutex_unlock(&g->lock); // start can acquire this
		error = mdev->info->funcs->start(mdev);
        mutex_lock(&g->lock);
		g->running = error == 0;
        first = g->running;
	}
    mutex_unlock(&g->lock);
	mutex_unlock(&g->start_lock);
    if (first)
        selftest(mdev);
	return (error);
}

int
pd_msm_gpu_submit(struct pd_msm_dev *mdev, struct drm_file *file, const u64 *ib_iova,
    const u32 *ib_dwords, int nib, u32 *seqno)
{
	struct pd_msm_gpu *g = &mdev->gpu;
	int error;

    mutex_lock(&g->lock);
    if (g->stopping || !g->running) {
        mutex_unlock(&g->lock);
        return (-ENODEV);
    }
	*seqno = g->seqno + 1;
	error = mdev->info->funcs->submit(mdev, file, ib_iova, ib_dwords, nib, *seqno);
	if (error == 0) {
		g->seqno = *seqno;
		g->last_ctx = file;
	}
	mutex_unlock(&g->lock);
	return (error);
}

u32
pd_msm_gpu_completed(struct pd_msm_dev *mdev)
{
	return (mdev->info->funcs->completed(mdev));
}

static int
pd_msm_gpu_wait_irq(struct pd_msm_dev *mdev, u32 target, s64 timeout_ns)
{
    struct pd_msm_gpu *g = &mdev->gpu;
    u64 start = ktime_get_ns();
    u64 budget = timeout_ns > 0 ? (u64)timeout_ns : 0;
    int state = pdos_wait_lock();
    int error;
    
    for (;;) {
        if (g->fence_error != 0) {
            // make terminal shutdown return an error even if an unobserved fence happened to complete
            // More elaborate recovery can preserve individual fence results later
            error = g->fence_error; // protected by pdos_wait_lock
            break;
        }
        
        u32 completed = pd_msm_gpu_completed(mdev);
        
        if ((s32)(completed - target) >= 0) {
            rmb();
            error = 0;
            break;
        }
        
        u64 elapsed = ktime_get_ns() - start;
        if (elapsed >= budget) {
            error = -ETIMEDOUT;
            break;
        }
        
        u64 remaining = budget - elapsed;
        
        s64 usec = (s64)(remaining / 1000 + (remaining % 1000 != 0));
        
        (void)pdos_wait_sleep(&g->fence_event, usec, 0, &state);
    }
    
    pdos_wait_unlock(state);
    return error;
}


// IRQ waits where supported or polling
int
pd_msm_gpu_wait(struct pd_msm_dev *mdev, u32 seqno, s64 timeout_ns)
{
    struct pd_msm_gpu *g = &mdev->gpu;
    
    if (g->irq_fences)
        return pd_msm_gpu_wait_irq(mdev, seqno, timeout_ns);
    
    u64 start = ktime_get_ns();
    u64 budget = timeout_ns > 0 ? (u64)timeout_ns : 0;
    bool dumped = false;

	for (;;) {
        // fence_error is always under the wait lock
        int state = pdos_wait_lock();
        int error = g->fence_error;
        pdos_wait_unlock(state);
        
        if (error != 0)
            return error;
        
        if ((s32)(pd_msm_gpu_completed(mdev) - seqno) >= 0) {
            rmb();
            break;
        }
        
        u64 elapsed = ktime_get_ns() - start;
        if (elapsed >= budget)
            return -ETIMEDOUT;
        
        if (!dumped && elapsed > 1000000000ull && mdev->info->funcs->dump != NULL) {
            mdev->info->funcs->dump(mdev);
            dumped = true;
        }
        
        usleep_range(50, 100);
	}
	if (mdev->info->funcs->fence_ack != NULL)
		mdev->info->funcs->fence_ack(mdev);
	return (0);
}

int
pd_msm_gpu_shutdown(struct pd_msm_dev *mdev)
{
    struct pd_msm_gpu *g = &mdev->gpu;
    int error = 0;
    mutex_lock(&g->start_lock);
    mutex_lock(&g->lock);
    
    g->stopping = true;
    
    int state = pdos_wait_lock();
    g->fence_error = -ENODEV;
    pdos_wakeup(&g->fence_event, 1);
    pdos_wait_unlock(state);
    
    mutex_unlock(&g->lock);
    
    // detach IRQ here while startup remains excluded
    // Don't hold gpu.lock or pdos_wait_lock() across IRQ removal
    if (mdev->info->funcs->irq_stop != NULL)
        error = mdev->info->funcs->irq_stop(mdev);
    
    mutex_unlock(&g->start_lock);
    
    // allow admitted operations up to five seconds to return
    const u64 budget_ns = 5000000000ull;
    u64 start = ktime_get_ns();
    int drain_error = 0;
    
    state = pdos_wait_lock();
    
    while (g->active_ops != 0) {
        u64 elapsed = ktime_get_ns() - start;
        
        if (elapsed >= budget_ns) {
            drain_error = -ETIMEDOUT;
            break;
        }
        
        u64 remaining = budget_ns - elapsed;
        s64 usec = (s64)(remaining / 1000 + (remaining % 1000 != 0));
        // registers the wait, releases the wait lock while asleep and acquires it before returning
        (void)pdos_wait_sleep(&g->drain_event, usec, 0, &state);
    }
    
    pdos_wait_unlock(state);
    
    return error != 0 ? error : drain_error;
}
