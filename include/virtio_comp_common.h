/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _VIRTIO_COMP_COMMON_H
#define _VIRTIO_COMP_COMMON_H

#include <linux/virtio.h>
#include <linux/spinlock.h>
#include <crypto/engine.h>
#include "virtio_comp.h"


/* Internal representation of a data virtqueue */
struct data_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* To protect the vq operations for the dataq */
	spinlock_t lock;

	/* Name of the tx queue: dataq.$index */
	char name[32];

	struct crypto_engine *engine;
};

struct virtio_comp {
	struct virtio_device *vdev;
	struct virtqueue *ctrl_vq;
	struct data_queue *data_vq;

	/* To protect the vq operations for the controlq */
	spinlock_t ctrl_lock;

	/* Maximum of data queues supported by the device */
	u32 max_data_queues;

	/* Number of queue currently used by the driver */
	u32 curr_queue;

	/*
	 * Specifies the services mask which the device support,
	 * see VIRTIO_COMP_SERVICE_*
	 */
	u32 comp_services;

	/* Detailed algorithms mask */
	u32 comp_algo;
	u32 hash_algo;

	/* Maximum size of per request */
	u64 max_size;

	/* Control VQ buffers: protected by the ctrl_lock */
	struct virtio_comp_op_ctrl_req ctrl;
	struct virtio_comp_session_input input;
	struct virtio_comp_inhdr ctrl_status;

	unsigned long status;
	atomic_t ref_count;
	struct list_head list;
	struct module *owner;
	uint8_t dev_id;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;
};

struct virtio_comp_sym_session_info {
	/* Backend session id, which come from the host side */
	__u64 session_id;
};

struct virtio_comp_request;
typedef void (*virtio_comp_data_callback)
		(struct virtio_comp_request *vc_req, int len);

struct virtio_comp_request {
	uint8_t status;
	struct virtio_comp_op_data_req *req_data;
	struct scatterlist **sgs;
	struct data_queue *dataq;
	virtio_comp_data_callback alg_cb;
};

int virtcomp_devmgr_add_dev(struct virtio_comp *vcomp_dev);
struct list_head *virtcomp_devmgr_get_head(void);
void virtcomp_devmgr_rm_dev(struct virtio_comp *vcomp_dev);
struct virtio_comp *virtcomp_devmgr_get_first(void);
int virtcomp_dev_in_use(struct virtio_comp *vcomp_dev);
int virtcomp_dev_get(struct virtio_comp *vcomp_dev);
void virtcomp_dev_put(struct virtio_comp *vcomp_dev);
int virtcomp_dev_started(struct virtio_comp *vcomp_dev);
bool virtcomp_algo_is_supported(struct virtio_comp *vcomp_dev,
				  uint32_t service,
				  uint32_t algo);
struct virtio_comp *virtcomp_get_dev_node(int node,
					      uint32_t service,
					      uint32_t algo);
int virtcomp_dev_start(struct virtio_comp *vcomp);
void virtcomp_dev_stop(struct virtio_comp *vcomp);
int virtio_comp_skcipher_crypt_req(
	struct crypto_engine *engine, void *vreq);

void
virtcomp_clear_request(struct virtio_comp_request *vc_req);

static inline int virtio_comp_get_current_node(void)
{
	int cpu, node;

	cpu = get_cpu();
	node = topology_physical_package_id(cpu);
	put_cpu();

	return node;
}

int virtio_comp_algs_register(struct virtio_comp *vcomp);
void virtio_comp_algs_unregister(struct virtio_comp *vcomp);

#endif /* _VIRTIO_COMP_COMMON_H */
