/*
 * SSTF IO Scheduler
 *
 * For Linux 4.13.9. Based on the course skeleton.
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

struct sstf_data {
	struct list_head queue;
	sector_t last_sector;
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

/* Despacha a requisicao mais proxima do ultimo setor atendido. */
static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *nd = q->elevator->elevator_data;
	struct request *rq;
	struct request *best = NULL;
	sector_t sector;
	sector_t distance;
	sector_t best_distance = 0;

	list_for_each_entry(rq, &nd->queue, queuelist) {
		sector = blk_rq_pos(rq);
		distance = sector >= nd->last_sector ?
			sector - nd->last_sector : nd->last_sector - sector;

		if (!best || distance < best_distance) {
			best = rq;
			best_distance = distance;
		}
	}

	if (!best)
		return 0;

	nd->last_sector = blk_rq_pos(best);
	list_del_init(&best->queuelist);
	elv_dispatch_sort(q, best);
	printk(KERN_INFO "[SSTF] dsp R %llu\n",
	       (unsigned long long)blk_rq_pos(best));
	return 1;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	list_add_tail(&rq->queuelist, &nd->queue);
	printk(KERN_INFO "[SSTF] add R %llu\n",
	       (unsigned long long)blk_rq_pos(rq));
}

static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *nd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = nd;
	INIT_LIST_HEAD(&nd->queue);
	nd->last_sector = 0;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn = sstf_merged_requests,
		.elevator_dispatch_fn = sstf_dispatch,
		.elevator_add_req_fn = sstf_add_request,
		.elevator_init_fn = sstf_init_queue,
		.elevator_exit_fn = sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_AUTHOR("Miguel Xavier; TP2");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler");
