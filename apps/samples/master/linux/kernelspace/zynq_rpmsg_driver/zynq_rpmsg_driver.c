/*
 * cyclone Remote Processor Messaging Framework driver
 *
 * Copyright (C) 2014 Mentor Graphics Corporation
 *
 * Based on cyclone Remote Processor driver
 *
 * Copyright (C) 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012 PetaLogix
 *
 * Based on origin OMAP Remote Processor driver
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of_irq.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include <asm/outercache.h>
#include <asm/cacheflush.h>
#include <asm/hardware/gic.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/idr.h>

#include "cyclone_rpmsg_internals.h"

static DEFINE_IDA(rpmsg_cyclone_dev_index);

/* Globals. */
struct work_struct cyclone_rpmsg_work;

struct platform_device *cyclone_rpmsg_platform;
struct cyclone_rpmsg_instance *cyclone_rpmsg_p;

static void cyclone_rpmsg_virtio_notify(struct virtqueue *vq)
{
	/* Notify the other core. */
	if (vq == cyclone_rpmsg_p->vrings[0].vq)
		/* Raise soft IRQ on GIC. */
		gic_raise_softirq_unicore(0, cyclone_rpmsg_p->vring0);
	else
		gic_raise_softirq_unicore(0, cyclone_rpmsg_p->vring1);
}

static void cyclone_rpmsg_virtio_del_vqs(struct virtio_device *vdev)
{
	struct cyclone_rpmsg_vring   *local_vring;
	int i;

	for (i = 0; i < cyclone_RPMSG_NUM_VRINGS; i++) {

		local_vring = &(cyclone_rpmsg_p->vrings[i]);

		vring_del_virtqueue(local_vring->vq);

		local_vring->vq =  NULL;

		dma_free_coherent(&(cyclone_rpmsg_platform->dev),
					local_vring->len, local_vring->va,
					local_vring->dma);
	}
}

static int cyclone_rpmsg_virtio_find_vqs(struct virtio_device *vdev,
					unsigned nvqs, struct virtqueue *vqs[],
					vq_callback_t *callbacks[],
					const char *names[])
{
	int				i;
	struct cyclone_rpmsg_vring   *local_vring;
	void				*vring_va;
	int				 size;

	/* Skip through the vrings. */
	for (i = 0; i < nvqs; i++) {

		local_vring = &(cyclone_rpmsg_p->vrings[i]);

		local_vring->len = cyclone_rpmsg_p->num_descs;

		size = vring_size(cyclone_rpmsg_p->num_descs,
					cyclone_rpmsg_p->align);

		/* Allocate non-cacheable memory for the vring. */
		local_vring->va = dma_alloc_coherent
					(&(cyclone_rpmsg_platform->dev),
					size, &(local_vring->dma), GFP_KERNEL);

		vring_va = local_vring->va;

		memset(vring_va, 0, size);

		local_vring->vq = vring_new_virtqueue(i,
						cyclone_rpmsg_p->num_descs,
						cyclone_rpmsg_p->align, vdev,
						false, vring_va,
						cyclone_rpmsg_virtio_notify,
						callbacks[i], names[i]);

		vqs[i] = local_vring->vq;
	}

	return 0;
}

static u8 cyclone_rpmsg_virtio_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void cyclone_rpmsg_virtio_set_status(struct virtio_device *vdev, u8 status)
{
   /* */
}

static void cyclone_rpmsg_virtio_reset(struct virtio_device *vdev)
{
	/* */
}

static u32 cyclone_rpmsg_virtio_get_features(struct virtio_device *vdev)
{
	/* Return features. */
	return cyclone_rpmsg_p->dev_feature;
}

static void cyclone_rpmsg_virtio_finalize_features(struct virtio_device *vdev)
{
	/* Set vring transport features. */
	vring_transport_features(vdev);

	cyclone_rpmsg_p->gen_feature = vdev->features[0];
}

static void cyclone_rpmsg_vdev_release(struct device *dev)
{

}

static void mid_level_type_release(struct device *dev)
{

}

static struct virtio_config_ops cyclone_rpmsg_virtio_config_ops = {
	.get_features	= cyclone_rpmsg_virtio_get_features,
	.finalize_features = cyclone_rpmsg_virtio_finalize_features,
	.find_vqs	= cyclone_rpmsg_virtio_find_vqs,
	.del_vqs	= cyclone_rpmsg_virtio_del_vqs,
	.reset		= cyclone_rpmsg_virtio_reset,
	.set_status	= cyclone_rpmsg_virtio_set_status,
	.get_status	= cyclone_rpmsg_virtio_get_status,
};

static struct device_type mid_level_type = {
	.name		= "rpmsg_mid",
	.release	= mid_level_type_release,
};

static void handle_event(struct work_struct *work)
{
	struct virtqueue *vq;

	flush_cache_all();

	outer_flush_range(cyclone_rpmsg_p->mem_start, cyclone_rpmsg_p->mem_end);

	vq = cyclone_rpmsg_p->vrings[0].vq;

	if (vring_interrupt(0, vq) == IRQ_NONE)
		dev_dbg(&cyclone_rpmsg_platform->dev, "no message found in vqid 0\n");
}

static void ipi_handler(void)
{
	schedule_work(&cyclone_rpmsg_work);
}

static int cyclone_rpmsg_deinitialize(struct platform_device *pdev)
{
	unregister_virtio_device(&(cyclone_rpmsg_p->virtio_dev));

	put_device(&(cyclone_rpmsg_p->mid_dev));

	dma_release_declared_memory(&pdev->dev);

	clear_ipi_handler(cyclone_rpmsg_p->vring0);

	return 0;
}

static int cyclone_rpmsg_initialize(struct platform_device *pdev)
{
	int ret = 0;
	int index;
	struct virtio_device *virtio_dev;

	/* Register ipi handler. */
	ret = set_ipi_handler(cyclone_rpmsg_p->vring0, ipi_handler,
				"Firmware kick");

	if (ret) {
		dev_err(&pdev->dev, "IPI handler already registered\n");
		return -ENODEV;
	}

	/* Initialize work. */
	INIT_WORK(&cyclone_rpmsg_work, handle_event);

	/* Memory allocations for vrings. */
	ret = dma_declare_coherent_memory(&pdev->dev,
					cyclone_rpmsg_p->mem_start,
					cyclone_rpmsg_p->mem_start,
					cyclone_rpmsg_p->mem_end -
					cyclone_rpmsg_p->mem_start + 1,
					DMA_MEMORY_IO);

	if (!ret) {
		dev_err(&pdev->dev, "dma_declare_coherent_memory failed\n");
		return -ENODEV;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return -ENODEV;
	}

	/* Initialize a mid-level device. Needed because of bad data structure
	 * handling and assumptions within the virtio rpmsg bus. We are doing it
	 * to just make sure that the virtio device has a parent device which
	 * then itself has a parent in the form of the platform device. */
	device_initialize(&(cyclone_rpmsg_p->mid_dev));

	cyclone_rpmsg_p->mid_dev.parent = &(pdev->dev);
	cyclone_rpmsg_p->mid_dev.type = &mid_level_type;

	index = ida_simple_get(&rpmsg_cyclone_dev_index, 0, 0, GFP_KERNEL);

	if (index < 0) {
		put_device(&(cyclone_rpmsg_p->mid_dev));
		return -ENODEV;
	}

	dev_set_name(&(cyclone_rpmsg_p->mid_dev), "rpmsg_mid%d", index);

	device_add(&(cyclone_rpmsg_p->mid_dev));

	/* Setup the virtio device structure. */
	virtio_dev = &(cyclone_rpmsg_p->virtio_dev);

	virtio_dev->id.device	= cyclone_rpmsg_p->virtioid;
	virtio_dev->config	  = &cyclone_rpmsg_virtio_config_ops;
	virtio_dev->dev.parent  = &(cyclone_rpmsg_p->mid_dev);
	virtio_dev->dev.release = cyclone_rpmsg_vdev_release;

	/* Register the virtio device. */
	ret = register_virtio_device(virtio_dev);

	dev_info(&(cyclone_rpmsg_platform->dev), "virtio device registered \r\n");

	return ret;
}

static int cyclone_rpmsg_retrieve_dts_info(struct platform_device *pdev)
{
	const void *of_prop;
	struct resource *res;

	/* Retrieve memory information. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "invalid address\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->mem_start = res->start;
	cyclone_rpmsg_p->mem_end = res->end;

	/* Allocate free IPI number */
	of_prop = of_get_property(pdev->dev.of_node, "vring0", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify vring0 node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->vring0 = be32_to_cpup(of_prop);


	/* Read vring1 ipi number */
	of_prop = of_get_property(pdev->dev.of_node, "vring1", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify vring1 node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->vring1 = be32_to_cpup(of_prop);

	of_prop = of_get_property(pdev->dev.of_node, "num-descs", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify num descs node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->num_descs = be32_to_cpup(of_prop);

	/* Read dev-feature  */
	of_prop = of_get_property(pdev->dev.of_node, "dev-feature", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify dev features node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->dev_feature = be32_to_cpup(of_prop);

	/* Read gen-feature */
	of_prop = of_get_property(pdev->dev.of_node, "gen-feature", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify gen features node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->gen_feature = be32_to_cpup(of_prop);

	/* Read number of vrings */
	of_prop = of_get_property(pdev->dev.of_node, "num-vrings", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify num-vrings node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->num_vrings = be32_to_cpup(of_prop);

	if (cyclone_rpmsg_p->num_vrings > 2) {
		dev_err(&pdev->dev, "We do not currently support more than 2 vrings.\n");
		return -ENODEV;
	}

	/* Read vring alignment */
	of_prop = of_get_property(pdev->dev.of_node, "alignment", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify alignment node property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->align = be32_to_cpup(of_prop);

	/* Read virtio ID*/
	of_prop = of_get_property(pdev->dev.of_node, "virtioid", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify virtio id property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->virtioid = be32_to_cpup(of_prop);

	/* Read Ring Tx address. */
	of_prop = of_get_property(pdev->dev.of_node, "ringtx", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify ring tx property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->ringtx = be32_to_cpup(of_prop);

	/* Read Ring Rx address. */
	of_prop = of_get_property(pdev->dev.of_node, "ringrx", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify ringrx property\n");
		return -ENODEV;
	}

	cyclone_rpmsg_p->ringrx = be32_to_cpup(of_prop);

	return 0;
}

static int cyclone_rpmsg_probe(struct platform_device *pdev)
{
	int ret = 0;

	cyclone_rpmsg_platform = pdev;

	/* Allocate memory for the cyclone RPMSG instance. */
	cyclone_rpmsg_p = kzalloc(sizeof(struct cyclone_rpmsg_instance), GFP_KERNEL);

	if (!cyclone_rpmsg_p) {
		dev_err(&pdev->dev, "Unable to alloc memory for cyclone_rpmsg instance.\n");
		return -ENOMEM;
	}

	/* Save the instance handle. */
	platform_set_drvdata(pdev, cyclone_rpmsg_p);

	/* Retrieve the rquired information from DTS. */
	ret = cyclone_rpmsg_retrieve_dts_info(pdev);

	if (ret) {
		dev_err(&pdev->dev, "Failure in retrieving info from DTS.\n");
		kzfree(cyclone_rpmsg_p);
		return -ENOMEM;
	}

	/* Perform all the initializations. */
	ret = cyclone_rpmsg_initialize(pdev);

	return ret;
}

static int cyclone_rpmsg_remove(struct platform_device *pdev)
{
	cyclone_rpmsg_deinitialize(pdev);

	kfree(cyclone_rpmsg_p);

	return 0;
}


/* Match table for OF platform binding */
static struct of_device_id cyclone_rpmsg_match[] = {
	{ .compatible = "xlnx,cyclone_rpmsg_driver", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, cyclone_rpmsg_match);

static struct platform_driver cyclone_rpmsg_driver = {
	.probe = cyclone_rpmsg_probe,
	.remove = cyclone_rpmsg_remove,
	.driver = {
		.name = "cyclone_rpmsg_driver",
		.owner = THIS_MODULE,
		.of_match_table = cyclone_rpmsg_match,
	},
};

static int __init init(void)
{
	return platform_driver_register(&cyclone_rpmsg_driver);
}

static void __exit fini(void)
{
	platform_driver_unregister(&cyclone_rpmsg_driver);
}


module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cyclone RPMSG driver to use RPMSG framework without remoteproc");
