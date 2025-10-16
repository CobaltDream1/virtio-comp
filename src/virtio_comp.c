// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

#define VIRTIO_ID_COMP 42

static int virtio_comp_probe(struct virtio_device *vdev)
{
    pr_info("virtio_comp: device probed (id=%d, name=%s)\n",
            vdev->id.device, vdev->dev.kobj.name);

    return 0;
}

static void virtio_comp_remove(struct virtio_device *vdev)
{
    pr_info("virtio_comp: device removed (id=%d)\n", vdev->id.device);
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
};

module_virtio_driver(virtio_comp_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Minimal Virtio Comp Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name <you@example.com>");
