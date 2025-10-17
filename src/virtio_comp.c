// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include "virtio_comp_common.h"
#include <linux/cpu.h>

#define VIRTIO_ID_COMP 42

static void virtcomp_clean_affinity(struct virtio_comp *vi, long hcpu)
{
	int i;

	if (vi->affinity_hint_set) {
		for (i = 0; i < vi->max_data_queues; i++)
			virtqueue_set_affinity(vi->data_vq[i].vq, NULL);

		vi->affinity_hint_set = false;
	}
}

static void virtcrypto_set_affinity(struct virtio_comp *vcompress)
{
	int i = 0;
	int cpu;

	/*
	 * In single queue mode, we don't set the cpu affinity.
	 */
	if (vcompress->curr_queue == 1 || vcompress->max_data_queues == 1) {
		virtcomp_clean_affinity(vcompress, -1);
		return;
	}

	/*
	 * In multiqueue mode, we let the queue to be private to one cpu
	 * by setting the affinity hint to eliminate the contention.
	 *
	 * TODO: adds cpu hotplug support by register cpu notifier.
	 *
	 */
	for_each_online_cpu(cpu) {
		virtqueue_set_affinity(vcompress->data_vq[i].vq, cpumask_of(cpu));
		if (++i >= vcompress->max_data_queues)
			break;
	}

	vcompress->affinity_hint_set = true;
}

static int virtcomp_update_status(struct virtio_comp *vcompress)
{
	u32 status;
	int err;

	virtio_cread(vcompress->vdev,
			struct virtio_comp_config, status, &status);

	/*
	 * Unknown status bits would be a host error and the driver
	 * should consider the device to be broken.
	 */
	if (status & (~VIRTIO_COMP_S_HW_READY)) {
		dev_warn(&vcompress->vdev->dev,
				"Unknown status bits: 0x%x\n", status);

		virtio_break_device(vcompress->vdev);
		return -EPERM;
	}

	if (vcompress->status == status)
		return 0;

	vcompress->status = status;

	if (vcompress->status & VIRTIO_COMP_S_HW_READY) {
		// err = virtcomp_dev_start(vcompress);
		// if (err) {
		// 	dev_err(&vcompress->vdev->dev,
		// 		"Failed to start virtio compress device.\n");

		// 	return -EPERM;
		// }
		dev_info(&vcompress->vdev->dev, "Accelerator device is ready\n");
	} else {
		// virtcomp_dev_stop(vcompress);
		dev_info(&vcompress->vdev->dev, "Accelerator is not ready\n");
	}

	return 0;
}

static void virtcomp_free_queues(struct virtio_comp *vi)
{
	kfree(vi->data_vq);
}

static void virtcomp_del_vqs(struct virtio_comp *vcompress)
{
	struct virtio_device *vdev = vcompress->vdev;

	virtcomp_clean_affinity(vcompress, -1);

	vdev->config->del_vqs(vdev);

	virtcomp_free_queues(vcompress);
}

static void virtcomp_dataq_callback(struct virtqueue *vq)
{
	struct virtio_comp *vcomp = vq->vdev->priv;
	struct virtio_comp_request *vc_req;
	unsigned long flags;
	unsigned int len;
	unsigned int qid = vq->index;

	spin_lock_irqsave(&vcomp->data_vq[qid].lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vc_req = virtqueue_get_buf(vq, &len)) != NULL) {
			spin_unlock_irqrestore(
				&vcomp->data_vq[qid].lock, flags);
			if (vc_req->alg_cb)
				vc_req->alg_cb(vc_req, len);
			spin_lock_irqsave(
				&vcomp->data_vq[qid].lock, flags);
		}
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&vcomp->data_vq[qid].lock, flags);
}

static int virtcomp_find_vqs(struct virtio_comp *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;
	// struct device *dev = &vi->vdev->dev;

	/*
	 * We expect 1 data virtqueue, followed by
	 * possible N-1 data queues used in multiqueue mode,
	 * followed by control vq.
	 */
	total_vqs = vi->max_data_queues + 1;

	/* Allocate space for find_vqs parameters */
	vqs = kcalloc(total_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kcalloc(total_vqs, sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kcalloc(total_vqs, sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;

	/* Parameters for control virtqueue */
	callbacks[total_vqs - 1] = NULL;
	names[total_vqs - 1] = "controlq";

	/* Allocate/initialize parameters for data virtqueues */
	for (i = 0; i < vi->max_data_queues; i++) {
		callbacks[i] = virtcomp_dataq_callback;
		snprintf(vi->data_vq[i].name, sizeof(vi->data_vq[i].name),
				"dataq.%d", i);
		names[i] = vi->data_vq[i].name;
	}

	ret = virtio_find_vqs(vi->vdev, total_vqs, vqs, callbacks, names, NULL);
	if (ret)
		goto err_find;

	vi->ctrl_vq = vqs[total_vqs - 1];

	for (i = 0; i < vi->max_data_queues; i++) {
		spin_lock_init(&vi->data_vq[i].lock);
		vi->data_vq[i].vq = vqs[i];
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;

err_find:
	kfree(names);
err_names:
	kfree(callbacks);
err_callback:
	kfree(vqs);
err_vq:
	return ret;
}

static int virtcomp_alloc_queues(struct virtio_comp *vi)
{
	vi->data_vq = kcalloc(vi->max_data_queues, sizeof(*vi->data_vq),
				GFP_KERNEL);
	if (!vi->data_vq)
		return -ENOMEM;

	return 0;
}

static int virtcomp_init_vqs(struct virtio_comp *vi)
{
	int ret;

	/* Allocate send & receive queues */
	ret = virtcomp_alloc_queues(vi);
	if (ret)
		goto err;

	ret = virtcomp_find_vqs(vi);
	if (ret)
		goto err_free;

	get_online_cpus();
	virtcrypto_set_affinity(vi);
	put_online_cpus();

	return 0;

err_free:
	virtcomp_free_queues(vi);
err:
	return ret;
}

static int virtio_comp_probe(struct virtio_device *vdev)
{
	int err = -EFAULT;
	struct virtio_comp *vcompress;
	u32 max_data_queues = 0;
	u64 max_size = 0;
	u32 comp_algo = 0;
	u32 hash_algo = 0;
	u32 comp_services = 0;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&vdev->dev) < 0) {
		/*
		 * If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&vdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	vcompress = kzalloc_node(sizeof(*vcompress), GFP_KERNEL,
					dev_to_node(&vdev->dev));
	if (!vcompress)
		return -ENOMEM;

	virtio_cread(vdev, struct virtio_comp_config,
			max_dataqueues, &max_data_queues);
	if (max_data_queues < 1)
		max_data_queues = 1;

	virtio_cread(vdev, struct virtio_comp_config,
			comp_algo, &comp_algo);
	virtio_cread(vdev, struct virtio_comp_config,
			max_size, &max_size);
	virtio_cread(vdev, struct virtio_comp_config,
			hash_algo, &hash_algo);

	/* Add virtio comp device to global table */
	// err = virtcomp_devmgr_add_dev(vcompress);
	// if (err) {
	// 	dev_err(&vdev->dev, "Failed to add new virtio comp device.\n");
	// 	goto free;
	// }
	vcompress->owner = THIS_MODULE;
	vcompress = vdev->priv = vcompress;
	vcompress->vdev = vdev;

	spin_lock_init(&vcompress->ctrl_lock);

	/* Use single data queue as default */
	vcompress->curr_queue = 1;
	vcompress->max_data_queues = max_data_queues;
	vcompress->comp_algo = comp_algo;
	vcompress->max_size = max_size;
	vcompress->comp_services = comp_services;
	vcompress->hash_algo = hash_algo;

	dev_info(&vdev->dev,
		"max_queues: %u, max_size 0x%llx\n",
		vcompress->max_data_queues,
		vcompress->max_size);

	err = virtcomp_init_vqs(vcompress);
	if (err) {
		dev_err(&vdev->dev, "Failed to initialize vqs.\n");
		goto free_dev;
	}

	virtio_device_ready(vdev);

	err = virtcomp_update_status(vcompress);
	if (err)
		goto free_vqs;

	return 0;

free_vqs:
	vcompress->vdev->config->reset(vdev);
	virtcomp_del_vqs(vcompress);
free_dev:
	// virtcomp_devmgr_rm_dev(vcompress);
// free:
	kfree(vcompress);
	return err;
}

static void virtcomp_free_unused_reqs(struct virtio_comp *vcompress)
{
	struct virtio_comp_request *vc_req;
	int i;
	struct virtqueue *vq;

	for (i = 0; i < vcompress->max_data_queues; i++) {
		vq = vcompress->data_vq[i].vq;
		while ((vc_req = virtqueue_detach_unused_buf(vq)) != NULL) {
			kfree(vc_req->req_data);
			kfree(vc_req->sgs);
		}
	}
}

static void virtio_comp_remove(struct virtio_device *vdev)
{	struct virtio_comp *vcompress = vdev->priv;

	dev_info(&vdev->dev, "Start virtcrypto_remove.\n");

	// if (virtcomp_dev_started(vcompress))
	// 	virtcomp_dev_stop(vcompress);
	vdev->config->reset(vdev);
	virtcomp_free_unused_reqs(vcompress);
	virtcomp_del_vqs(vcompress);
	// virtcomp_devmgr_rm_dev(vcompress);
	kfree(vcompress);
}


static void virtcomp_config_changed(struct virtio_device *vdev)
{
	struct virtio_comp *vcompress = vdev->priv;

	virtcomp_update_status(vcompress);
}


// 定义设备 ID 匹配表
static const struct virtio_device_id id_table[] = {
    { VIRTIO_ID_COMP, VIRTIO_DEV_ANY_ID },
    { 0 },
};

// 注册驱动
static struct virtio_driver virtio_comp_driver = {
    .feature_table = NULL,
    .feature_table_size = 0,
    .driver.name = "virtio_comp",
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .probe = virtio_comp_probe,
    .remove = virtio_comp_remove,
		.config_changed = virtcomp_config_changed,
};

module_virtio_driver(virtio_comp_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Minimal Virtio Comp Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name <you@example.com>");
