/*
 * Simple Driver for Management PF
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * Code borrowed from Xilinx SDAccel XDMA driver
 *
 * Author(s):
 * Sonal Santan <sonal.santan@xilinx.com>
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "mgmt-core.h"
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include "../xocl_drv.h"
#include "version.h"

//#define USE_FEATURE_ROM

static const struct pci_device_id pci_ids[] = {
	XOCL_MGMT_PCI_IDS,
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, pci_ids);

int health_interval = 5;
module_param(health_interval, int, (S_IRUGO|S_IWUSR));
MODULE_PARM_DESC(health_interval,
	"Interval (in sec) after which the health thread is run. (1 = Minimum, 5 = default)");

int health_check = 1;
module_param(health_check, int, (S_IRUGO|S_IWUSR));
MODULE_PARM_DESC(health_check,
	"Enable health thread that checks the status of AXI Firewall and SYSMON. (0 = disable, 1 = enable)");

int minimum_initialization = 0;
module_param(minimum_initialization, int, (S_IRUGO|S_IWUSR));
MODULE_PARM_DESC(minimum_initialization,
	"Enable minimum_initialization to force driver to load without vailid firmware or DSA. Thus xbsak flash is able to upgrade firmware. (0 = normal initialization, 1 = minimum initialization)");

#define	LOW_TEMP		0
#define	HI_TEMP			85000
#define	LOW_MILLVOLT		500
#define	HI_MILLVOLT		2500


static dev_t xclmgmt_devnode;
struct class *xrt_class = NULL;

/*
 * Called when the device goes from unused to used.
 */
static int char_open(struct inode *inode, struct file *file)
{
	struct xclmgmt_dev *lro;

	/* pointer to containing data structure of the character device inode */
	lro = container_of(inode->i_cdev, struct xclmgmt_dev,
			user_char_dev.cdev);

	/* create a reference to our char device in the opened file */
	file->private_data = lro;
	BUG_ON(!lro);

	mgmt_info(lro, "opened file %p by pid: %d\n",
		file, pid_nr(task_tgid(current)));

	xocl_drv_get(lro);

	return 0;
}

/*
 * Called when the device goes from used to unused.
 */
static int char_close(struct inode *inode, struct file *file)
{
	struct xclmgmt_dev *lro;

	lro = (struct xclmgmt_dev *)file->private_data;
	BUG_ON(!lro);

	if (xocl_drv_released(lro))
		goto end;

	mgmt_info(lro, "Closing file %p by pid: %d\n",
		file, pid_nr(task_tgid(current)));

end:
	xocl_drv_put(lro);
	return 0;
}

/*
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct xclmgmt_dev *lro)
{
	if (lro->core.bar_addr) {
		/* unmap BAR */
		pci_iounmap(lro->core.pdev, lro->core.bar_addr);
		/* mark as unmapped */
		lro->core.bar_addr = NULL;
	}
	if (lro->core.intr_bar_addr) {
		/* unmap BAR */
		pci_iounmap(lro->core.pdev, lro->core.intr_bar_addr);
		/* mark as unmapped */
		lro->core.intr_bar_addr = NULL;
	}
}

static int identify_bar(struct xocl_dev_core *core, int bar)
{
	void *__iomem bar_addr;
	resource_size_t bar_len;

	bar_len = pci_resource_len(core->pdev, bar);
	bar_addr = pci_iomap(core->pdev, bar, bar_len);
	if (!bar_addr) {
		xocl_err(&core->pdev->dev, "Could not map BAR #%d",
				core->bar_idx);
		return -EIO;
	}

	/*
	 * did not find a better way to identify BARS. Currently,
	 * we have DSAs which rely VBNV name to differenciate them.
	 * And reading VBNV name needs to bring up Feature ROM.
	 * So we are not able to specify BARs in devices.h
	 */
	if (bar_len < 1024 * 1024 && bar > 0) {
		core->intr_bar_idx = bar;
		core->intr_bar_addr = bar_addr;
		core->intr_bar_size = bar_len;
	} else if (bar_len < 256 * 1024 * 1024) {
		core->bar_idx = bar;
		core->bar_size = bar_len;
		core->bar_addr = bar_addr;
	}

	return 0;
}

/* map_bars() -- map device regions into kernel virtual address space
 *
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed, given by the
 * bar_map_sizes[] array.
 */
static int map_bars(struct xclmgmt_dev *lro)
{
	struct pci_dev *pdev = lro->core.pdev;
	resource_size_t bar_len;
	int	i, ret = 0;

	for (i = PCI_STD_RESOURCES; i <= PCI_STD_RESOURCE_END; i++) {
		bar_len = pci_resource_len(pdev, i);
		if (bar_len > 0) {
			ret = identify_bar(&lro->core, i);
			if (ret)
				goto failed;
		}
	}

	/* succesfully mapped all required BAR regions */
	return 0;

failed:
	unmap_bars(lro);
	return ret;
}

void get_pcie_link_info(struct xclmgmt_dev *lro,
	unsigned short *link_width, unsigned short *link_speed, bool is_cap)
{
	u16 stat;
	long result;
	int pos = is_cap ? PCI_EXP_LNKCAP : PCI_EXP_LNKSTA;

	result = pcie_capability_read_word(lro->core.pdev, pos, &stat);
	if (result) {
		*link_width = *link_speed = 0;
		mgmt_err(lro, "Read pcie capability failed");
		return;
	}
	*link_width = (stat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
	*link_speed = stat & PCI_EXP_LNKSTA_CLS;
}

void device_info(struct xclmgmt_dev *lro, struct xclmgmt_ioc_info *obj)
{
	u32 val, major, minor, patch;
	struct FeatureRomHeader rom;

	memset(obj, 0, sizeof(struct xclmgmt_ioc_info));
	sscanf(XRT_DRIVER_VERSION, "%d.%d.%d", &major, &minor, &patch);

	obj->vendor = lro->core.pdev->vendor;
	obj->device = lro->core.pdev->device;
	obj->subsystem_vendor = lro->core.pdev->subsystem_vendor;
	obj->subsystem_device = lro->core.pdev->subsystem_device;
	obj->driver_version = XOCL_DRV_VER_NUM(major, minor, patch);
	obj->pci_slot = PCI_SLOT(lro->core.pdev->devfn);

	val = MGMT_READ_REG32(lro, GENERAL_STATUS_BASE);
	mgmt_info(lro, "MIG Calibration: %d \n", val);

	obj->mig_calibration[0] = (val & BIT(0)) ? true : false;
	obj->mig_calibration[1] = obj->mig_calibration[0];
	obj->mig_calibration[2] = obj->mig_calibration[0];
	obj->mig_calibration[3] = obj->mig_calibration[0];

	/*
	 * Get feature rom info
	 */
	obj->ddr_channel_num = xocl_get_ddr_channel_count(lro);
	obj->ddr_channel_size = xocl_get_ddr_channel_size(lro);
	obj->time_stamp = xocl_get_timestamp(lro);
	obj->isXPR = XOCL_DSA_XPR_ON(lro);
	xocl_get_raw_header(lro, &rom);
	memcpy(obj->vbnv, rom.VBNVName, 64);
	memcpy(obj->fpga, rom.FPGAPartName, 64);

	/* Get sysmon info */
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_TEMP, &val);
	obj->onchip_temp = val / 1000;
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_INT, &val);
	obj->vcc_int = val;
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_AUX, &val);
	obj->vcc_aux = val;
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_BRAM, &val);
	obj->vcc_bram = val;

	fill_frequency_info(lro, obj);
	get_pcie_link_info(lro, &obj->pcie_link_width, &obj->pcie_link_speed,
		false);
}

/*
 * Maps the PCIe BAR into user space for memory-like access using mmap().
 * Callable even when lro->ready == false.
 */
static int bridge_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc;
	struct xclmgmt_dev *lro;
	unsigned long off;
	unsigned long phys;
	unsigned long vsize;
	unsigned long psize;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	lro = (struct xclmgmt_dev *)file->private_data;
	BUG_ON(!lro);

	off = vma->vm_pgoff << PAGE_SHIFT;
	/* BAR physical address */
	phys = pci_resource_start(lro->core.pdev, lro->core.bar_idx) + off;
	vsize = vma->vm_end - vma->vm_start;
	/* complete resource */
	psize = pci_resource_end(lro->core.pdev, lro->core.bar_idx) -
		pci_resource_start(lro->core.pdev, lro->core.bar_idx) + 1 - off;

	mgmt_info(lro, "mmap(): bar %d, phys:0x%lx, vsize:%ld, psize:%ld",
		lro->core.bar_idx, phys, vsize, psize);

	if (vsize > psize)
		return -EINVAL;

	/*
	 * pages must not be cached as this would result in cache line sized
	 * accesses to the end point
	 */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/*
	 * prevent touching the pages (byte access) for swap-in,
	 * and prevent the pages from being swapped out
	 */
#ifndef VM_RESERVED
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
#else
	vma->vm_flags |= VM_IO | VM_RESERVED;
#endif

	/* make MMIO accessible to user space */
	rc = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
				vsize, vma->vm_page_prot);
	if (rc)
		return -EAGAIN;

	return rc;
}

/*
 * character device file operations for control bus (through control bridge)
 */
static struct file_operations ctrl_fops = {
	.owner = THIS_MODULE,
	.open = char_open,
	.release = char_close,
	.mmap = bridge_mmap,
	.unlocked_ioctl = mgmt_ioctl,
};

/*
 * create_char() -- create a character device interface to data or control bus
 *
 * If at least one SG DMA engine is specified, the character device interface
 * is coupled to the SG DMA file operations which operate on the data bus. If
 * no engines are specified, the interface is coupled with the control bus.
 */
static int create_char(struct xclmgmt_dev *lro)
{
	struct xclmgmt_char *lro_char;
	int rc;

	lro_char = &lro->user_char_dev;

	/* couple the control device file operations to the character device */
	cdev_init(&lro_char->cdev, &ctrl_fops);
	lro_char->cdev.owner = THIS_MODULE;
	lro_char->cdev.dev = MKDEV(MAJOR(xclmgmt_devnode), lro->core.dev_minor);
	rc = cdev_add(&lro_char->cdev, lro_char->cdev.dev, 1);
	if (rc < 0) {
		memset(lro_char, 0, sizeof(*lro_char));
		printk(KERN_INFO "cdev_add() = %d\n", rc);
		goto fail_add;
	}

	lro_char->sys_device = device_create(xrt_class,
				&lro->core.pdev->dev,
				lro_char->cdev.dev, NULL,
			 	DRV_NAME "%d", lro->instance);

	if (IS_ERR(lro_char->sys_device)) {
		rc = PTR_ERR(lro_char->sys_device);
		goto fail_device;
	}

	return 0;

fail_device:
	cdev_del(&lro_char->cdev);
fail_add:
	return rc;
}

static int destroy_sg_char(struct xclmgmt_char *lro_char)
{
	BUG_ON(!lro_char);
	BUG_ON(!xrt_class);

	if (lro_char->sys_device)
		device_destroy(xrt_class, lro_char->cdev.dev);
	cdev_del(&lro_char->cdev);

	return 0;
}

struct pci_dev *find_user_node(const struct pci_dev *pdev)
{
	struct xclmgmt_dev *lro;
	unsigned int slot = PCI_SLOT(pdev->devfn);
	unsigned int func = PCI_FUNC(pdev->devfn);
	struct pci_dev *user_dev;

	lro = (struct xclmgmt_dev *)dev_get_drvdata(&pdev->dev);

	/*
	 * if we are function one then the zero
	 * function has the user pf node
	 */
	if (func == 0) {
		mgmt_err(lro, "failed get user pf, expect user pf is func 0");
		return NULL;
	}

	user_dev = pci_get_slot(pdev->bus, PCI_DEVFN(slot, 0));
	if (!user_dev) {
		mgmt_err(lro, "did not find user dev");
		return NULL;
	}

	return user_dev;
}

inline void check_temp_within_range(struct xclmgmt_dev *lro, u32 temp)
{
	if(temp < LOW_TEMP || temp > HI_TEMP) {
		mgmt_err(lro, "Temperature outside normal range (%d-%d) %d.",
			LOW_TEMP, HI_TEMP, temp);
	}
}

inline void check_volt_within_range(struct xclmgmt_dev *lro, u16 volt)
{
	if(volt < LOW_MILLVOLT || volt > HI_MILLVOLT) {
		mgmt_err(lro, "Voltage outside normal range (%d-%d)mV %d.",
			LOW_MILLVOLT, HI_MILLVOLT, volt);
	}
}

static void check_sysmon(struct xclmgmt_dev *lro)
{
	u32 val;

	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_TEMP, &val);
	check_temp_within_range(lro, val);

	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_INT, &val);
	check_volt_within_range(lro, val);
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_AUX, &val);
	check_volt_within_range(lro, val);
	xocl_sysmon_get_prop(lro, XOCL_SYSMON_PROP_VCC_BRAM, &val);
	check_volt_within_range(lro, val);
}

static int health_check_cb(void *data)
{
	struct xclmgmt_dev *lro = (struct xclmgmt_dev *)data;
	struct mailbox_req mbreq = { MAILBOX_REQ_FIREWALL, };
	bool tripped;

	if (!health_check)
		return 0;

	mutex_lock(&lro->busy_mutex);
	tripped = xocl_af_check(lro, NULL);
	mutex_unlock(&lro->busy_mutex);

	if (!tripped) {
		check_sysmon(lro);
	} else {
		mgmt_info(lro, "firewall tripped, notify peer");
		(void) xocl_peer_notify(lro, &mbreq);
	}

	return 0;
}

static inline bool xclmgmt_support_intr(struct xclmgmt_dev *lro)
{
	return lro->core.intr_bar_addr != NULL;
}

static int xclmgmt_setup_msix(struct xclmgmt_dev *lro)
{
	int total, rv, i;

	if (!xclmgmt_support_intr(lro))
		return -EOPNOTSUPP;

	/*
	 * Get start vector (index into msi-x table) of msi-x usr intr on this
	 * device.
	 *
	 * The device has XCLMGMT_MAX_USER_INTR number of usr intrs, the last
	 * half of them belongs to mgmt pf, and the first half to user pf. All
	 * vectors are hard-wired.
	 *
	 * The device also has some number of DMA intrs whose vectors come
	 * before usr ones.
	 *
	 * This means that mgmt pf needs to allocate msi-x table big enough to
	 * cover its own usr vectors. So, only the last chunk of the table will
	 * ever be used for mgmt pf.
	 */
	lro->msix_user_start_vector = XOCL_READ_REG32(lro->core.intr_bar_addr +
		XCLMGMT_INTR_USER_VECTOR) & 0x0f;
	total = lro->msix_user_start_vector + XCLMGMT_MAX_USER_INTR;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	i = 0; // Suppress warning about unused variable
	rv = pci_alloc_irq_vectors(lro->core.pdev, total, total, PCI_IRQ_MSIX);
	if (rv == total)
		rv = 0;
#else
	for (i = 0; i < total; i++)
		lro->msix_irq_entries[i].entry = i;
	rv = pci_enable_msix(lro->core.pdev, lro->msix_irq_entries, total);
#endif
	printk(KERN_INFO "setting up msix, total irqs: %d, rv=%d\n", total, rv);
	return rv;
}

static void xclmgmt_teardown_msix(struct xclmgmt_dev *lro)
{
	if (xclmgmt_support_intr(lro))
		pci_disable_msix(lro->core.pdev);
}

static int xclmgmt_intr_config(xdev_handle_t xdev_hdl, u32 intr, bool en)
{
	struct xclmgmt_dev *lro = (struct xclmgmt_dev *)xdev_hdl;

	if (!xclmgmt_support_intr(lro))
		return -EOPNOTSUPP;

	XOCL_WRITE_REG32(1 << intr, lro->core.intr_bar_addr +
		(en ? XCLMGMT_INTR_USER_ENABLE : XCLMGMT_INTR_USER_DISABLE));
	return 0;
}

static int xclmgmt_intr_register(xdev_handle_t xdev_hdl, u32 intr,
	irq_handler_t handler, void *arg)
{
	u32 vec;
	struct xclmgmt_dev *lro = (struct xclmgmt_dev *)xdev_hdl;

	if (!xclmgmt_support_intr(lro))
		return -EOPNOTSUPP;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	vec = pci_irq_vector(lro->core.pdev,
		lro->msix_user_start_vector + intr);
#else
	vec = lro->msix_irq_entries[
		lro->msix_user_start_vector + intr].vector;
#endif

	if (handler)
		return request_irq(vec, handler, 0, DRV_NAME, arg);

	free_irq(vec, arg);
	return 0;
}

struct xocl_pci_funcs xclmgmt_pci_ops = {
	.intr_config = xclmgmt_intr_config,
	.intr_register = xclmgmt_intr_register,
};

static int xclmgmt_xclbin_download(struct xclmgmt_dev *lro ,void *data)
{
	struct mailbox_req *req = (struct mailbox_req *)data;

	/* is magic number*/
	if(memcmp(req->u.xclbin_reg.data, ICAP_XCLBIN_V2, sizeof(ICAP_XCLBIN_V2)))
		return -EINVAL;

	return xocl_icap_download_axlf(lro, req->u.xclbin_reg.data);
}

static int xbmgmt_mb_peer_data_broker(struct xclmgmt_dev *lro)
{
	int ret = -EINVAL;

	switch(lro->data_buf.cmd_type){
		case MB_CMD_LOAD_XCLBIN:
			ret = xocl_icap_download_axlf(lro, lro->data_buf.data_buf);
			break;
		case MB_CMD_RECLOCK:
			ret = xocl_icap_ocl_update_clock_freq_topology(lro, (struct xclmgmt_ioc_freqscaling*)lro->data_buf.data_buf);
			break;
		default:
			printk(KERN_ERR "Can't recognize data_type : %u\n", lro->data_buf.cmd_type);
		  break;
	}

	return ret;
}


static int xclmgmt_mb_data_alloc_and_recv(struct xclmgmt_dev *lro ,void *data)
{
	int ret = 0;
	struct mailbox_req *req = (struct mailbox_req *)data;
	uint32_t data_total_len = req->u.data_buf.data_total_len;
	uint32_t len = req->u.data_buf.len;
	uint32_t offset = req->u.data_buf.offset;
	char *data_ptr = req->u.data_buf.data;

	if(lro->data_buf.data_buf == NULL){
		printk(KERN_INFO "lro->data_buf.data_buf init ...\n");

		lro->data_buf.data_buf = vmalloc(data_total_len);
		if(lro->data_buf.data_buf == NULL){
			printk(KERN_ERR "fail to alloc lro->data_buf.data_buf  ...\n");
			ret = -ENOMEM;
			return ret;
		}
		lro->data_buf.cmd_type = req->u.data_buf.cmd_type;
		lro->data_buf.priv_data = req->u.data_buf.priv_data;
	}

	memcpy(lro->data_buf.data_buf+offset, data_ptr, len);

	if(offset+len == data_total_len){
		printk(KERN_INFO "Get whole data ...\n");
		ret = xbmgmt_mb_peer_data_broker(lro);
		vfree(lro->data_buf.data_buf);
		lro->data_buf.data_buf = NULL;
	}
	else if ((offset+len) > data_total_len){
		vfree(lro->data_buf.data_buf);
		lro->data_buf.data_buf = NULL;
		ret = -ENOMEM;
	}

	return ret;
}

static void xclmgmt_mailbox_srv(void *arg, void *data, size_t len,
	u64 msgid, int err)
{
	int ret;
	struct xclmgmt_dev *lro = (struct xclmgmt_dev *)arg;
	struct mailbox_req *req = (struct mailbox_req *)data;

	if (err != 0)
		return;

	printk(KERN_INFO "%s received request (%d) from peer\n", __func__, req->req);

	switch (req->req) {
	case MAILBOX_REQ_LOCK_BITSTREAM:
		ret = xocl_icap_lock_bitstream(lro, &req->u.req_bit_lock.uuid,
			req->u.req_bit_lock.pid);
		(void) xocl_peer_response(lro, msgid, &ret, sizeof (ret));
		break;
	case MAILBOX_REQ_UNLOCK_BITSTREAM:
		ret = xocl_icap_unlock_bitstream(lro, &req->u.req_bit_lock.uuid,
			req->u.req_bit_lock.pid);
		(void) xocl_peer_response(lro, msgid, &ret, sizeof (ret));
		break;
	case MAILBOX_REQ_HOT_RESET:
		ret = (int) reset_hot_ioctl(lro);
		(void) xocl_peer_response(lro, msgid, &ret, sizeof (ret));
	case MAILBOX_REQ_DOWNLOAD_XCLBIN:
		ret = xclmgmt_xclbin_download(lro, data);
		(void) xocl_peer_response(lro, msgid, &ret, sizeof (ret));
		break;
	case MAILBOX_REQ_SEND_DATA:
		ret = xclmgmt_mb_data_alloc_and_recv(lro, data);
		(void) xocl_peer_response(lro, msgid, &ret, sizeof (ret));
		break;
	default:
		break;
	}
}

/*
 * Called after minimum initialization is done. Should not return failure.
 * If something goes wrong, it should clean up and return back to minimum
 * initialization stage.
 */
static void xclmgmt_extended_probe(struct xclmgmt_dev *lro)
{
	int ret;
	struct xocl_board_private *dev_info = &lro->core.priv;
	struct pci_dev *pdev = lro->pci_dev;

	/* We can only support MSI-X. */
	ret = xclmgmt_setup_msix(lro);
	if (ret && (ret != -EOPNOTSUPP)) {
		xocl_err(&pdev->dev, "set up MSI-X failed\n");
		goto fail;
	}
	lro->core.pci_ops = &xclmgmt_pci_ops;
	lro->core.pdev = pdev;

	/*
	 * Workaround needed on some platforms. Will clear out any stale
	 * data after the platform has been reset
	 */
	ret = xocl_subdev_create_one(lro,
		&(struct xocl_subdev_info)XOCL_DEVINFO_AF);
	if (ret) {
		xocl_err(&pdev->dev, "failed to register firewall\n");
		goto fail_firewall;
	}
	if(dev_info->flags & XOCL_DSAFLAG_AXILITE_FLUSH)
		platform_axilite_flush(lro);

	ret = xocl_subdev_create_all(lro, dev_info->subdev_info,
		dev_info->subdev_num);
	if (ret) {
		xocl_err(&pdev->dev, "failed to register subdevs\n");
		goto fail_all_subdev;
	}
	xocl_err(&pdev->dev, "created all sub devices");

	ret = xocl_icap_download_boot_firmware(lro);
	if (ret)
		goto fail_all_subdev;

	xocl_af_start_health_check(lro, health_check_cb, lro,
		health_interval * 1000);

	/* Launch the mailbox server. */
	(void) xocl_peer_listen(lro, xclmgmt_mailbox_srv, (void *)lro);

	lro->ready = true;
	xocl_err(&pdev->dev, "device fully initialized\n");
	return;

fail_all_subdev:
	xocl_subdev_destroy_all(lro);
fail_firewall:
	xclmgmt_teardown_msix(lro);
fail:
	xocl_err(&pdev->dev, "failed to fully probe device, err: %d\n", ret);
}

/*
 * Device initialization is done in two phases:
 * 1. Minimum initialization - init to the point where open/close/mmap entry
 * points are working, sysfs entries work without register access, ioctl entry
 * point is completely disabled.
 * 2. Full initialization - driver is ready for use.
 * Once we pass minimum initialization point, probe function shall not fail.
 */
static int xclmgmt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc = 0;
	struct xclmgmt_dev *lro = NULL;
	struct xocl_board_private *dev_info;

	xocl_info(&pdev->dev, "Driver: %s", XRT_DRIVER_VERSION);
	xocl_info(&pdev->dev, "probe(pdev = 0x%p, pci_id = 0x%p)\n", pdev, id);

	rc = pci_enable_device(pdev);
	if (rc) {
		xocl_err(&pdev->dev, "pci_enable_device() failed, rc = %d.\n",
			rc);
		return rc;
	}

	/* allocate zeroed device book keeping structure */
	lro = kzalloc(sizeof(struct xclmgmt_dev), GFP_KERNEL);
	if (!lro) {
		xocl_err(&pdev->dev, "Could not kzalloc(xclmgmt_dev).\n");
		rc = -ENOMEM;
		goto err_alloc;
	}

	/* create a device to driver reference */
	dev_set_drvdata(&pdev->dev, lro);
	/* create a driver to device reference */
	lro->core.pdev = pdev;
	lro->pci_dev = pdev;
	lro->ready = false;

	rc = pcie_get_readrq(pdev);
        if (rc < 0) {
                dev_err(&pdev->dev, "failed to read mrrs %d\n", rc);
                goto err_alloc;
        }
        if (rc > 512) {
                rc = pcie_set_readrq(pdev, 512);
                if (rc) {
                        dev_err(&pdev->dev, "failed to force mrrs %d\n", rc);
                        goto err_alloc;
                }
        }

	rc = xocl_alloc_dev_minor(lro);
	if (rc)
		goto err_alloc_minor;

	rc = pci_request_regions(pdev, DRV_NAME);
	/* could not request all regions? */
	if (rc) {
		xocl_err(&pdev->dev, "pci_request_regions() = %d\n", rc);
		goto err_regions;
	}

	dev_info = (struct xocl_board_private *)id->driver_data;
	xocl_fill_dsa_priv(lro, dev_info);

	/* map BARs */
	rc = map_bars(lro);
	if (rc)
		goto err_map;

	lro->instance = XOCL_DEV_ID(pdev);
	rc = create_char(lro);
	lro->data_buf.data_buf = NULL;
	if (rc) {
		xocl_err(&pdev->dev, "create_char(user_char_dev) failed\n");
		goto err_cdev;
	}
	mutex_init(&lro->busy_mutex);

	mgmt_init_sysfs(&pdev->dev);

	/* Probe will not fail from now on. */
	xocl_err(&pdev->dev, "minimum initialization done\n");

	xocl_core_init(lro, NULL);

	/* No further initialization for MFG board. */
	if (minimum_initialization ||
		(dev_info->flags & XOCL_DSAFLAG_MFG) != 0) {
		return 0;
	}

	xclmgmt_extended_probe(lro);

	return 0;

err_cdev:
	unmap_bars(lro);
err_map:
	pci_release_regions(pdev);
err_regions:
	xocl_free_dev_minor(lro);
err_alloc_minor:
	kfree(lro);
	dev_set_drvdata(&pdev->dev, NULL);
err_alloc:
	pci_disable_device(pdev);

	return rc;
}

static void xclmgmt_remove(struct pci_dev *pdev)
{
	struct xclmgmt_dev *lro;

	if ((pdev == 0) || (dev_get_drvdata(&pdev->dev) == 0)) {
		return;
	}

	lro = (struct xclmgmt_dev *)dev_get_drvdata(&pdev->dev);
	mgmt_info(lro, "remove(0x%p) where pdev->dev.driver_data = 0x%p",
	       pdev, lro);
	BUG_ON(lro->core.pdev != pdev);

	mgmt_fini_sysfs(&pdev->dev);

	xocl_subdev_destroy_all(lro);

	xclmgmt_teardown_msix(lro);
	/* remove user character device */
	destroy_sg_char(&lro->user_char_dev);

	/* unmap the BARs */
	unmap_bars(lro);
	pci_disable_device(pdev);
	pci_release_regions(pdev);

	xocl_free_dev_minor(lro);

	dev_set_drvdata(&pdev->dev, NULL);

	xocl_core_fini(lro);
}

static pci_ers_result_t mgmt_pci_error_detected(struct pci_dev *pdev,
	pci_channel_state_t state)
{
	switch (state) {
	case pci_channel_io_normal:
		xocl_info(&pdev->dev, "PCI normal state error\n");
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		xocl_info(&pdev->dev, "PCI frozen state error\n");
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		xocl_info(&pdev->dev, "PCI failure state error\n");
		return PCI_ERS_RESULT_DISCONNECT;
	default:
		xocl_info(&pdev->dev, "PCI unknown state %d error\n", state);
		break;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static const struct pci_error_handlers xclmgmt_err_handler = {
	.error_detected = mgmt_pci_error_detected,
};

static struct pci_driver xclmgmt_driver = {
	.name = DRV_NAME,
	.id_table = pci_ids,
	.probe = xclmgmt_probe,
	.remove = xclmgmt_remove,
	/* resume, suspend are optional */
	.err_handler = &xclmgmt_err_handler,
};

static int (*drv_reg_funcs[])(void) __initdata = {
	xocl_init_feature_rom,
	xocl_init_sysmon,
	xocl_init_mb,
	xocl_init_xvc,
	xocl_init_xiic,
	xocl_init_mailbox,
	xocl_init_firewall,
	xocl_init_icap,
	xocl_init_mig,
	xocl_init_xmc,
	xocl_init_dna,
};

static void (*drv_unreg_funcs[])(void) = {
	xocl_fini_feature_rom,
	xocl_fini_sysmon,
	xocl_fini_mb,
	xocl_fini_xvc,
	xocl_fini_xiic,
	xocl_fini_mailbox,
	xocl_fini_firewall,
	xocl_fini_icap,
	xocl_fini_mig,
	xocl_fini_xmc,
	xocl_fini_dna,
};

static int __init xclmgmt_init(void)
{
	int res, i;

	pr_info(DRV_NAME " init()\n");
	xrt_class = class_create(THIS_MODULE, "xrt_mgmt");
	if (IS_ERR(xrt_class))
		return PTR_ERR(xrt_class);

	res = alloc_chrdev_region(&xclmgmt_devnode, 0,
				  XOCL_MAX_DEVICES, DRV_NAME);
	if (res)
		goto alloc_err;

	/* Need to init sub device driver before pci driver register */
	for (i = 0; i < ARRAY_SIZE(drv_reg_funcs); ++i) {
		res = drv_reg_funcs[i]();
		if (res) {
			goto drv_init_err;
		}
	}

	res = pci_register_driver(&xclmgmt_driver);
	if (res)
		goto reg_err;

	return 0;

drv_init_err:
reg_err:
	for (i--; i >= 0; i--) {
		drv_unreg_funcs[i]();
	}
	unregister_chrdev_region(xclmgmt_devnode, XOCL_MAX_DEVICES);
alloc_err:
	pr_info(DRV_NAME " init() err\n");
	class_destroy(xrt_class);
	return res;
}

static void xclmgmt_exit(void)
{
	int i;

	pr_info(DRV_NAME" exit()\n");
	pci_unregister_driver(&xclmgmt_driver);

	for (i = ARRAY_SIZE(drv_unreg_funcs) - 1; i >= 0; i--) {
		drv_unreg_funcs[i]();
	}
	/* unregister this driver from the PCI bus driver */
	unregister_chrdev_region(xclmgmt_devnode, XOCL_MAX_DEVICES);
	class_destroy(xrt_class);
}

module_init(xclmgmt_init);
module_exit(xclmgmt_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lizhi Hou <lizhi.hou@xilinx.com>");
MODULE_VERSION(XRT_DRIVER_VERSION);
MODULE_DESCRIPTION("Xilinx SDx management function driver");
