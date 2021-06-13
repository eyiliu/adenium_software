/*
 * DMA driver for Xilinx Video DMA Engine
 *
 * Copyright (C) 2010-2014 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 * The AXI Video Direct Memory Access (AXI VDMA) core is a soft Xilinx IP
 * core that provides high-bandwidth direct memory access between memory
 * and AXI4-Stream type video target peripherals. The core provides efficient
 * two dimensional DMA operations with independent asynchronous read (S2MM)
 * and write (MM2S) channel operation. It can be configured to have either
 * one channel or two channels. If configured as two channels, one is to
 * transmit to the video device (MM2S) and another is to receive from the
 * video device (S2MM). Initialization, status, interrupt and management
 * registers are accessed through an AXI4-Lite slave interface.
 *
 * The AXI Direct Memory Access (AXI DMA) core is a soft Xilinx IP core that
 * provides high-bandwidth one dimensional direct memory access between memory
 * and AXI4-Stream target peripherals. It supports one receive and one
 * transmit channel, both of them optional at synthesis time.
 *
 * The AXI CDMA, is a soft IP, which provides high-bandwidth Direct Memory
 * Access (DMA) between a memory-mapped source address and a memory-mapped
 * destination address.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This version has been modified and simplified for simplified usage
 */

//#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io-64-nonatomic-lo-hi.h>
/* #include <linux/compiler-gcc.h> */
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/preempt.h> /*smp_wmb() ??*/
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/miscdevice.h> /*my_cdev_struct*/
#include <linux/spinlock.h> /*spinlocks spin_lock_irqsave*/

/* #include <linux/dmaengine.h> */

#include "xilinx_dma.h"
#include "dmaengine.h"
#include "axidmachar.h"

static uint bufsize_rd = 1 * 1024 * 1024;
static uint bufsize_wr = 1 * 64 * 1024;
static uint bufcount_rd = 200;
static uint bufcount_wr = 1;
module_param(bufsize_rd, uint, S_IRUGO);
module_param(bufsize_wr, uint, S_IRUGO);
module_param(bufcount_rd, uint, S_IRUGO);
module_param(bufcount_wr, uint, S_IRUGO);

static int cnt_dma_rd = 0; //counter of transactions for dma operations
static int cnt_trans_rd = 0; //counter of transactions for read - how many buffers were processed

struct device *dev; /*the dma device pointer*/
typedef enum {
  BS_UNALLOCATED = 0, /*no address is given yet*/
  BS_ALLOCATED = 1, /*the buffer has been allocated in the memory*/
  BS_READY_FOR_DMA = 2, /*this buffer can be used for dma*/
  BS_DMA_ISSUED = 3, /*dma has been started on this buffer, but not yet finished*/
  BS_DMA_FINISHED_OK = 4, /*dma transfer has been successfully finished. */
  BS_DMA_FINISHED_FAIL = 5,/*dma transfer finished with an error*/
  BS_SYNCED_FOR_CPU = 6, /*data in the buffer can be read by the CPU*/
  BS_PROCESSED = 7 /*all data has be read out*/
} buffer_state;

struct dma_rx_buf_t {
  u8 * buf; /*the virtual address of the buffer*/
  dma_addr_t hwaddr; /*the hardware address of the buffer*/
  buffer_state bs; /* state of this buffer*/
  int bytes_received;/*how many bytes have been received from the DMA*/
  int bytes_processed; /*how many bytes have already sent out*/
};

struct dma_tx_buf_t {
  u8 * buf; /*the virtual address of the buffer*/
  dma_addr_t hwaddr; /*the hardware address of the buffer*/
  int bytes_stored;/*how many bytes have been received from the write()*/
  int bytes_processed; /*how many bytes have already sent out*/
};

static struct dma_rx_buf_t *dma_rx_bufs;/*array with the buffer information structures*/
static struct dma_tx_buf_t *dma_tx_bufs;/*array with the buffer information structures*/
static int rx_buf_store_index = 0;/* Index of currently running buffer or where the DMA should start */
static int rx_buf_process_index = 0;/* Index of currently running buffer */
static struct xilinx_dma_chan *rx_chan;
static struct xilinx_dma_chan *tx_chan;

static struct task_struct *rx_thread;
int rx_thread_wq_condition;
DECLARE_WAIT_QUEUE_HEAD(rx_thread_wq);
static DEFINE_SPINLOCK(dma_register_rd_lock);
static DEFINE_SPINLOCK(dma_register_wr_lock);

/*char device stuff*/
struct class *crdev_class_rd;
struct class *crdev_class_wr;
static struct semaphore sem_rd;
static struct semaphore sem_wr;

/* character device functions */
static int charrd_open(struct inode *, struct file *);
static int charrd_close(struct inode *, struct file *);
static ssize_t charrd_read(struct file *, char __user *, size_t, loff_t *);

static int charwr_open(struct inode *, struct file *);
static int charwr_close(struct inode *, struct file *);
static ssize_t charwr_write(struct file *, const char __user *, size_t, loff_t *);
static struct file_operations fops_rd = { .owner = THIS_MODULE, // pointer to the module struct
                                          .open = charrd_open,     //
                                          .release = charrd_close, //
                                          .read = charrd_read     //
};

/* -----------------------------------------------------------------------------
 * Descriptors and segments alloc and free
 */


/**
 * xilinx_axidma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_axidma_tx_segment *
xilinx_axidma_alloc_tx_segment(struct xilinx_dma_chan *chan) {
  struct xilinx_axidma_tx_segment *segment = NULL;
  unsigned long flags;

  spin_lock_irqsave(&chan->lock, flags);
  if (!list_empty(&chan->free_seg_list)) {
    segment = list_first_entry(&chan->free_seg_list, struct xilinx_axidma_tx_segment, node);
    list_del(&segment->node);
  }
  spin_unlock_irqrestore(&chan->lock, flags);

  return segment;
}

static void xilinx_dma_clean_hw_desc(struct xilinx_axidma_desc_hw *hw) {
  u32 next_desc = hw->next_desc;
  u32 next_desc_msb = hw->next_desc_msb;

  memset(hw, 0, sizeof(struct xilinx_axidma_desc_hw));

  hw->next_desc = next_desc;
  hw->next_desc_msb = next_desc_msb;
}

/**
 * xilinx_dma_free_tx_segment - Free transaction segment
 * @chan: Driver specific DMA channel
 * @segment: DMA transaction segment
 */
static void xilinx_dma_free_tx_segment(struct xilinx_dma_chan *chan, struct xilinx_axidma_tx_segment *segment) {
  xilinx_dma_clean_hw_desc(&segment->hw);

  list_add_tail(&segment->node, &chan->free_seg_list);
}

/**
 * xilinx_dma_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xilinx_dma_tx_descriptor *
xilinx_dma_alloc_tx_descriptor(struct xilinx_dma_chan *chan) {
  struct xilinx_dma_tx_descriptor *desc;

  desc = kzalloc(sizeof(*desc), GFP_KERNEL);
  if (!desc)
    return NULL;

  INIT_LIST_HEAD(&desc->segments);

  return desc;
}

/**
 * xilinx_dma_free_tx_descriptor - Free transaction descriptor
 * @chan: Driver specific DMA channel
 * @desc: DMA transaction descriptor
 */
static void xilinx_dma_free_tx_descriptor(struct xilinx_dma_chan *chan, struct xilinx_dma_tx_descriptor *desc) {
  struct xilinx_axidma_tx_segment *axidma_segment, *axidma_next;

  if (!desc)
    return;

  list_for_each_entry_safe(axidma_segment, axidma_next,
                           &desc->segments, node)
  {
    list_del(&axidma_segment->node);
    xilinx_dma_free_tx_segment(chan, axidma_segment);
  }

  kfree(desc);
}

/* Required functions */

/**
 * xilinx_dma_free_desc_list - Free descriptors list
 * @chan: Driver specific DMA channel
 * @list: List to parse and delete the descriptor
 */
static void xilinx_dma_free_desc_list(struct xilinx_dma_chan *chan, struct list_head *list) {
  struct xilinx_dma_tx_descriptor *desc, *next;

  list_for_each_entry_safe(desc, next, list, node)
  {
    list_del(&desc->node);
    xilinx_dma_free_tx_descriptor(chan, desc);
  }
}

/**
 * xilinx_dma_free_descriptors - Free channel descriptors
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_free_descriptors(struct xilinx_dma_chan *chan) {
  unsigned long flags;

  spin_lock_irqsave(&chan->lock, flags);

  xilinx_dma_free_desc_list(chan, &chan->pending_list);
  xilinx_dma_free_desc_list(chan, &chan->done_list);
  xilinx_dma_free_desc_list(chan, &chan->active_list);

  spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xilinx_dma_free_chan_resources(struct dma_chan *dchan) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  unsigned long flags;

  dev_dbg(chan->dev, "Free all channel resources.\n");

  xilinx_dma_free_descriptors(chan);

  spin_lock_irqsave(&chan->lock, flags);
  INIT_LIST_HEAD(&chan->free_seg_list);
  spin_unlock_irqrestore(&chan->lock, flags);

  /* Free memory that is allocated for BD */
  dma_free_coherent(chan->dev, sizeof(*chan->seg_v) *
                    XILINX_DMA_NUM_DESCS, chan->seg_v, chan->seg_p);

  /* Free Memory that is allocated for cyclic DMA Mode */
  dma_free_coherent(chan->dev, sizeof(*chan->cyclic_seg_v), chan->cyclic_seg_v, chan->cyclic_seg_p);
}

/**
 * xilinx_dma_chan_handle_cyclic - Cyclic dma callback
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 * @flags: flags for spin lock
 */
static void xilinx_dma_chan_handle_cyclic(struct xilinx_dma_chan *chan, struct xilinx_dma_tx_descriptor *desc, unsigned long *flags) {
  dma_async_tx_callback callback;
  void *callback_param;

  callback = desc->async_tx.callback;
  callback_param = desc->async_tx.callback_param;
  if (callback) {
    spin_unlock_irqrestore(&chan->lock, *flags);
    callback(callback_param);
    spin_lock_irqsave(&chan->lock, *flags);
  }
}

/**
 * xilinx_dma_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_chan_desc_cleanup(struct xilinx_dma_chan *chan) {
  struct xilinx_dma_tx_descriptor *desc, *next;
  unsigned long flags;

  spin_lock_irqsave(&chan->lock, flags);

  list_for_each_entry_safe(desc, next, &chan->done_list, node)
  {
    struct dmaengine_desc_callback cb;

    if (desc->cyclic) {
      xilinx_dma_chan_handle_cyclic(chan, desc, &flags);
      break;
    }

    /* Remove from the list of running transactions */
    list_del(&desc->node);

    /* Run the link descriptor callback function */
    dmaengine_desc_get_callback(&desc->async_tx, &cb);
    if (dmaengine_desc_callback_valid(&cb)) {
      spin_unlock_irqrestore(&chan->lock, flags);
      dmaengine_desc_callback_invoke(&cb, NULL);
      spin_lock_irqsave(&chan->lock, flags);
    }

    /* Run any dependencies, then free the descriptor */
    dma_run_dependencies(&desc->async_tx);
    xilinx_dma_free_tx_descriptor(chan, desc);
  }

  spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_do_tasklet - Schedule completion tasklet
 * @data: Pointer to the Xilinx DMA channel structure
 */
static void xilinx_dma_do_tasklet(unsigned long data) {
  struct xilinx_dma_chan *chan = (struct xilinx_dma_chan *) data;

  xilinx_dma_chan_desc_cleanup(chan);
}

/**
 * xilinx_dma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_alloc_chan_resources(struct dma_chan *dchan) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  int i;

  /* Has this channel already been allocated? */
  if (chan->desc_pool)
    return 0;

  /*
   * We need the descriptor to be aligned to 64bytes
   * for meeting Xilinx VDMA specification requirement.
   */
  /* Allocate the buffer descriptors. */
  chan->seg_v = dma_zalloc_coherent(chan->dev, sizeof(*chan->seg_v) *
                                    XILINX_DMA_NUM_DESCS, &chan->seg_p, GFP_KERNEL);
  if (!chan->seg_v) {
    dev_err(chan->dev, "unable to allocate channel %d descriptors\n", chan->id);
    return -ENOMEM;
  }
  /*
   * For cyclic DMA mode we need to program the tail Descriptor
   * register with a value which is not a part of the BD chain
   * so allocating a desc segment during channel allocation for
   * programming tail descriptor.
   */
  chan->cyclic_seg_v = dma_zalloc_coherent(chan->dev, sizeof(*chan->cyclic_seg_v), &chan->cyclic_seg_p, GFP_KERNEL);
  if (!chan->cyclic_seg_v) {
    dev_err(chan->dev, "unable to allocate desc segment for cyclic DMA\n");
    dma_free_coherent(chan->dev, sizeof(*chan->seg_v) *
                      XILINX_DMA_NUM_DESCS, chan->seg_v, chan->seg_p);
    return -ENOMEM;
  }
  chan->cyclic_seg_v->phys = chan->cyclic_seg_p;

  for (i = 0; i < XILINX_DMA_NUM_DESCS; i++) {
    chan->seg_v[i].hw.next_desc = lower_32_bits(chan->seg_p + sizeof(*chan->seg_v) * ((i + 1) % XILINX_DMA_NUM_DESCS));
    chan->seg_v[i].hw.next_desc_msb = upper_32_bits(chan->seg_p + sizeof(*chan->seg_v) * ((i + 1) % XILINX_DMA_NUM_DESCS));
    chan->seg_v[i].phys = chan->seg_p + sizeof(*chan->seg_v) * i;
    list_add_tail(&chan->seg_v[i].node, &chan->free_seg_list);
  }


  dma_cookie_init(dchan);

  /* For AXI DMA resetting once channel will reset the
   * other channel as well so enable the interrupts here.
   */
  dma_ctrl_set(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMAXR_ALL_IRQ_MASK);

  return 0;
}

/**
 * xilinx_dma_tx_status - Get DMA transaction status
 * @dchan: DMA channel
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status
 */
static enum dma_status xilinx_dma_tx_status(struct dma_chan *dchan, dma_cookie_t cookie, struct dma_tx_state *txstate) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  struct xilinx_dma_tx_descriptor *desc;
  struct xilinx_axidma_tx_segment *segment;
  struct xilinx_axidma_desc_hw *hw;
  enum dma_status ret;
  unsigned long flags;
  u32 residue = 0;

  ret = dma_cookie_status(dchan, cookie, txstate);
  if (ret == DMA_COMPLETE || !txstate)
    return ret;

  spin_lock_irqsave(&chan->lock, flags);

  desc = list_last_entry(&chan->active_list, struct xilinx_dma_tx_descriptor, node);
  if (chan->has_sg) {
    list_for_each_entry(segment, &desc->segments, node)
    {
      hw = &segment->hw;
      residue += (hw->control - hw->status) & chan->xdev->max_buffer_len;
    }
  }
  spin_unlock_irqrestore(&chan->lock, flags);

  chan->residue = residue;
  dma_set_residue(txstate, chan->residue);

  return ret;
}

/**
 * xilinx_dma_stop_transfer - Halt DMA channel
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on DMA stop and and failure value on error
 */
static int xilinx_dma_stop_transfer(struct xilinx_dma_chan *chan) {
  u32 val;

  dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RUNSTOP);

  /* Wait for the hardware to halt */
  return xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMASR, val, val & XILINX_DMA_DMASR_HALTED, 0, XILINX_DMA_LOOP_COUNT);
}

/**
 * xilinx_dma_start - Start DMA channel
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_start(struct xilinx_dma_chan *chan) {
  int err;
  u32 val;

  dma_ctrl_set(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RUNSTOP);

  /* Wait for the hardware to start */
  err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMASR, val, !(val & XILINX_DMA_DMASR_HALTED), 0, XILINX_DMA_LOOP_COUNT);

  if (err) {
    dev_err(chan->dev, "Cannot start channel %p: %x\n", chan, dma_ctrl_read(chan, XILINX_DMA_REG_DMASR));

    chan->err = true;
  }
}

/**
 * xilinx_dma_start_transfer - Starts DMA transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_dma_start_transfer(struct xilinx_dma_chan *chan) {
  struct xilinx_dma_tx_descriptor *head_desc, *tail_desc;
  struct xilinx_axidma_tx_segment *tail_segment;
  u32 reg;

  if (chan->err)
    return;

  if (!chan->idle)
    return;

  if (list_empty(&chan->pending_list))
    return;

  head_desc = list_first_entry(&chan->pending_list, struct xilinx_dma_tx_descriptor, node);
  tail_desc = list_last_entry(&chan->pending_list, struct xilinx_dma_tx_descriptor, node);
  tail_segment = list_last_entry(&tail_desc->segments, struct xilinx_axidma_tx_segment, node);

  reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);

  if (chan->desc_pendingcount <= XILINX_DMA_COALESCE_MAX) {
    reg &= ~XILINX_DMA_CR_COALESCE_MAX;
    reg |= chan->desc_pendingcount <<
      XILINX_DMA_CR_COALESCE_SHIFT;
    dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);
  }

  if (chan->has_sg && !chan->xdev->mcdma)
    xilinx_write(chan, XILINX_DMA_REG_CURDESC, head_desc->async_tx.phys);

  if (chan->has_sg && chan->xdev->mcdma) {
    if (chan->direction == DMA_MEM_TO_DEV) {
      dma_ctrl_write(chan, XILINX_DMA_REG_CURDESC, head_desc->async_tx.phys);
    } else {
      if (!chan->tdest) {
        dma_ctrl_write(chan, XILINX_DMA_REG_CURDESC, head_desc->async_tx.phys);
      } else {
        dma_ctrl_write(chan, XILINX_DMA_MCRX_CDESC(chan->tdest), head_desc->async_tx.phys);
      }
    }
  }

  xilinx_dma_start(chan);

  if (chan->err)
    return;

  /* Start the transfer */
  if (chan->has_sg && !chan->xdev->mcdma) {
    if (chan->cyclic)
      xilinx_write(chan, XILINX_DMA_REG_TAILDESC, chan->cyclic_seg_v->phys);
    else
      xilinx_write(chan, XILINX_DMA_REG_TAILDESC, tail_segment->phys);
  } else if (chan->has_sg && chan->xdev->mcdma) {
    if (chan->direction == DMA_MEM_TO_DEV) {
      dma_ctrl_write(chan, XILINX_DMA_REG_TAILDESC, tail_segment->phys);
    } else {
      if (!chan->tdest) {
        dma_ctrl_write(chan, XILINX_DMA_REG_TAILDESC, tail_segment->phys);
      } else {
        dma_ctrl_write(chan, XILINX_DMA_MCRX_TDESC(chan->tdest), tail_segment->phys);
      }
    }
  } else {
    struct xilinx_axidma_tx_segment *segment;
    struct xilinx_axidma_desc_hw *hw;

    segment = list_first_entry(&head_desc->segments, struct xilinx_axidma_tx_segment, node);
    hw = &segment->hw;

    xilinx_write(chan, XILINX_DMA_REG_SRCDSTADDR, hw->buf_addr);

    /* Start the transfer */
    dma_ctrl_write(chan, XILINX_DMA_REG_BTT, hw->control & chan->xdev->max_buffer_len);
  }

  list_splice_tail_init(&chan->pending_list, &chan->active_list);
  chan->desc_pendingcount = 0;
  chan->idle = false;
}

/**
 * xilinx_dma_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xilinx_dma_issue_pending(struct dma_chan *dchan) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  unsigned long flags;

  spin_lock_irqsave(&chan->lock, flags);
  chan->start_transfer(chan);
  spin_unlock_irqrestore(&chan->lock, flags);
}


/**
 * xilinx_dma_reset - Reset DMA channel
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_reset(struct xilinx_dma_chan *chan) {
  int err;
  u32 tmp;

  dma_ctrl_set(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RESET);

  /* Wait for the hardware to finish reset */
  err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMACR, tmp, !(tmp & XILINX_DMA_DMACR_RESET), 0, XILINX_DMA_LOOP_COUNT);

  if (err) {
    dev_err(chan->dev, "reset timeout, cr %x, sr %x\n", dma_ctrl_read(chan, XILINX_DMA_REG_DMACR), dma_ctrl_read(chan, XILINX_DMA_REG_DMASR));
    return -ETIMEDOUT;
  }

  chan->err = false;
  chan->idle = true;
  chan->desc_submitcount = 0;

  return err;
}

/**
 * xilinx_dma_chan_reset - Reset DMA channel and enable interrupts
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_reset(struct xilinx_dma_chan *chan) {
  int err;

  /* Reset VDMA */
  err = xilinx_dma_reset(chan);
  if (err)
    return err;

  /* Enable interrupts */
  dma_ctrl_set(chan, XILINX_DMA_REG_DMACR,
               XILINX_DMA_DMAXR_ALL_IRQ_MASK);

  return 0;
}

static int rx_resume(struct xilinx_dma_chan *chan, bool do_reset) {
  int err = 0;

  if (!chan->idle)
    return err;

  unsigned long flags;
  spin_lock_irqsave(&dma_register_rd_lock, flags);

  if (dma_rx_bufs[rx_buf_store_index].bs == BS_READY_FOR_DMA) { /*start a new transfer*/
    if (do_reset) {
      xilinx_dma_reset(rx_chan);
      if (chan->err)
        return -ETIME;
    }
    /*start the channel*/
    xilinx_dma_start(rx_chan);
    /*enable interrupt*/
    dma_ctrl_set(rx_chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_ERR_IRQ | XILINX_DMA_DMACR_FRM_CNT_IRQ);
    /*write destination address*/
    dma_ctrl_write(rx_chan, XILINX_DMA_REG_SRCDSTADDR, dma_rx_bufs[rx_buf_store_index].hwaddr);
    /*write length*/
    dma_ctrl_write(rx_chan, XILINX_DMA_REG_LENGTH, bufsize_rd);
    chan->idle = false;
  } else {
    spin_unlock_irqrestore(&dma_register_rd_lock, flags);
    return -ENOBUFS;
  }
  spin_unlock_irqrestore(&dma_register_rd_lock, flags);
  return err;
}

static irqreturn_t modified_rx_irq_handler(int irq, void *data) {
  struct xilinx_dma_chan *chan = data;
  spin_lock(&dma_register_rd_lock);
  int next_dma_buf_index = rx_buf_store_index + 1;
  if (next_dma_buf_index == bufcount_rd) {
    next_dma_buf_index = 0;
  }
  u32 length = dma_ctrl_read(chan, XILINX_DMA_REG_LENGTH);
  u32 status = dma_ctrl_read(chan, XILINX_DMA_REG_DMASR);

  dma_ctrl_write(chan, XILINX_DMA_REG_DMASR, status & XILINX_DMA_DMAXR_ALL_IRQ_MASK);/*clear interrupt*/
  dma_rx_bufs[rx_buf_store_index].bytes_processed = 0;
  dma_rx_bufs[rx_buf_store_index].bytes_received = length;
  /*state has to be written last*/
  smp_wmb();
  if (unlikely(status & XILINX_DMA_DMASR_DMA_INT_ERR)) {
    dev_err(dev, "error occured in buf %d, reseting, going to next\n", rx_buf_store_index);
    xilinx_dma_reset(chan);
    dma_rx_bufs[rx_buf_store_index].bs = BS_DMA_FINISHED_FAIL;
  } else {
    dma_rx_bufs[rx_buf_store_index].bs = BS_DMA_FINISHED_OK;
  }
  cnt_dma_rd++;
  rx_buf_store_index = next_dma_buf_index;
  if (dma_rx_bufs[rx_buf_store_index].bs == BS_READY_FOR_DMA) { /*start a new transfer*/
    xilinx_dma_start(chan); /*start the channel*/
    dma_ctrl_write(chan, XILINX_DMA_REG_SRCDSTADDR, dma_rx_bufs[next_dma_buf_index].hwaddr);/*write destination address*/
    dma_ctrl_write(chan, XILINX_DMA_REG_LENGTH, bufsize_rd);/*write number of bytes to transfer to the LENGTH register*/
    chan->idle = false;
  } else {
    chan->idle = true;
  }
  spin_unlock(&dma_register_rd_lock);

  //wakeup unreliable.better do a loop
//	rx_thread_wq_condition = 1;
//	wake_up_process(rx_thread);
  //	wake_up_interruptible(&rx_thread_wq);
  return IRQ_HANDLED;
}


/**
 * append_desc_queue - Queuing descriptor
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 */
static void append_desc_queue(struct xilinx_dma_chan *chan, struct xilinx_dma_tx_descriptor *desc) {
  struct xilinx_dma_tx_descriptor *tail_desc;
  struct xilinx_axidma_tx_segment *axidma_tail_segment;

  if (list_empty(&chan->pending_list))
    goto append;

  /*
   * Add the hardware descriptor to the chain of hardware descriptors
   * that already exists in memory.
   */
  tail_desc = list_last_entry(&chan->pending_list, struct xilinx_dma_tx_descriptor, node);
  axidma_tail_segment = list_last_entry(&tail_desc->segments, struct xilinx_axidma_tx_segment, node);
  axidma_tail_segment->hw.next_desc = (u32) desc->async_tx.phys;

  /*
   * Add the software descriptor and all children to the list
   * of pending transactions
   */
append: list_add_tail(&desc->node, &chan->pending_list);
  chan->desc_pendingcount++;

}

/**
 * xilinx_dma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx) {
  struct xilinx_dma_tx_descriptor *desc = to_dma_tx_descriptor(tx);
  struct xilinx_dma_chan *chan = to_xilinx_chan(tx->chan);
  dma_cookie_t cookie;
  unsigned long flags;
  int err;

  if (chan->cyclic) {
    xilinx_dma_free_tx_descriptor(chan, desc);
    return -EBUSY;
  }

  if (chan->err) {
    /*
     * If reset fails, need to hard reset the system.
     * Channel is no longer functional
     */
    err = xilinx_dma_chan_reset(chan);
    if (err < 0)
      return err;
  }

  spin_lock_irqsave(&chan->lock, flags);

  cookie = dma_cookie_assign(tx);

  /* Put this transaction onto the tail of the pending queue */
  append_desc_queue(chan, desc);

  if (desc->cyclic)
    chan->cyclic = true;

  spin_unlock_irqrestore(&chan->lock, flags);

  return cookie;
}


/**
 * xilinx_dma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 * @context: APP words of the descriptor
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl, unsigned int sg_len,
                                                                enum dma_transfer_direction direction, unsigned long flags, void *context) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  struct xilinx_dma_tx_descriptor *desc;
  struct xilinx_axidma_tx_segment *segment = NULL;
  u32 *app_w = (u32 *) context;
  struct scatterlist *sg;
  size_t copy;
  size_t sg_used;
  unsigned int i;

  if (!is_slave_direction(direction))
    return NULL;

  /* Allocate a transaction descriptor. */
  desc = xilinx_dma_alloc_tx_descriptor(chan);
  if (!desc)
    return NULL;

  dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
  desc->async_tx.tx_submit = xilinx_dma_tx_submit;

  /* Build transactions using information in the scatter gather list */
  for_each_sg(sgl, sg, sg_len, i)
  {
    sg_used = 0;

    /* Loop until the entire scatterlist entry is used */
    while (sg_used < sg_dma_len(sg)) {
      struct xilinx_axidma_desc_hw *hw;

      /* Get a free segment */
      segment = xilinx_axidma_alloc_tx_segment(chan);
      if (!segment)
        goto error;

      /*
       * Calculate the maximum number of bytes to transfer,
       * making sure it is less than the hw limit
       */
      copy = min_t(size_t, sg_dma_len(sg) - sg_used, chan->xdev->max_buffer_len);
      hw = &segment->hw;

      /* Fill in the descriptor */
      xilinx_axidma_buf(chan, hw, sg_dma_address(sg), sg_used, 0);

      hw->control = copy;

      if (chan->direction == DMA_MEM_TO_DEV) {
        if (app_w)
          memcpy(hw->app, app_w, sizeof(u32) *
                 XILINX_DMA_NUM_APP_WORDS);
      }

      sg_used += copy;

      /*
       * Insert the segment into the descriptor segments
       * list.
       */
      list_add_tail(&segment->node, &desc->segments);
    }
  }

  segment = list_first_entry(&desc->segments, struct xilinx_axidma_tx_segment, node);
  desc->async_tx.phys = segment->phys;

  /* For the last DMA_MEM_TO_DEV transfer, set EOP */
  if (chan->direction == DMA_MEM_TO_DEV) {
    segment->hw.control |= XILINX_DMA_BD_SOP;
    segment = list_last_entry(&desc->segments, struct xilinx_axidma_tx_segment, node);
    segment->hw.control |= XILINX_DMA_BD_EOP;
  }

  return &desc->async_tx;

error: xilinx_dma_free_tx_descriptor(chan, desc);
  return NULL;
}

/**
 * xilinx_dma_prep_dma_cyclic - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @buf_addr: Physical address of the buffer
 * @buf_len: Total length of the cyclic buffers
 * @period_len: length of individual cyclic buffer
 * @direction: DMA direction
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_dma_cyclic(struct dma_chan *dchan, dma_addr_t buf_addr, size_t buf_len, size_t period_len,
                                                                  enum dma_transfer_direction direction, unsigned long flags)
{
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  struct xilinx_dma_tx_descriptor *desc;
  struct xilinx_axidma_tx_segment *segment, *head_segment, *prev = NULL;
  size_t copy, sg_used;
  unsigned int num_periods;
  int i;
  u32 reg;

  if (!period_len)
    return NULL;

  num_periods = buf_len / period_len;

  if (!num_periods)
    return NULL;

  if (!is_slave_direction(direction))
    return NULL;

  /* Allocate a transaction descriptor. */
  desc = xilinx_dma_alloc_tx_descriptor(chan);
  if (!desc)
    return NULL;

  chan->direction = direction;
  dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
  desc->async_tx.tx_submit = xilinx_dma_tx_submit;

  for (i = 0; i < num_periods; ++i) {
    sg_used = 0;

    while (sg_used < period_len) {
      struct xilinx_axidma_desc_hw *hw;

      /* Get a free segment */
      segment = xilinx_axidma_alloc_tx_segment(chan);
      if (!segment)
        goto error;

      /*
       * Calculate the maximum number of bytes to transfer,
       * making sure it is less than the hw limit
       */
      copy = min_t(size_t, period_len - sg_used,
                   chan->xdev->max_buffer_len);
      hw = &segment->hw;
      xilinx_axidma_buf(chan, hw, buf_addr, sg_used,
                        period_len * i);
      hw->control = copy;

      if (prev)
        prev->hw.next_desc = segment->phys;

      prev = segment;
      sg_used += copy;

      /*
       * Insert the segment into the descriptor segments
       * list.
       */
      list_add_tail(&segment->node, &desc->segments);
    }
  }

  head_segment = list_first_entry(&desc->segments,
                                  struct xilinx_axidma_tx_segment, node);
  desc->async_tx.phys = head_segment->phys;

  desc->cyclic = true;
  reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);
  reg |= XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
  dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);

  segment = list_last_entry(&desc->segments,
                            struct xilinx_axidma_tx_segment,
                            node);
  segment->hw.next_desc = (u32) head_segment->phys;

  /* For the last DMA_MEM_TO_DEV transfer, set EOP */
  if (direction == DMA_MEM_TO_DEV) {
    head_segment->hw.control |= XILINX_DMA_BD_SOP;
    segment->hw.control |= XILINX_DMA_BD_EOP;
  }

  return &desc->async_tx;

error:
  xilinx_dma_free_tx_descriptor(chan, desc);
  return NULL;
}

/**
 * xilinx_dma_prep_interleaved - prepare a descriptor for a
 *	DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_dma_prep_interleaved(struct dma_chan *dchan,
                            struct dma_interleaved_template *xt,
                            unsigned long flags)
{
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  struct xilinx_dma_tx_descriptor *desc;
  struct xilinx_axidma_tx_segment *segment;
  struct xilinx_axidma_desc_hw *hw;

  if (!is_slave_direction(xt->dir))
    return NULL;

  if (!xt->numf || !xt->sgl[0].size)
    return NULL;

  if (xt->frame_size != 1)
    return NULL;

  /* Allocate a transaction descriptor. */
  desc = xilinx_dma_alloc_tx_descriptor(chan);
  if (!desc)
    return NULL;

  chan->direction = xt->dir;
  dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
  desc->async_tx.tx_submit = xilinx_dma_tx_submit;

  /* Get a free segment */
  segment = xilinx_axidma_alloc_tx_segment(chan);
  if (!segment)
    goto error;

  hw = &segment->hw;

  /* Fill in the descriptor */
  if (xt->dir != DMA_MEM_TO_DEV)
    hw->buf_addr = xt->dst_start;
  else
    hw->buf_addr = xt->src_start;

  hw->mcdma_control = chan->tdest & XILINX_DMA_BD_TDEST_MASK;
  hw->vsize_stride = (xt->numf << XILINX_DMA_BD_VSIZE_SHIFT) &
    XILINX_DMA_BD_VSIZE_MASK;
  hw->vsize_stride |= (xt->sgl[0].icg + xt->sgl[0].size) &
    XILINX_DMA_BD_STRIDE_MASK;
  hw->control = xt->sgl[0].size & XILINX_DMA_BD_HSIZE_MASK;

  /*
   * Insert the segment into the descriptor segments
   * list.
   */
  list_add_tail(&segment->node, &desc->segments);

  segment = list_first_entry(&desc->segments,
                             struct xilinx_axidma_tx_segment, node);
  desc->async_tx.phys = segment->phys;

  /* For the last DMA_MEM_TO_DEV transfer, set EOP */
  if (xt->dir == DMA_MEM_TO_DEV) {
    segment->hw.control |= XILINX_DMA_BD_SOP;
    segment = list_last_entry(&desc->segments,
                              struct xilinx_axidma_tx_segment,
                              node);
    segment->hw.control |= XILINX_DMA_BD_EOP;
  }

  return &desc->async_tx;

error:
  xilinx_dma_free_tx_descriptor(chan, desc);
  return NULL;
}

/**
 * xilinx_dma_terminate_all - Halt the channel and free descriptors
 * @dchan: Driver specific DMA Channel pointer
 *
 * Return: '0' always.
 */
static int xilinx_dma_terminate_all(struct dma_chan *dchan) {
  struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
  u32 reg;
  int err;

  if (chan->cyclic)
    xilinx_dma_chan_reset(chan);

  err = chan->stop_transfer(chan);
  if (err) {
    dev_err(chan->dev, "Cannot stop channel %p: %x\n", chan, dma_ctrl_read(chan, XILINX_DMA_REG_DMASR));
    chan->err = true;
  }

  /* Remove and free all of the descriptors in the lists */
  xilinx_dma_free_descriptors(chan);

  if (chan->cyclic) {
    reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);
    reg &= ~XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
    dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);
    chan->cyclic = false;
  }

  chan->idle = true;
  return 0;
}


/* -----------------------------------------------------------------------------
 * Probe and remove
 */

/**
 * xilinx_dma_chan_remove - Per Channel remove function
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_chan_remove(struct xilinx_dma_chan *chan) {
  /* Disable all interrupts */
  dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR,
               XILINX_DMA_DMAXR_ALL_IRQ_MASK);

  if (chan->irq > 0)
    free_irq(chan->irq, chan);

  tasklet_kill(&chan->tasklet);

  list_del(&chan->common.device_node);
}

static int axidma_clk_init(struct platform_device *pdev, struct clk **axi_clk, struct clk **tx_clk, struct clk **rx_clk, struct clk **sg_clk,
                           struct clk **tmp_clk) {
  int err;

  *tmp_clk = NULL;

  *axi_clk = devm_clk_get(&pdev->dev, "s_axi_lite_aclk");
  if (IS_ERR(*axi_clk)) {
    err = PTR_ERR(*axi_clk);
    dev_err(&pdev->dev, "failed to get axi_aclk (%d)\n", err);
    return err;
  }

  *tx_clk = devm_clk_get(&pdev->dev, "m_axi_mm2s_aclk");
  if (IS_ERR(*tx_clk))
    *tx_clk = NULL;

  *rx_clk = devm_clk_get(&pdev->dev, "m_axi_s2mm_aclk");
  if (IS_ERR(*rx_clk))
    *rx_clk = NULL;

  *sg_clk = devm_clk_get(&pdev->dev, "m_axi_sg_aclk");
  if (IS_ERR(*sg_clk))
    *sg_clk = NULL;

  err = clk_prepare_enable(*axi_clk);
  if (err) {
    dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n", err);
    return err;
  }

  err = clk_prepare_enable(*tx_clk);
  if (err) {
    dev_err(&pdev->dev, "failed to enable tx_clk (%d)\n", err);
    goto err_disable_axiclk;
  }

  err = clk_prepare_enable(*rx_clk);
  if (err) {
    dev_err(&pdev->dev, "failed to enable rx_clk (%d)\n", err);
    goto err_disable_txclk;
  }

  err = clk_prepare_enable(*sg_clk);
  if (err) {
    dev_err(&pdev->dev, "failed to enable sg_clk (%d)\n", err);
    goto err_disable_rxclk;
  }

  return 0;

err_disable_rxclk: clk_disable_unprepare(*rx_clk);
err_disable_txclk: clk_disable_unprepare(*tx_clk);
err_disable_axiclk: clk_disable_unprepare(*axi_clk);

  return err;
}

static void xdma_disable_allclks(struct xilinx_dma_device *xdev) {
  clk_disable_unprepare(xdev->rxs_clk);
  clk_disable_unprepare(xdev->rx_clk);
  clk_disable_unprepare(xdev->txs_clk);
  clk_disable_unprepare(xdev->tx_clk);
  clk_disable_unprepare(xdev->axi_clk);
}

/**
 * xilinx_dma_chan_probe - Per Channel Probing
 * It get channel features from the device tree entry and
 * initialize special channel handling routines
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 * @chan_id: DMA Channel id
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_probe(struct xilinx_dma_device *xdev, struct device_node *node, int chan_id) {
  struct xilinx_dma_chan *chan;
  bool has_dre = false;
  u32 value, width;
  int err;

  /* Allocate and initialize the channel structure */
  chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
  if (!chan)
    return -ENOMEM;

  chan->dev = xdev->dev;
  chan->xdev = xdev;
  chan->has_sg = xdev->has_sg;
  chan->desc_pendingcount = 0x0;
  chan->ext_addr = xdev->ext_addr;
  /* This variable enusres that descripotrs are not
   * Submited when dma engine is in progress. This variable is
   * Added to avoid pollling for a bit in the status register to
   * Know dma state in the driver hot path.
   */
  chan->idle = true;

  spin_lock_init(&chan->lock);
  INIT_LIST_HEAD(&chan->pending_list);
  INIT_LIST_HEAD(&chan->done_list);
  INIT_LIST_HEAD(&chan->active_list);
  INIT_LIST_HEAD(&chan->free_seg_list);

  /* Retrieve the channel properties from the device tree */
  has_dre = of_property_read_bool(node, "xlnx,include-dre");

  chan->genlock = of_property_read_bool(node, "xlnx,genlock-mode");

  err = of_property_read_u32(node, "xlnx,datawidth", &value);
  if (err) {
    dev_err(xdev->dev, "missing xlnx,datawidth property\n");
    return err;
  }
  width = value >> 3; /* Convert bits to bytes */

  /* If data width is greater than 8 bytes, DRE is not in hw */
  if (width > 8)
    has_dre = false;

  if (!has_dre)
    xdev->common.copy_align = fls(width - 1);

  if (of_device_is_compatible(node, "xlnx,axi-vdma-mm2s-channel") || of_device_is_compatible(node, "xlnx,axi-dma-mm2s-channel")
      || of_device_is_compatible(node, "xlnx,axi-cdma-channel")) {
    tx_chan = chan;
    dev_info(xdev->dev, "xilinx_dma_chan_probe for tx_chan\n");
    chan->direction = DMA_MEM_TO_DEV;
    chan->id = chan_id;
    chan->tdest = chan_id;
    xdev->common.directions = BIT(DMA_MEM_TO_DEV);
    chan->ctrl_offset = XILINX_DMA_MM2S_CTRL_OFFSET;
  } else if (of_device_is_compatible(node, "xlnx,axi-vdma-s2mm-channel") || of_device_is_compatible(node, "xlnx,axi-dma-s2mm-channel")) {
    rx_chan = chan;
    dev_info(xdev->dev, "xilinx_dma_chan_probe for rx_chan\n");
    chan->direction = DMA_DEV_TO_MEM;
    chan->id = chan_id;
    chan->tdest = chan_id - xdev->nr_channels;
    xdev->common.directions |= BIT(DMA_DEV_TO_MEM);
    chan->has_vflip = of_property_read_bool(node, "xlnx,enable-vert-flip");
    if (chan->has_vflip) {
      chan->config.vflip_en = dma_read(chan,
                                       XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP) &
        XILINX_VDMA_ENABLE_VERTICAL_FLIP;
    }

    chan->ctrl_offset = XILINX_DMA_S2MM_CTRL_OFFSET;
  } else {
    dev_err(xdev->dev, "Invalid channel compatible node\n");
    return -EINVAL;
  }

  /* Request the interrupt */
  chan->irq = irq_of_parse_and_map(node, 0);
  err = request_irq(chan->irq, modified_rx_irq_handler, IRQF_SHARED, "xilinx-dma-controller", chan);
  if (err) {
    dev_err(xdev->dev, "unable to request IRQ %d\n", chan->irq);
    return err;
  }

  chan->start_transfer = xilinx_dma_start_transfer;
  chan->stop_transfer = xilinx_dma_stop_transfer;

  /* Initialize the tasklet */
  tasklet_init(&chan->tasklet, xilinx_dma_do_tasklet, (unsigned long) chan);

  /*
   * Initialize the DMA channel and add it to the DMA engine channels
   * list.
   */
  chan->common.device = &xdev->common;

  list_add_tail(&chan->common.device_node, &xdev->common.channels);
  xdev->chan[chan->id] = chan;

  /* Reset the channel */
  err = xilinx_dma_chan_reset(chan);
  if (err < 0) {
    dev_err(xdev->dev, "Reset channel failed\n");
    return err;
  }

  return 0;
}

/**
 * xilinx_dma_child_probe - Per child node probe
 * It get number of dma-channels per child node from
 * device-tree and initializes all the channels.
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 *
 * Return: 0 always.
 */
static int xilinx_dma_child_probe(struct xilinx_dma_device *xdev, struct device_node *node) {
  int ret, i, nr_channels = 1;

  ret = of_property_read_u32(node, "dma-channels", &nr_channels);
  if ((ret < 0) && xdev->mcdma)
    dev_warn(xdev->dev, "missing dma-channels property\n");

  for (i = 0; i < nr_channels; i++)
    xilinx_dma_chan_probe(xdev, node, xdev->chan_id++);

  xdev->nr_channels += nr_channels;

  return 0;
}

/**
 * of_dma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec, struct of_dma *ofdma) {
  struct xilinx_dma_device *xdev = ofdma->of_dma_data;
  int chan_id = dma_spec->args[0];

  if (chan_id >= xdev->nr_channels || !xdev->chan[chan_id])
    return NULL;

  return dma_get_slave_channel(&xdev->chan[chan_id]->common);
}

static const struct xilinx_dma_config axidma_config = {
  .dmatype = XDMA_TYPE_AXIDMA,
  .clk_init = axidma_clk_init,
};

static const struct of_device_id xilinx_dma_of_ids[] = {
  { .compatible = "xlnx,axi-dma-1.00.a", .data = &axidma_config },
  { }
};

MODULE_DEVICE_TABLE(of, xilinx_dma_of_ids);

/**
 * allocate_buffers - allocate buffers from cma pool.
 * It is needed to increase the dma pool size to something bigger
 * either in the bootargs of petalinux, or directly during the boot
 * via parameter cma=256M
 */
static int allocate_buffers(struct device *dev) {
  int i;
  /* RX BUFFERS*/
  dev_info(dev, "debug: allocating RX buffer pointer array. Size=%llu\n", (unsigned long long) bufcount_rd * sizeof(struct dma_rx_buf_t));
  //dma_rx_bufs = kmalloc(bufcount * sizeof(struct dma_rx_buf_t), GFP_KERNEL);
  dma_rx_bufs = kmalloc(bufcount_rd * sizeof(*dma_rx_bufs), GFP_KERNEL);
  if (!dma_rx_bufs) {
    dev_err(dev, "allocating RX buffer descriptor array failed\n");
    return -ENOMEM;
  }
  dev_info(dev, "Allocating %d RX buffers of %d bytes each\n", bufcount_rd, bufsize_rd);
  for (i = 0; i < bufcount_rd; i++) {
    dma_rx_bufs[i].bs = BS_UNALLOCATED;
    dma_rx_bufs[i].bytes_processed = 0;
    dma_rx_bufs[i].bytes_received = 0;
    dma_rx_bufs[i].hwaddr = 0;
    dma_rx_bufs[i].buf = dma_alloc_coherent(dev, bufsize_rd, &dma_rx_bufs[i].hwaddr, GFP_DMA | GFP_KERNEL);

    if (!dma_rx_bufs[i].buf) {
      dev_err(dev, "allocating rx buffer %02d failed\n", i);
      return -ENOMEM;
    }
    dma_rx_bufs[i].bs = BS_ALLOCATED;
  }

  for (i = 0; i < bufcount_rd; i++) {
    dma_rx_bufs[i].bs = BS_READY_FOR_DMA;
  }
  dev_info(dev, "successfully allocated %d RX buffers of %d bytes each\n", bufcount_rd, bufsize_rd);

  /* TX BUFFERS*/
  /*
  dev_info(dev, "debug: allocating TX buffer pointer array. Size=%llu\n", (unsigned long long) bufcount_wr * sizeof(struct dma_tx_buf_t));
  dma_tx_bufs = kmalloc(bufcount_wr * sizeof(*dma_tx_bufs), GFP_KERNEL);
  if (!dma_tx_bufs) {
    dev_err(dev, "allocating TX buffer descriptor array failed\n");
    return -ENOMEM;
  }
  for (i = 0; i < bufcount_wr; i++) {
    dma_tx_bufs[i].bytes_processed = 0;
    dma_tx_bufs[i].bytes_stored = 0;
    dma_tx_bufs[i].buf = dma_alloc_coherent(dev, bufsize_wr, &dma_tx_bufs[i].hwaddr, GFP_DMA | GFP_KERNEL);
    if (!dma_tx_bufs[i].buf) {
      dev_err(dev, "allocating tx buffer %02d failed\n", i);
      return -ENOMEM;
    }
  }
  dev_info(dev, "successfully allocated %d TX buffers of %d bytes\n", bufcount_wr, bufsize_wr);
  */
  return 0;
}

static int free_buffers(struct device *dev) {
  /* RX BUFFERS*/
  if(dma_rx_bufs){
    dev_info(dev, "Free %d RX buffers of %d bytes each\n", bufcount_rd, bufsize_rd);
    for (int i = 0; i < bufcount_rd; i++) {
      if(dma_rx_bufs[i].buf){
        dma_free_coherent(dev, bufsize_rd,  dma_rx_bufs[i].buf,  dma_rx_bufs[i].hwaddr);
      }
    }
    dev_info(dev, "debug: free RX buffer pointer array. Size=%llu\n", (unsigned long long) bufcount_rd * sizeof(struct dma_rx_buf_t));
    kfree(dma_rx_bufs);
    dma_rx_bufs = 0;
  }

  /* TX BUFFERS*/
  /*
  dev_info(dev, "Free %d TX buffers of %d bytes each\n", bufcount_wr, bufsize_wr);
  if(dma_tx_bufs){
    for (int i = 0; i < bufcount_wr; i++) {
      if(dma_tx_bufs[i].buf){
        dma_free_coherent(dev, bufsize_wr, dma_tx_bufs[i].buf, dma_tx_bufs[i].hwaddr);
      }
    }
    dev_info(dev, "debug: free TX buffer pointer array. Size=%llu\n", (unsigned long long) bufcount_wr * sizeof(struct dma_tx_buf_t));
    kfree(dma_tx_bufs);
    dma_tx_bufs = 0;
  }
  */
  return 0;
}

static int rx_task(void *data) {
  unsigned long flags;
  int ret = 0;
  dev_info(dev, "starting RX thread\n");
  //dev_info(dev, "resetting tx_chan\n");
  //xilinx_dma_reset(tx_chan);
  //dma_ctrl_clr(tx_chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMAXR_ALL_IRQ_MASK);
  /* start the first transaction*/
  dev_info(dev, "resuming rx_chan\n");
  ret = rx_resume(rx_chan, true);
  //dev_info(dev, "disabling interrupt of TX\n");
  msleep(10000); //sleep 10 s
  return ret;
  while (!kthread_should_stop()) {
    msleep(60000);
    spin_lock_irqsave(&dma_register_rd_lock, flags);
    u32 length = dma_ctrl_read(rx_chan, XILINX_DMA_REG_LENGTH);
    u32 status = dma_ctrl_read(rx_chan, XILINX_DMA_REG_DMASR);
    spin_unlock_irqrestore(&dma_register_rd_lock, flags);
    dev_info(dev, "DBG:[r%d.%d-w%d.%d] T%d(%d)I%d CR=0x%08x SR=0x%08x\n DA=0x%08x,LEN=0x%08x\n",
             rx_buf_process_index, dma_rx_bufs[rx_buf_process_index].bs,
             rx_buf_store_index, dma_rx_bufs[rx_buf_store_index].bs,
             cnt_dma_rd, cnt_dma_rd - cnt_trans_rd, rx_chan->idle ? 1 : 0,
             dma_ctrl_read(rx_chan, XILINX_DMA_REG_DMACR), status,
             dma_ctrl_read(rx_chan, XILINX_DMA_REG_SRCDSTADDR), length
      );
  }
  return ret;
}

/* character device functions */
static int charrd_open(struct inode * inode, struct file *file) {
  if (down_interruptible(&sem_rd) != 0) {
    dev_err(dev, "device %s is already opened by another process\n", file->f_path.dentry->d_iname);
    return -EBUSY;
  }
  return 0;
}
static int charrd_close(struct inode *inode, struct file *file) {
  up(&sem_rd);
  return 0;
}

static void buffer_rx_cleanup_increment_index(void) {
  dma_rx_bufs[rx_buf_process_index].bytes_processed = 0;
  dma_rx_bufs[rx_buf_process_index].bytes_received = 0;
  dma_rx_bufs[rx_buf_process_index].bs = BS_READY_FOR_DMA;
  smp_wmb();
  int rx_buf_next_index = rx_buf_process_index + 1;
  if (rx_buf_next_index >= bufcount_rd)
    rx_buf_next_index = 0;
  rx_buf_process_index = rx_buf_next_index;
  cnt_trans_rd++;
  if (rx_chan->idle) {
    rx_resume(rx_chan, false);
  }
}

static ssize_t charrd_read(struct file *file, char __user *buf, size_t buf_size, loff_t *off) {
//dev_info(dev, "charrd_read reading size %d\n", buf_size);
  u32 copied = 0; //how many bytes have been already copied to the buf
  if (!buf_size) {
    dev_err(dev, "<charrd_read> buf_size is ZERO!!\n");
    return 0;
  }
  u32 chunk;/*how much data will be copied*/
  size_t dma_buf_remain_bytes; /* how many bytes can be copied from the single DMA buffer */
  unsigned long timeout = usecs_to_jiffies(100);
  unsigned long remaining;/* time remaining to the timeout*/
  while (buf_size) {/*while there it is requested to copy more data*/
    //dev_info(dev, "[%d].%d bsize=%d, \n", rx_buf_process_index, dma_rx_bufs[rx_buf_process_index].bs, buf_size);
    if ((dma_rx_bufs[rx_buf_process_index].bs == BS_DMA_FINISHED_OK) || (dma_rx_bufs[rx_buf_process_index].bs == BS_DMA_FINISHED_FAIL)) {
      dma_buf_remain_bytes = dma_rx_bufs[rx_buf_process_index].bytes_received - dma_rx_bufs[rx_buf_process_index].bytes_processed;
      if (dma_buf_remain_bytes <= 0) {/* the buffer was fully read*/
        buffer_rx_cleanup_increment_index();
        continue;/*move on to the next buffer*/
      }
      chunk = min(buf_size, dma_buf_remain_bytes);
      if (chunk > 0) {
        int copy_ret = copy_to_user(buf + copied, dma_rx_bufs[rx_buf_process_index].buf + dma_rx_bufs[rx_buf_process_index].bytes_processed, chunk);
        dma_rx_bufs[rx_buf_process_index].bytes_processed = dma_rx_bufs[rx_buf_process_index].bytes_processed + chunk;
        if (copy_ret) /* really bad error*/
          return -EFAULT;
        *off += chunk;
        copied += chunk;
        buf_size -= chunk;
        if (dma_rx_bufs[rx_buf_process_index].bytes_processed == dma_rx_bufs[rx_buf_process_index].bytes_received) {
          buffer_rx_cleanup_increment_index();
        }
      } else {
        /* should never happen*/
      }
    } else if (dma_rx_bufs[rx_buf_process_index].bs == BS_READY_FOR_DMA) {
      /* reached the dma buffer writer pointer*/
      if (rx_chan->idle) {
        /*for some reasons the dma is not started, although it could. Let's restart*/
        rx_resume(rx_chan, false); /*restart dma*/
        continue; /*check if new data is already there */
      }
      if (copied) /* some data is already copied*/
        break; /* we return what we have*/
      if (file->f_flags & O_NONBLOCK)
        return -EAGAIN; /* quit if mode is set to non-blocking */
			//remaining = wait_event_interruptible_timeout(rx_thread_wq, dma_rx_bufs[rx_buf_process_index].bs == BS_DMA_FINISHED_OK, timeout);
      timeout = 10000000;
      while (timeout--) {
        if (dma_rx_bufs[rx_buf_process_index].bs == BS_DMA_FINISHED_OK)
          break;
        if (dma_rx_bufs[rx_buf_process_index].bs == BS_DMA_FINISHED_FAIL)
          break;
      }
      /* Returns:
       * 0 if the @condition evaluated to %false after the @timeout elapsed,
       * 1 if the @condition evaluated to %true after the @timeout elapsed,
       * the remaining jiffies (at least 1) if the @condition evaluated
       * to %true before the @timeout elapsed, or -%ERESTARTSYS if it was
       * interrupted by a signal.
       */
      rx_thread_wq_condition = 0;
      //dev_info(dev, "charrd_read slept remaining %d\n", remaining);
      if (remaining > 1) {
        //dev_info(dev,"woken up success: %lu\n",remaining);
        continue; /* new data should be there*/
      }
//			dev_info(dev, "no data from dma %d. blocking.", remaining); /*and continue*/
    }
  }
//dev_info(dev, "charrd_read copied %d\n", copied);
  return copied;
}

static int charwr_open(struct inode *inode, struct file *file) {
  if (down_interruptible(&sem_wr) != 0) {
    dev_err(dev, "device %s is already opened by another process\n", file->f_path.dentry->d_iname);
    return -EBUSY;
  }
  return 0;
}
static int charwr_close(struct inode *inode, struct file *file) {
  up(&sem_wr);
  return 0;
}
static ssize_t charwr_write(struct file *fp, const char __user *buf, size_t buf_size, loff_t *off) {
//	dev_info(dev, "writing %d bytes\n", buf_size);
  if (buf_size > bufsize_wr) {
    /*trying to write more then the buffer size*/
    dev_err(dev, "trying to write %llu, but max=%d. Clipped to %d.\n", (unsigned long long) buf_size, bufsize_wr, bufsize_wr);
    buf_size = bufsize_wr;
  }
  int notwritten = copy_from_user(dma_tx_bufs[0].buf, buf, buf_size);
  if (notwritten) {
    dev_err(dev, "copy_from_user failed. Ret=%d\n", notwritten);
  }
  unsigned long flags;
  spin_lock_irqsave(&dma_register_wr_lock, flags);
  /* straightforward drive of the registers */
  /* step 1: run */
  dma_ctrl_set(tx_chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RUNSTOP);

  /* step2: disable interrupts (already disabled for TX */
  //dma_ctrl_clr(tx_chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMAXR_ALL_IRQ_MASK);
  /* step3: write source address */
  dma_ctrl_write(tx_chan, XILINX_DMA_REG_SRCDSTADDR, dma_tx_bufs[0].hwaddr);/*write destination address*/

  /* step4: set the number of bytes to transfer*/
  dma_ctrl_write(tx_chan, XILINX_DMA_REG_LENGTH, buf_size); /*write number of bytes to transfer to the LENGTH register*/

  /*wait_for the successful sent*/

  unsigned int loopcnt = 0;
  u32 dma_sr;
  while (1) {
    dma_sr = dma_ctrl_read(tx_chan, XILINX_DMA_REG_DMASR);
    loopcnt ++;
    if (dma_sr & XILINX_DMA_DMASR_IDLE){
      break;/*intensive waiting*/
    }
    if(loopcnt == U16_MAX *100){
      dev_err(dev, "ERROR timeout during write: sr=0x%08x\n", dma_sr);
      spin_unlock_irqrestore(&dma_register_wr_lock, flags);
      return -EFAULT;
    }
  }
  if (dma_sr & XILINX_DMA_DMASR_ALL_ERR_MASK) {
    dev_err(dev, "ERROR during write: sr=0x%08x\n", dma_sr);
    spin_unlock_irqrestore(&dma_register_wr_lock, flags);
    return -EFAULT;
  }
  spin_unlock_irqrestore(&dma_register_wr_lock, flags);
  unsigned int dma_actually_written = dma_ctrl_read(tx_chan, XILINX_DMA_REG_LENGTH);
  if (dma_actually_written != buf_size)
    dev_err(dev, "ERROR wrong number of bytes written: len=0x%08x\n", dma_actually_written);
  return dma_actually_written;
}

static int create_devices(struct xilinx_dma_device *xdev) {
  int ret = 0;

  sema_init(&sem_rd, 1); //initial value of one - only 1 process can read from
  /* sema_init(&sem_wr, 1); //initial value of one */

  xdev->mdevrd.minor = MISC_DYNAMIC_MINOR;
  xdev->mdevrd.name = "axidmard";
  xdev->mdevrd.fops = &fops_rd;
  xdev->mdevrd.parent = NULL;
  xdev->mdevrd.nodename = "axidmard";

  ret = misc_register(&xdev->mdevrd);
  if (ret) {
    dev_err(xdev->dev, "Failed to register miscdev axidmard\n");
    return ret;
  }
  dev_info(xdev->dev, "/dev/axidmard Registered\n");

  return ret;
}

/**
 * xilinx_dma_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_probe(struct platform_device *pdev) {
  int (*clk_init)(struct platform_device *, struct clk **, struct clk **, struct clk **, struct clk **, struct clk **) = axidma_clk_init;
  struct device_node *node = pdev->dev.of_node;
  struct xilinx_dma_device *xdev;
  struct device_node *child, *np = pdev->dev.of_node;
  struct resource *io;
  u32 addr_width = 0;
  u32 len_width = 0;
  int i, err;

  /* Allocate and initialize the DMA engine structure */
  xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
  if (!xdev)
    return -ENOMEM;

  xdev->dev = &pdev->dev;
  dev = &pdev->dev;
  dev_info(&pdev->dev, "loading modified DMA driver instead of xilinx AXI DMA");
  if (np) {
    const struct of_device_id *match;

    match = of_match_node(xilinx_dma_of_ids, np);
    if (match && match->data) {
      xdev->dma_config = match->data;
      clk_init = xdev->dma_config->clk_init;
    }
  }

  err = clk_init(pdev, &xdev->axi_clk, &xdev->tx_clk, &xdev->txs_clk, &xdev->rx_clk, &xdev->rxs_clk);
  if (err)
    return err;

  /* Request and map I/O memory */
  io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  xdev->regs = devm_ioremap_resource(&pdev->dev, io);
  if (IS_ERR(xdev->regs))
    return PTR_ERR(xdev->regs);

  /* Retrieve the DMA engine properties from the device tree */
  xdev->has_sg = of_property_read_bool(node, "xlnx,include-sg");
  xdev->max_buffer_len = GENMASK(XILINX_DMA_MAX_TRANS_LEN_MAX - 1, 0);

  xdev->mcdma = of_property_read_bool(node, "xlnx,mcdma");
  if (!of_property_read_u32(node, "xlnx,sg-length-width", &len_width)) {
    if (len_width < XILINX_DMA_MAX_TRANS_LEN_MIN || len_width > XILINX_DMA_V2_MAX_TRANS_LEN_MAX) {
      dev_warn(xdev->dev, "invalid xlnx,sg-length-width property value using default width\n");
    } else {
      if (len_width > XILINX_DMA_MAX_TRANS_LEN_MAX)
        dev_warn(xdev->dev, "Please ensure that IP supports buffer length > 23 bits\n");

      xdev->max_buffer_len = GENMASK(len_width - 1, 0);
    }
  }

  err = of_property_read_u32(node, "xlnx,addrwidth", &addr_width);
  if (err < 0)
    dev_warn(xdev->dev, "missing xlnx,addrwidth property\n");

  if (addr_width > 32)
    xdev->ext_addr = true;
  else
    xdev->ext_addr = false;

  /* Set the dma mask bits */
  dma_set_mask(xdev->dev, DMA_BIT_MASK(addr_width));

  /* Initialize the DMA engine */
  xdev->common.dev = &pdev->dev;

  INIT_LIST_HEAD(&xdev->common.channels);
  dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
  dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);

  xdev->common.dst_addr_widths = BIT(addr_width / 8);
  xdev->common.src_addr_widths = BIT(addr_width / 8);
  xdev->common.device_alloc_chan_resources = xilinx_dma_alloc_chan_resources;
  xdev->common.device_free_chan_resources = xilinx_dma_free_chan_resources;
  xdev->common.device_terminate_all = xilinx_dma_terminate_all;
  xdev->common.device_tx_status = xilinx_dma_tx_status;
  xdev->common.device_issue_pending = xilinx_dma_issue_pending;

  dma_cap_set(DMA_CYCLIC, xdev->common.cap_mask);
  xdev->common.device_prep_slave_sg = xilinx_dma_prep_slave_sg;
  xdev->common.device_prep_dma_cyclic = xilinx_dma_prep_dma_cyclic;
  xdev->common.device_prep_interleaved_dma = xilinx_dma_prep_interleaved;
  /* Residue calculation is supported by only AXI DMA */
  xdev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;

  platform_set_drvdata(pdev, xdev);

  /* Initialize the channels */
  for_each_child_of_node(node, child)
  {
    err = xilinx_dma_child_probe(xdev, child);
    if (err < 0)
      goto disable_clks;
  }


  /* Register the DMA engine with the core */
  dma_async_device_register(&xdev->common);

  err = of_dma_controller_register(node, of_dma_xilinx_xlate, xdev);
  if (err < 0) {
    dev_err(&pdev->dev, "Unable to register DMA to DT\n");
    dma_async_device_unregister(&xdev->common);
    goto error;
  }

  if (xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA)
    dev_info(&pdev->dev, "Xilinx AXI DMA Engine Driver Probed!!\n");

  /* allocate buffers*/
  err = allocate_buffers(xdev->dev);
  if (err)
    goto error;

  /* register character devices */
  create_devices(xdev);

  /* start threads*/
  rx_thread = kthread_run(rx_task, NULL, "dma rx thread");
  if (IS_ERR(rx_thread)) {
    dev_err(xdev->dev, "failed to start the kernel thread");
    kfree(rx_thread);
  }
  //TODO why can't be the reset called directly? it crashes the kernel...
  /* start the DMA (requires a reset)*/
  //rx_resume(rx_chan, true);
  return 0;

disable_clks: xdma_disable_allclks(xdev);
error: for (i = 0; i < xdev->nr_channels; i++)
    if (xdev->chan[i])
      xilinx_dma_chan_remove(xdev->chan[i]);

  return err;
}

/**
 * xilinx_dma_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int xilinx_dma_remove(struct platform_device *pdev) {
  struct xilinx_dma_device *xdev = platform_get_drvdata(pdev);
  int i;

  of_dma_controller_free(pdev->dev.of_node);

  dma_async_device_unregister(&xdev->common);

  for (i = 0; i < xdev->nr_channels; i++)
    if (xdev->chan[i])
      xilinx_dma_chan_remove(xdev->chan[i]);

  xdma_disable_allclks(xdev);
  kfree(rx_thread);

  misc_deregister(&xdev->mdevrd);
  dev_info(xdev->dev, "/dev/axidmard Deregistered\n");

  /* misc_deregister(&xdev->mdevwr); */
  /* dev_info(xdev->dev, "/dev/axidmawr Deregistered\n"); */

  free_buffers(xdev->dev);

  return 0;
}

static struct platform_driver xilinx_vdma_driver = {
  .driver = {
    .name = "xilinx-dma_modified",
    .of_match_table = xilinx_dma_of_ids,
  },
  .probe = xilinx_dma_probe,
  .remove = xilinx_dma_remove,
};

module_platform_driver(xilinx_vdma_driver);

MODULE_AUTHOR("Kvasnicka, DESY/FZU; Yi Liu DESY");
MODULE_DESCRIPTION("Modified Xilinx VDMA driver");
MODULE_LICENSE("GPL v2");
