/***************************************************************************/
/**                   I N T E L  C O N F I D E N T I A L                  **/
/***************************************************************************/
/**                                                                       **/
/**  Copyright (c) 2016 Intel Corporation                                 **/
/**                                                                       **/
/**  This program contains proprietary and confidential information.      **/
/**  All rights reserved.                                                 **/
/**                                                                       **/
/***************************************************************************/
/**                   I N T E L  C O N F I D E N T I A L                  **/
/***************************************************************************/

#include <linux/svos.h>
#include <linux/svos_svfs_exports.h>
#include <asm/e820/api.h>

/*
 * Starting point for svos hooks in the arch tree.
 * The strategy is to minimize code placement in base kernel
 * files to just hook calls or minor changes.
 */

int svos_enable_ras_errorcorrect = 0;
unsigned long svos1stTargetPage;
static int svos_memory_split = 0;
unsigned long long svos_split_after = 0x4000000; /* Default is 64MB */
unsigned long long svos_split_above = 0x100000000;
struct e820_table e820_svos;
EXPORT_SYMBOL(e820_svos);
struct	svos_node_memory svos_node_memory[MAX_NUMNODES];
nodemask_t svos_nodes_parsed;
EXPORT_SYMBOL(svos_node_memory);
EXPORT_SYMBOL(svos_nodes_parsed);

/*
 * Enable error correction if indicated on kernel command line.
 */
static int __init svos_enable_ras(char *str)
{
	svos_enable_ras_errorcorrect = 1;
	return 1;
}
early_param("svos_enable_ras", svos_enable_ras);
EXPORT_SYMBOL(svos_enable_ras_errorcorrect);

__init unsigned long
svos_adjgap( unsigned long gapsize )
{
	unsigned long round;
	unsigned long start;

	round = 0x100000;
	while ((gapsize >> 4) > round)
		round += round;

	start = (gapsize + round) & -round;
	return start;
}

/*
 * Handle the svos memory parameters
 */
static __init int memory_setup(char *opt)
{
	if (!opt)
		return -EINVAL;

	if(!strncmp(opt, "split_above=", 12)){
		opt+=12;
		svos_split_above = memparse(opt, &opt);
	}

	if(!strncmp(opt, "split=", 6)){
		opt+=6;
		svos_memory_split = memparse(opt, &opt);
	}
	if(!strncmp(opt, "split_after=", 12)) {
		opt+=12;
		svos_split_after = memparse(opt, &opt);
	}
	return 0;
}
early_param("svos_memory", memory_setup);

#define GAP_SIZE	0x40000000LL /* 1GB */
void __init svos_mem_init(void)
{
	unsigned long long target_space = svos1stTargetPage << PAGE_SHIFT;
	unsigned long long accum_size;
	unsigned long long above_addr;
	struct e820_entry *ep;
	int i;

	/* Disable user address space randomization */
	randomize_va_space = 0;

	memset(&e820_svos,0,sizeof(e820_svos));
	memcpy(&e820_svos,e820_table,sizeof(struct e820_table));
	//
	// if no svos memory is specified or svos@ is zero
	// act as if svos@ is set to max mem.
	//
	if( target_space == 0ULL )
		return;

	//
	// scan the e820 map and figure out what address will give
	// us enough memory to satisfy the svos@ boot parameter.
	// scan only the high memory > 4G.
	// we shouldn't forget the take into account split_after and split_above.
	//
	// Simple case no split just look for an addr that wil give
	// us the requested kernel memory and remove the rest for SVOS.
	//
	accum_size = 0;
	if( svos_memory_split == 0 ){
		for (i = 0; i < e820_table->nr_entries; i++) {
			ep =  &e820_table->entries[i];
			if( ep->type != E820_TYPE_RAM )
				continue;

			if( (accum_size + ep->size) >= target_space ){
				e820__range_remove( ep->addr + (target_space - accum_size), ULLONG_MAX, E820_TYPE_RAM,1);
				return;
			}

			accum_size += ep->size;
		}
		// fall through the else to common error return
	}

	//
	// more complicated case.
	// Take into account split_after and split_above values.
	// Kernel gets memory from 0 - split_after.
	// SVOS   gets memory from split_after to split_above.
	// Kernel gets memory from split_above to (SVOS@ - split_after).
	// SVOS   get the remainder.
	// the effective value of split_above may need to be modified
	// to take into account holes in ram especially in high ram
	// where the holes can be GBs in size.
	//

	else {
		above_addr = 0;
		for(i = 0; i < e820_table->nr_entries; i++){
			ep = &e820_table->entries[i];
			if( ep->type != E820_TYPE_RAM )
				continue;

			if( (ep->addr + ep->size) <= svos_split_above )
				continue;

			if( above_addr == 0 ){		// first segment to look at
				accum_size = svos_split_after;
				//
				// set the above address to be either the boot param value
				// or the first memory address above the boot param value.
				//
				if( ep->addr < svos_split_above ) {
					above_addr = svos_split_above;
					accum_size += ep->size - (svos_split_above - ep->addr);
				}
				else{
					above_addr = ep->addr;
					accum_size += ep->size;
				}
			}
			else
				accum_size += ep->size;
			//
			// Once we have accumulated enough space
			// remove the svos areas from the e820 map.
			//
			if( accum_size >= target_space ){
				e820__range_remove( svos_split_after, (svos_split_above - svos_split_after), E820_TYPE_RAM,1);
				e820__range_remove( above_addr + (target_space - svos_split_after), ULLONG_MAX, E820_TYPE_RAM,1);
				return;
			}
		}
	}
	//
	// common error return
	//
	printk( KERN_ERR "%s : not enough memory to satisfy svos@ mem parameter\n", __FUNCTION__);
	return;
}

/*
 * called from the parse_memopt handling to initialize the svosmem 
 * parameter.
 */
void
svos_parse_mem( char *p )
{
	u64	mem_size = memparse( p+5, &p );

	svos1stTargetPage = mem_size >>PAGE_SHIFT;
	return;
}

//
// Trap hook called from do_trap_no_signal on traps
//   the handler return code tells trap code whether to
//   continue the normal processing or return.
//
int (*svTrapHandlerKernelP)(int index, struct pt_regs *regs);
EXPORT_SYMBOL(svTrapHandlerKernelP);
int
svos_trap_hook(int trapnr, struct pt_regs *regs)
{
	int	trapResult = 0;
	if( svTrapHandlerKernelP != NULL) {
		trapResult = svTrapHandlerKernelP(trapnr, regs);
	}
	return trapResult;
}
EXPORT_SYMBOL(svos_trap_hook);

EXPORT_SYMBOL(init_mm);
struct mm_struct *svoskern_init_mm = &init_mm;
EXPORT_SYMBOL(svoskern_init_mm);

struct task_struct *svoskern_find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns)
{
	return find_task_by_pid_ns(nr, ns);
}
EXPORT_SYMBOL(svoskern_find_task_by_pid_ns);

extern struct list_head pci_mmcfg_list;
struct list_head *svoskern_pci_mmcfg_list = &pci_mmcfg_list;
EXPORT_SYMBOL(svoskern_pci_mmcfg_list);

// These will ultimately be renamed as follows ***start here***
//int svos_memory_split = 0;
//unsigned long long svoskern_split_after = 0x4000000; //Default is 64MB
//unsigned long long svoskern_split_above = 0x100000000;
//EXPORT_SYMBOL(svoskern_split_after);
//EXPORT_SYMBOL(svoskern_split_above);  // TO_BE_DEPRECATED - no longer needed

//struct e820_table svoskern_e820_table;
//EXPORT_SYMBOL(svoskern_e820_table);

//struct svos_node_memory svoskern_node_memory[MAX_NUMNODES];
//nodemask_t svoskern_nodes_parsed;
//EXPORT_SYMBOL(svoskern_node_memory);
//EXPORT_SYMBOL(svoskern_nodes_parsed);
// These will ultimately be renamed ***end here***

unsigned long
svoskern_ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}
EXPORT_SYMBOL(svoskern_ksys_mmap_pgoff);

EXPORT_SYMBOL(vector_irq);

int (*svoskern_svfs_callback_trap_handler)(int index, struct pt_regs *regs);
EXPORT_SYMBOL(svoskern_svfs_callback_trap_handler);

#ifdef CONFIG_DMAR_TABLE
// Hooks for syncing vtd state between the kernel's DMAR driver and the
// SVFS vt-d driver.
int (*svoskern_svfs_callback_vtd_submit_sync)(u64, void *) = NULL;
EXPORT_SYMBOL(svoskern_svfs_callback_vtd_submit_sync);

int (*svoskern_svfs_callback_vtd_fault_handler)(u64, void *) = NULL;
EXPORT_SYMBOL(svoskern_svfs_callback_vtd_fault_handler);

void svoskern_svfs_callback_reset_vtd_inval_que(u64 reg_phys_address)
{
	printk(KERN_CRIT "%s called to handle address - %LX\n", __FUNCTION__,
		reg_phys_address);
}
EXPORT_SYMBOL(svoskern_svfs_callback_reset_vtd_inval_que);
#endif

void
svoskern_lock_pci(void)
{
//	spin_lock_irq(&pci_lock);
	return;
}
EXPORT_SYMBOL(svoskern_lock_pci);

void
svoskern_unlock_pci(void)
{
//	spin_unlock_irq(&pci_lock);
	return;
}
EXPORT_SYMBOL(svoskern_unlock_pci);

void
svoskern_lock_pci_irqsave(unsigned long *flags)
{
//	spin_lock_irq(&pci_lock);
	return;
}
EXPORT_SYMBOL(svoskern_lock_pci_irqsave);

void
svoskern_unlock_pci_irqrestore(unsigned long *flags)
{
//	spin_unlock_irq(&pci_lock);
	return;
}
EXPORT_SYMBOL(svoskern_unlock_pci_irqrestore);

int
svoskern_pci_setup_device(struct pci_dev *dev)
{
	extern int pci_setup_device(struct pci_dev *dev);
	return pci_setup_device(dev);
}
EXPORT_SYMBOL(svoskern_pci_setup_device);

void
svoskern_pci_device_add(struct pci_dev *dev, struct pci_bus *bus)
{
	pci_device_add(dev, bus);
}
EXPORT_SYMBOL(svoskern_pci_device_add);

unsigned long
svoskern_get_cr4_features(void)
{
	return mmu_cr4_features;
}
EXPORT_SYMBOL(svoskern_get_cr4_features);

void
svoskern_clear_in_cr4(unsigned long mask)
{
	cr4_clear_bits(mask);
}
EXPORT_SYMBOL(svoskern_clear_in_cr4);

void
svoskern_set_in_cr4(unsigned long mask)
{
	cr4_set_bits(mask);
}
EXPORT_SYMBOL(svoskern_set_in_cr4);

unsigned long
svoskern_native_read_cr0(void)
{
	return native_read_cr0();
}
EXPORT_SYMBOL(svoskern_native_read_cr0);

unsigned long
svoskern_native_read_cr2(void)
{
	return native_read_cr2();
}
EXPORT_SYMBOL(svoskern_native_read_cr2);

unsigned long
svoskern_native_read_cr3(void)
{
	return __read_cr3();
}
EXPORT_SYMBOL(svoskern_native_read_cr3);

unsigned long
svoskern_native_read_cr4(void)
{
	return native_read_cr4();
}
EXPORT_SYMBOL(svoskern_native_read_cr4);

void
svoskern_native_write_cr0(unsigned long val)
{
	native_write_cr0(val);
}
EXPORT_SYMBOL(svoskern_native_write_cr0);

void
svoskern_native_write_cr2(unsigned long val)
{
	native_write_cr2(val);
}
EXPORT_SYMBOL(svoskern_native_write_cr2);

void
svoskern_native_write_cr3(unsigned long val)
{
	native_write_cr3(val);
}
EXPORT_SYMBOL(svoskern_native_write_cr3);

void
svoskern_native_write_cr4(unsigned long val)
{
	native_write_cr4(val);
}
EXPORT_SYMBOL(svoskern_native_write_cr4);

bool svoskern_pat_enabled(void)
{
	return pat_enabled();
}
EXPORT_SYMBOL(svoskern_pat_enabled);

u8 svoskern_mtrr_type_lookup(u64 start, u64 end, u8 *uniform)
{
	return mtrr_type_lookup(start, end, uniform);
}
EXPORT_SYMBOL(svoskern_mtrr_type_lookup);

int svoskern__irq_domain_alloc_irqs(struct irq_domain *domain, int irq_base,
				     unsigned int nr_irqs, int node, void *arg,
				     bool realloc)
{
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	return __irq_domain_alloc_irqs(domain, irq_base, nr_irqs, node,
				       arg, realloc, NULL);
#else
	return -1;
#endif
}
EXPORT_SYMBOL(svoskern__irq_domain_alloc_irqs);

void svoskern_flush_tlb_page(struct vm_area_struct *vma, unsigned long page_addr)
{
	flush_tlb_page(vma, page_addr);
}
EXPORT_SYMBOL(svoskern_flush_tlb_page);

void svoskern_flush_tlb_local(void)
{
        flush_tlb_local();
}
EXPORT_SYMBOL(svoskern_flush_tlb_local);

void svoskern_flush_tlb_all(void)
{
        flush_tlb_all();
}
EXPORT_SYMBOL(svoskern_flush_tlb_all);

void svoskern_set_cpu_online(unsigned int cpu, bool online)
{
	set_cpu_online(cpu, online);
}
EXPORT_SYMBOL(svoskern_set_cpu_online);

#ifdef CONFIG_KALLSYMS
unsigned long svoskern_kallsyms_lookup_name(const char *name)
{
	return kallsyms_lookup_name(name);
}
EXPORT_SYMBOL(svoskern_kallsyms_lookup_name);
#endif
