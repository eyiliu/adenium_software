#ifndef AXIDMACHAR_H
#define AXIDMACHAR_H

#include <asm-generic/int-ll64.h>
#include <linux/compiler.h>
// #include <linux/compiler-gcc.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

/* Register/Descriptor Offsets */
#define XILINX_DMA_MM2S_CTRL_OFFSET		0x0000
#define XILINX_DMA_S2MM_CTRL_OFFSET		0x0030
#define XILINX_VDMA_MM2S_DESC_OFFSET		0x0050
#define XILINX_VDMA_S2MM_DESC_OFFSET		0x00a0

/* Control Registers */
#define XILINX_DMA_REG_DMACR			0x0000
#define XILINX_DMA_DMACR_DELAY_MAX		0xff
#define XILINX_DMA_DMACR_DELAY_SHIFT		24
#define XILINX_DMA_DMACR_FRAME_COUNT_MAX	0xff
#define XILINX_DMA_DMACR_FRAME_COUNT_SHIFT	16
#define XILINX_DMA_DMACR_ERR_IRQ		BIT(14)
#define XILINX_DMA_DMACR_DLY_CNT_IRQ		BIT(13)
#define XILINX_DMA_DMACR_FRM_CNT_IRQ		BIT(12)
#define XILINX_DMA_DMACR_MASTER_SHIFT		8
#define XILINX_DMA_DMACR_FSYNCSRC_SHIFT	5
#define XILINX_DMA_DMACR_FRAMECNT_EN		BIT(4)
#define XILINX_DMA_DMACR_GENLOCK_EN		BIT(3)
#define XILINX_DMA_DMACR_RESET			BIT(2)
#define XILINX_DMA_DMACR_CIRC_EN		BIT(1)
#define XILINX_DMA_DMACR_RUNSTOP		BIT(0)
#define XILINX_DMA_DMACR_FSYNCSRC_MASK		GENMASK(6, 5)

#define XILINX_DMA_REG_DMASR			0x0004
#define XILINX_DMA_DMASR_EOL_LATE_ERR		BIT(15)
#define XILINX_DMA_DMASR_ERR_IRQ		BIT(14)
#define XILINX_DMA_DMASR_DLY_CNT_IRQ		BIT(13)
#define XILINX_DMA_DMASR_FRM_CNT_IRQ		BIT(12)
#define XILINX_DMA_DMASR_SOF_LATE_ERR		BIT(11)
#define XILINX_DMA_DMASR_SG_DEC_ERR		BIT(10)
#define XILINX_DMA_DMASR_SG_SLV_ERR		BIT(9)
#define XILINX_DMA_DMASR_EOF_EARLY_ERR		BIT(8)
#define XILINX_DMA_DMASR_SOF_EARLY_ERR		BIT(7)
#define XILINX_DMA_DMASR_DMA_DEC_ERR		BIT(6)
#define XILINX_DMA_DMASR_DMA_SLAVE_ERR		BIT(5)
#define XILINX_DMA_DMASR_DMA_INT_ERR		BIT(4)
#define XILINX_DMA_DMASR_IDLE			BIT(1)
#define XILINX_DMA_DMASR_HALTED		BIT(0)
#define XILINX_DMA_DMASR_DELAY_MASK		GENMASK(31, 24)
#define XILINX_DMA_DMASR_FRAME_COUNT_MASK	GENMASK(23, 16)

#define XILINX_DMA_REG_CURDESC			0x0008
#define XILINX_DMA_REG_TAILDESC		0x0010
#define XILINX_DMA_REG_REG_INDEX		0x0014
#define XILINX_DMA_REG_FRMSTORE		0x0018
#define XILINX_DMA_REG_THRESHOLD		0x001c
#define XILINX_DMA_REG_FRMPTR_STS		0x0024
#define XILINX_DMA_REG_PARK_PTR		0x0028
#define XILINX_DMA_PARK_PTR_WR_REF_SHIFT	8
#define XILINX_DMA_PARK_PTR_WR_REF_MASK		GENMASK(12, 8)
#define XILINX_DMA_PARK_PTR_RD_REF_SHIFT	0
#define XILINX_DMA_PARK_PTR_RD_REF_MASK		GENMASK(4, 0)
#define XILINX_DMA_REG_VDMA_VERSION		0x002c

/* Register Direct Mode Registers */
#define XILINX_DMA_REG_VSIZE			0x0000
#define XILINX_DMA_REG_HSIZE			0x0004

#define XILINX_DMA_REG_FRMDLY_STRIDE		0x0008
#define XILINX_DMA_FRMDLY_STRIDE_FRMDLY_SHIFT	24
#define XILINX_DMA_FRMDLY_STRIDE_STRIDE_SHIFT	0

#define XILINX_VDMA_REG_START_ADDRESS(n)	(0x000c + 4 * (n))
#define XILINX_VDMA_REG_START_ADDRESS_64(n)	(0x000c + 8 * (n))

#define XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP	0x00ec
#define XILINX_VDMA_ENABLE_VERTICAL_FLIP	BIT(0)

/* HW specific definitions */
#define XILINX_DMA_MAX_CHANS_PER_DEVICE	0x20

#define XILINX_DMA_DMAXR_ALL_IRQ_MASK	\
		(XILINX_DMA_DMASR_FRM_CNT_IRQ | \
		 XILINX_DMA_DMASR_DLY_CNT_IRQ | \
		 XILINX_DMA_DMASR_ERR_IRQ)

#define XILINX_DMA_DMASR_ALL_ERR_MASK	\
		(XILINX_DMA_DMASR_EOL_LATE_ERR | \
		 XILINX_DMA_DMASR_SOF_LATE_ERR | \
		 XILINX_DMA_DMASR_SG_DEC_ERR | \
		 XILINX_DMA_DMASR_SG_SLV_ERR | \
		 XILINX_DMA_DMASR_EOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_SOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_DMA_DEC_ERR | \
		 XILINX_DMA_DMASR_DMA_SLAVE_ERR | \
		 XILINX_DMA_DMASR_DMA_INT_ERR)

/*
 * Recoverable errors are DMA Internal error, SOF Early, EOF Early
 * and SOF Late. They are only recoverable when C_FLUSH_ON_FSYNC
 * is enabled in the h/w system.
 */
#define XILINX_DMA_DMASR_ERR_RECOVER_MASK	\
		(XILINX_DMA_DMASR_SOF_LATE_ERR | \
		 XILINX_DMA_DMASR_EOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_SOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_DMA_INT_ERR)

/* Axi VDMA Flush on Fsync bits */
#define XILINX_DMA_FLUSH_S2MM		3
#define XILINX_DMA_FLUSH_MM2S		2
#define XILINX_DMA_FLUSH_BOTH		1

/* Delay loop counter to prevent hardware failure */
#define XILINX_DMA_LOOP_COUNT		1000000

/* AXI DMA Specific Registers/Offsets */
#define XILINX_DMA_REG_SRCDSTADDR	0x18
#define XILINX_DMA_REG_BTT			0x28
#define XILINX_DMA_REG_LENGTH		0x28

/* AXI DMA Specific Masks/Bit fields */
#define XILINX_DMA_MAX_TRANS_LEN_MIN	8
#define XILINX_DMA_MAX_TRANS_LEN_MAX	23
#define XILINX_DMA_V2_MAX_TRANS_LEN_MAX	26
#define XILINX_DMA_CR_COALESCE_MAX	GENMASK(23, 16)
#define XILINX_DMA_CR_CYCLIC_BD_EN_MASK	BIT(4)
#define XILINX_DMA_CR_COALESCE_SHIFT	16
#define XILINX_DMA_BD_SOP		BIT(27)
#define XILINX_DMA_BD_EOP		BIT(26)
#define XILINX_DMA_COALESCE_MAX		255
#define XILINX_DMA_NUM_DESCS		255
#define XILINX_DMA_NUM_APP_WORDS	5

/* Multi-Channel DMA Descriptor offsets*/
#define XILINX_DMA_MCRX_CDESC(x)	(0x40 + (x-1) * 0x20)
#define XILINX_DMA_MCRX_TDESC(x)	(0x48 + (x-1) * 0x20)

/* Multi-Channel DMA Masks/Shifts */
#define XILINX_DMA_BD_HSIZE_MASK	GENMASK(15, 0)
#define XILINX_DMA_BD_STRIDE_MASK	GENMASK(15, 0)
#define XILINX_DMA_BD_VSIZE_MASK	GENMASK(31, 19)
#define XILINX_DMA_BD_TDEST_MASK	GENMASK(4, 0)
#define XILINX_DMA_BD_STRIDE_SHIFT	0
#define XILINX_DMA_BD_VSIZE_SHIFT	19



enum xdma_ip_type {
		   XDMA_TYPE_AXIDMA = 0,
		   XDMA_TYPE_CDMA,
		   XDMA_TYPE_VDMA,
};



/**
 * struct xilinx_vdma_desc_hw - Hardware Descriptor
 * @next_desc: Next Descriptor Pointer @0x00
 * @pad1: Reserved @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @vsize: Vertical Size @0x10
 * @hsize: Horizontal Size @0x14
 * @stride: Number of bytes between the first
 *	    pixels of each horizontal line @0x18
 */
struct xilinx_vdma_desc_hw {
	u32 next_desc;
	u32 pad1;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 vsize;
	u32 hsize;
	u32 stride;
}__aligned(64);

/**
 * struct xilinx_axidma_desc_hw - Hardware Descriptor for AXI DMA
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: MSB of Next Descriptor Pointer @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @mcdma_control: Control field for mcdma @0x10
 * @vsize_stride: Vsize and Stride field for mcdma @0x14
 * @control: Control field @0x18
 * @status: Status field @0x1C
 * @app: APP Fields @0x20 - 0x30
 */
struct xilinx_axidma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 mcdma_control;
	u32 vsize_stride;
	u32 control;
	u32 status;
	u32 app[XILINX_DMA_NUM_APP_WORDS];
}__aligned(64);

/**
 * struct xilinx_cdma_desc_hw - Hardware Descriptor
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: Next Descriptor Pointer MSB @0x04
 * @src_addr: Source address @0x08
 * @src_addr_msb: Source address MSB @0x0C
 * @dest_addr: Destination address @0x10
 * @dest_addr_msb: Destination address MSB @0x14
 * @control: Control field @0x18
 * @status: Status field @0x1C
 */
struct xilinx_cdma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 src_addr;
	u32 src_addr_msb;
	u32 dest_addr;
	u32 dest_addr_msb;
	u32 control;
	u32 status;
}__aligned(64);

/**
 * struct xilinx_vdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_vdma_tx_segment {
	struct xilinx_vdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
}__aligned(64);

/**
 * struct xilinx_axidma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_axidma_tx_segment {
	struct xilinx_axidma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
}__aligned(64);

/**
 * struct xilinx_cdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_cdma_tx_segment {
	struct xilinx_cdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
}__aligned(64);

/**
 * struct xilinx_dma_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @segments: TX segments list
 * @node: Node in the channel descriptors list
 * @cyclic: Check for cyclic transfers.
 */
struct xilinx_dma_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct list_head segments;
	struct list_head node;
	bool cyclic;
};

/**
 * struct xilinx_dma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @ctrl_offset: Control registers offset
 * @desc_offset: TX descriptor registers offset
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @active_list: Descriptors ready to submit
 * @done_list: Complete descriptors
 * @free_seg_list: Free descriptors
 * @common: DMA common channel
 * @desc_pool: Descriptors pool
 * @dev: The dma device
 * @irq: Channel IRQ
 * @id: Channel ID
 * @direction: Transfer direction
 * @num_frms: Number of frames
 * @has_sg: Support scatter transfers
 * @cyclic: Check for cyclic transfers.
 * @genlock: Support genlock mode
 * @err: Channel has errors
 * @idle: Check for channel idle
 * @tasklet: Cleanup work after irq
 * @config: Device configuration info
 * @flush_on_fsync: Flush on Frame sync
 * @desc_pendingcount: Descriptor pending count
 * @ext_addr: Indicates 64 bit addressing is supported by dma channel
 * @desc_submitcount: Descriptor h/w submitted count
 * @residue: Residue for AXI DMA
 * @seg_v: Statically allocated segments base
 * @seg_p: Physical allocated segments base
 * @cyclic_seg_v: Statically allocated segment base for cyclic transfers
 * @cyclic_seg_p: Physical allocated segments base for cyclic dma
 * @start_transfer: Differentiate b/w DMA IP's transfer
 * @stop_transfer: Differentiate b/w DMA IP's quiesce
 * @tdest: TDEST value for mcdma
 * @has_vflip: S2MM vertical flip
 */
struct xilinx_dma_chan {
	struct xilinx_dma_device *xdev;
	u32 ctrl_offset;
	u32 desc_offset;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head active_list;
	struct list_head done_list;
	struct list_head free_seg_list;
	struct dma_chan common;
	struct dma_pool *desc_pool;
	struct device *dev;
	int irq;
	int id;
	enum dma_transfer_direction direction;
	int num_frms;
	bool has_sg;
	bool cyclic;
	bool genlock;
	bool err;
	bool idle;
	struct tasklet_struct tasklet;
	struct xilinx_vdma_config config;
	bool flush_on_fsync;
	u32 desc_pendingcount;
	bool ext_addr;
	u32 desc_submitcount;
	u32 residue;
	struct xilinx_axidma_tx_segment *seg_v;
	dma_addr_t seg_p;
	struct xilinx_axidma_tx_segment *cyclic_seg_v;
	dma_addr_t cyclic_seg_p;
	void (*start_transfer)(struct xilinx_dma_chan *chan);
	int (*stop_transfer)(struct xilinx_dma_chan *chan);
	u16 tdest;
	bool has_vflip;
};

struct xilinx_dma_config {
	enum xdma_ip_type dmatype;
	int (*clk_init)(struct platform_device *pdev, struct clk **axi_clk, struct clk **tx_clk, struct clk **txs_clk, struct clk **rx_clk, struct clk **rxs_clk);
};

/**
 * struct xilinx_dma_device - DMA device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 * @has_sg: Specifies whether Scatter-Gather is present or not
 * @mcdma: Specifies whether Multi-Channel is present or not
 * @flush_on_fsync: Flush on frame sync
 * @ext_addr: Indicates 64 bit addressing is supported by dma device
 * @pdev: Platform device structure pointer
 * @dma_config: DMA config structure
 * @axi_clk: DMA Axi4-lite interace clock
 * @tx_clk: DMA mm2s clock
 * @txs_clk: DMA mm2s stream clock
 * @rx_clk: DMA s2mm clock
 * @rxs_clk: DMA s2mm stream clock
 * @nr_channels: Number of channels DMA device supports
 * @chan_id: DMA channel identifier
 * @max_buffer_len: Max buffer length
 */
struct xilinx_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_dma_chan *chan[XILINX_DMA_MAX_CHANS_PER_DEVICE];
	bool has_sg;
	bool mcdma;
	u32 flush_on_fsync;
	bool ext_addr;
	struct platform_device *pdev;
	const struct xilinx_dma_config *dma_config;
	struct clk *axi_clk;
	struct clk *tx_clk;
	struct clk *txs_clk;
	struct clk *rx_clk;
	struct clk *rxs_clk;
	u32 nr_channels;
	u32 chan_id;
	u32 max_buffer_len;
	struct miscdevice mdevrd; /* misc device for file operations*/
	struct miscdevice mdevwr; /* misc device for file operations*/
};

/* Macros */
#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_dma_chan, common)
#define to_dma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_dma_tx_descriptor, async_tx)
#define xilinx_dma_poll_timeout(chan, reg, val, cond, delay_us, timeout_us) \
	readl_poll_timeout(chan->xdev->regs + chan->ctrl_offset + reg, val, cond, delay_us, timeout_us)

/* IO accessors */
static inline u32 dma_read(struct xilinx_dma_chan *chan, u32 reg) {
	return ioread32(chan->xdev->regs + reg);
}

static inline void dma_write(struct xilinx_dma_chan *chan, u32 reg, u32 value) {
	iowrite32(value, chan->xdev->regs + reg);
}

static inline void vdma_desc_write(struct xilinx_dma_chan *chan, u32 reg, u32 value) {
	dma_write(chan, chan->desc_offset + reg, value);
}

static inline u32 dma_ctrl_read(struct xilinx_dma_chan *chan, u32 reg) {
	return dma_read(chan, chan->ctrl_offset + reg);
}

static inline void dma_ctrl_write(struct xilinx_dma_chan *chan, u32 reg, u32 value) {
	dma_write(chan, chan->ctrl_offset + reg, value);
}

static inline void dma_ctrl_clr(struct xilinx_dma_chan *chan, u32 reg, u32 clr) {
	dma_ctrl_write(chan, reg, dma_ctrl_read(chan, reg) & ~clr);
}

static inline void dma_ctrl_set(struct xilinx_dma_chan *chan, u32 reg, u32 set) {
	dma_ctrl_write(chan, reg, dma_ctrl_read(chan, reg) | set);
}

/**
 * vdma_desc_write_64 - 64-bit descriptor write
 * @chan: Driver specific VDMA channel
 * @reg: Register to write
 * @value_lsb: lower address of the descriptor.
 * @value_msb: upper address of the descriptor.
 *
 * Since vdma driver is trying to write to a register offset which is not a
 * multiple of 64 bits(ex : 0x5c), we are writing as two separate 32 bits
 * instead of a single 64 bit register write.
 */
static inline void vdma_desc_write_64(struct xilinx_dma_chan *chan, u32 reg, u32 value_lsb, u32 value_msb) {
	/* Write the lsb 32 bits*/
	writel(value_lsb, chan->xdev->regs + chan->desc_offset + reg);

	/* Write the msb 32 bits */
	writel(value_msb, chan->xdev->regs + chan->desc_offset + reg + 4);
}

static inline void dma_writeq(struct xilinx_dma_chan *chan, u32 reg, u64 value) {
	lo_hi_writeq(value, chan->xdev->regs + chan->ctrl_offset + reg);
}

static inline void xilinx_write(struct xilinx_dma_chan *chan, u32 reg, dma_addr_t addr) {
	if (chan->ext_addr)
		dma_writeq(chan, reg, addr);
	else
		dma_ctrl_write(chan, reg, addr);
}
//
static inline void xilinx_axidma_buf(struct xilinx_dma_chan *chan, struct xilinx_axidma_desc_hw *hw, dma_addr_t buf_addr, size_t sg_used, size_t period_len)
{
	if (chan->ext_addr) {
		hw->buf_addr = lower_32_bits(buf_addr + sg_used + period_len);
		hw->buf_addr_msb = upper_32_bits(buf_addr + sg_used +
		        period_len);
	} else {
		hw->buf_addr = buf_addr + sg_used + period_len;
	}
}

#endif
