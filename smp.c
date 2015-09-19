/*
 * Generic helpers for smp ipi calls
 *
 * (C) Jens Axboe <jens.axboe@oracle.com> 2008
 */
#include <linux/irq_work.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/sched.h>

#include "smpboot.h"

//---------------------------
#include <linux/pci_regs.h> //PCI_CLASS_REVISION
#include <linux/pci_ids.h> //PCI_CLASS_SERIAL_USB_EHCI
#include <linux/usb/ehci_def.h> //CMD_LRESET
#include <linux/delay.h> //mdelay
#include <asm/pci-direct.h> //read_pci_config
//---------------------------

enum {
	CSD_FLAG_LOCK		= 0x01,
	CSD_FLAG_SYNCHRONOUS	= 0x02,
};

struct call_function_data {
	struct call_single_data	__percpu *csd;
	cpumask_var_t		cpumask;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct call_function_data, cfd_data);

static DEFINE_PER_CPU_SHARED_ALIGNED(struct llist_head, call_single_queue);

static void flush_smp_call_function_queue(bool warn_cpu_offline);

static int
hotplug_cfd(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	struct call_function_data *cfd = &per_cpu(cfd_data, cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		if (!zalloc_cpumask_var_node(&cfd->cpumask, GFP_KERNEL,
				cpu_to_node(cpu)))
			return notifier_from_errno(-ENOMEM);
		cfd->csd = alloc_percpu(struct call_single_data);
		if (!cfd->csd) {
			free_cpumask_var(cfd->cpumask);
			return notifier_from_errno(-ENOMEM);
		}
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		/* Fall-through to the CPU_DEAD[_FROZEN] case. */

	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		free_cpumask_var(cfd->cpumask);
		free_percpu(cfd->csd);
		break;

	case CPU_DYING:
	case CPU_DYING_FROZEN:
		/*
		 * The IPIs for the smp-call-function callbacks queued by other
		 * CPUs might arrive late, either due to hardware latencies or
		 * because this CPU disabled interrupts (inside stop-machine)
		 * before the IPIs were sent. So flush out any pending callbacks
		 * explicitly (without waiting for the IPIs to arrive), to
		 * ensure that the outgoing CPU doesn't go offline with work
		 * still pending.
		 */
		flush_smp_call_function_queue(false);
		break;
#endif
	};

	return NOTIFY_OK;
}

static struct notifier_block hotplug_cfd_notifier = {
	.notifier_call		= hotplug_cfd,
};

void __init call_function_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int i;

	for_each_possible_cpu(i)
		init_llist_head(&per_cpu(call_single_queue, i));

	hotplug_cfd(&hotplug_cfd_notifier, CPU_UP_PREPARE, cpu);
	register_cpu_notifier(&hotplug_cfd_notifier);
}

/*
 * csd_lock/csd_unlock used to serialize access to per-cpu csd resources
 *
 * For non-synchronous ipi calls the csd can still be in use by the
 * previous function call. For multi-cpu calls its even more interesting
 * as we'll have to ensure no other cpu is observing our csd.
 */
static void csd_lock_wait(struct call_single_data *csd)
{
	while (smp_load_acquire(&csd->flags) & CSD_FLAG_LOCK)
		cpu_relax();
}

static void csd_lock(struct call_single_data *csd)
{
	csd_lock_wait(csd);
	csd->flags |= CSD_FLAG_LOCK;

	/*
	 * prevent CPU from reordering the above assignment
	 * to ->flags with any subsequent assignments to other
	 * fields of the specified call_single_data structure:
	 */
	smp_wmb();
}

static void csd_unlock(struct call_single_data *csd)
{
	WARN_ON(!(csd->flags & CSD_FLAG_LOCK));

	/*
	 * ensure we're all done before releasing data:
	 */
	smp_store_release(&csd->flags, 0);
}

static DEFINE_PER_CPU_SHARED_ALIGNED(struct call_single_data, csd_data);

/*
 * Insert a previously allocated call_single_data element
 * for execution on the given CPU. data must already have
 * ->func, ->info, and ->flags set.
 */
static int generic_exec_single(int cpu, struct call_single_data *csd,
			       smp_call_func_t func, void *info)
{
	if (cpu == smp_processor_id()) {
		unsigned long flags;

		/*
		 * We can unlock early even for the synchronous on-stack case,
		 * since we're doing this from the same CPU..
		 */
		csd_unlock(csd);
		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
		return 0;
	}


	if ((unsigned)cpu >= nr_cpu_ids || !cpu_online(cpu)) {
		csd_unlock(csd);
		return -ENXIO;
	}

	csd->func = func;
	csd->info = info;

	/*
	 * The list addition should be visible before sending the IPI
	 * handler locks the list to pull the entry off it because of
	 * normal cache coherency rules implied by spinlocks.
	 *
	 * If IPIs can go out of order to the cache coherency protocol
	 * in an architecture, sufficient synchronisation should be added
	 * to arch code to make it appear to obey cache coherency WRT
	 * locking and barrier primitives. Generic code isn't really
	 * equipped to do the right thing...
	 */
	if (llist_add(&csd->llist, &per_cpu(call_single_queue, cpu)))
		arch_send_call_function_single_ipi(cpu);

	return 0;
}

/**
 * generic_smp_call_function_single_interrupt - Execute SMP IPI callbacks
 *
 * Invoked by arch to handle an IPI for call function single.
 * Must be called with interrupts disabled.
 */
void generic_smp_call_function_single_interrupt(void)
{
	flush_smp_call_function_queue(true);
}

/**
 * flush_smp_call_function_queue - Flush pending smp-call-function callbacks
 *
 * @warn_cpu_offline: If set to 'true', warn if callbacks were queued on an
 *		      offline CPU. Skip this check if set to 'false'.
 *
 * Flush any pending smp-call-function callbacks queued on this CPU. This is
 * invoked by the generic IPI handler, as well as by a CPU about to go offline,
 * to ensure that all pending IPI callbacks are run before it goes completely
 * offline.
 *
 * Loop through the call_single_queue and run all the queued callbacks.
 * Must be called with interrupts disabled.
 */
static void flush_smp_call_function_queue(bool warn_cpu_offline)
{
	struct llist_head *head;
	struct llist_node *entry;
	struct call_single_data *csd, *csd_next;
	static bool warned;

	WARN_ON(!irqs_disabled());

	head = this_cpu_ptr(&call_single_queue);
	entry = llist_del_all(head);
	entry = llist_reverse_order(entry);

	/* There shouldn't be any pending callbacks on an offline CPU. */
	if (unlikely(warn_cpu_offline && !cpu_online(smp_processor_id()) &&
		     !warned && !llist_empty(head))) {
		warned = true;
		WARN(1, "IPI on offline CPU %d\n", smp_processor_id());

		/*
		 * We don't have to use the _safe() variant here
		 * because we are not invoking the IPI handlers yet.
		 */
		llist_for_each_entry(csd, entry, llist)
			pr_warn("IPI callback %pS sent to offline CPU\n",
				csd->func);
	}

	llist_for_each_entry_safe(csd, csd_next, entry, llist) {
		smp_call_func_t func = csd->func;
		void *info = csd->info;

		/* Do we wait until *after* callback? */
		if (csd->flags & CSD_FLAG_SYNCHRONOUS) {
			func(info);
			csd_unlock(csd);
		} else {
			csd_unlock(csd);
			func(info);
		}
	}

	/*
	 * Handle irq works queued remotely by irq_work_queue_on().
	 * Smp functions above are typically synchronous so they
	 * better run first since some other CPUs may be busy waiting
	 * for them.
	 */
	irq_work_run();
}

/*
 * smp_call_function_single - Run a function on a specific CPU
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 */
int smp_call_function_single(int cpu, smp_call_func_t func, void *info,
			     int wait)
{
	struct call_single_data *csd;
	struct call_single_data csd_stack = { .flags = CSD_FLAG_LOCK | CSD_FLAG_SYNCHRONOUS };
	int this_cpu;
	int err;

	/*
	 * prevent preemption and reschedule on another processor,
	 * as well as CPU removal
	 */
	this_cpu = get_cpu();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress);

	csd = &csd_stack;
	if (!wait) {
		csd = this_cpu_ptr(&csd_data);
		csd_lock(csd);
	}

	err = generic_exec_single(cpu, csd, func, info);

	if (wait)
		csd_lock_wait(csd);

	put_cpu();

	return err;
}
EXPORT_SYMBOL(smp_call_function_single);

/**
 * smp_call_function_single_async(): Run an asynchronous function on a
 * 			         specific CPU.
 * @cpu: The CPU to run on.
 * @csd: Pre-allocated and setup data structure
 *
 * Like smp_call_function_single(), but the call is asynchonous and
 * can thus be done from contexts with disabled interrupts.
 *
 * The caller passes his own pre-allocated data structure
 * (ie: embedded in an object) and is responsible for synchronizing it
 * such that the IPIs performed on the @csd are strictly serialized.
 *
 * NOTE: Be careful, there is unfortunately no current debugging facility to
 * validate the correctness of this serialization.
 */
int smp_call_function_single_async(int cpu, struct call_single_data *csd)
{
	int err = 0;

	preempt_disable();

	/* We could deadlock if we have to wait here with interrupts disabled! */
	if (WARN_ON_ONCE(csd->flags & CSD_FLAG_LOCK))
		csd_lock_wait(csd);

	csd->flags = CSD_FLAG_LOCK;
	smp_wmb();

	err = generic_exec_single(cpu, csd, csd->func, csd->info);
	preempt_enable();

	return err;
}
EXPORT_SYMBOL_GPL(smp_call_function_single_async);

/*
 * smp_call_function_any - Run a function on any of the given cpus
 * @mask: The mask of cpus it can run on.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait until function has completed.
 *
 * Returns 0 on success, else a negative status code (if no cpus were online).
 *
 * Selection preference:
 *	1) current cpu if in @mask
 *	2) any cpu of current node if in @mask
 *	3) any other online cpu in @mask
 */
int smp_call_function_any(const struct cpumask *mask,
			  smp_call_func_t func, void *info, int wait)
{
	unsigned int cpu;
	const struct cpumask *nodemask;
	int ret;

	/* Try for same CPU (cheapest) */
	cpu = get_cpu();
	if (cpumask_test_cpu(cpu, mask))
		goto call;

	/* Try for same node. */
	nodemask = cpumask_of_node(cpu_to_node(cpu));
	for (cpu = cpumask_first_and(nodemask, mask); cpu < nr_cpu_ids;
	     cpu = cpumask_next_and(cpu, nodemask, mask)) {
		if (cpu_online(cpu))
			goto call;
	}

	/* Any online will do: smp_call_function_single handles nr_cpu_ids. */
	cpu = cpumask_any_and(mask, cpu_online_mask);
call:
	ret = smp_call_function_single(cpu, func, info, wait);
	put_cpu();
	return ret;
}
EXPORT_SYMBOL_GPL(smp_call_function_any);

/**
 * smp_call_function_many(): Run a function on a set of other CPUs.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * If @wait is true, then returns once @func has returned.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler. Preemption
 * must be disabled when calling this function.
 */
void smp_call_function_many(const struct cpumask *mask,
			    smp_call_func_t func, void *info, bool wait)
{
	struct call_function_data *cfd;
	int cpu, next_cpu, this_cpu = smp_processor_id();

	/*
	 * Can deadlock when called with interrupts disabled.
	 * We allow cpu's that are not yet online though, as no one else can
	 * send smp call function interrupt to this cpu and as such deadlocks
	 * can't happen.
	 */
	WARN_ON_ONCE(cpu_online(this_cpu) && irqs_disabled()
		     && !oops_in_progress && !early_boot_irqs_disabled);

	/* Try to fastpath.  So, what's a CPU they want? Ignoring this one. */
	cpu = cpumask_first_and(mask, cpu_online_mask);
	if (cpu == this_cpu)
		cpu = cpumask_next_and(cpu, mask, cpu_online_mask);

	/* No online cpus?  We're done. */
	if (cpu >= nr_cpu_ids)
		return;

	/* Do we have another CPU which isn't us? */
	next_cpu = cpumask_next_and(cpu, mask, cpu_online_mask);
	if (next_cpu == this_cpu)
		next_cpu = cpumask_next_and(next_cpu, mask, cpu_online_mask);

	/* Fastpath: do that cpu by itself. */
	if (next_cpu >= nr_cpu_ids) {
		smp_call_function_single(cpu, func, info, wait);
		return;
	}

	cfd = this_cpu_ptr(&cfd_data);

	cpumask_and(cfd->cpumask, mask, cpu_online_mask);
	cpumask_clear_cpu(this_cpu, cfd->cpumask);

	/* Some callers race with other cpus changing the passed mask */
	if (unlikely(!cpumask_weight(cfd->cpumask)))
		return;

	for_each_cpu(cpu, cfd->cpumask) {
		struct call_single_data *csd = per_cpu_ptr(cfd->csd, cpu);

		csd_lock(csd);
		if (wait)
			csd->flags |= CSD_FLAG_SYNCHRONOUS;
		csd->func = func;
		csd->info = info;
		llist_add(&csd->llist, &per_cpu(call_single_queue, cpu));
	}

	/* Send a message to all CPUs in the map */
	arch_send_call_function_ipi_mask(cfd->cpumask);

	if (wait) {
		for_each_cpu(cpu, cfd->cpumask) {
			struct call_single_data *csd;

			csd = per_cpu_ptr(cfd->csd, cpu);
			csd_lock_wait(csd);
		}
	}
}
EXPORT_SYMBOL(smp_call_function_many);

/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * Returns 0.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(smp_call_func_t func, void *info, int wait)
{
	preempt_disable();
	smp_call_function_many(cpu_online_mask, func, info, wait);
	preempt_enable();

	return 0;
}
EXPORT_SYMBOL(smp_call_function);

/* Setup configured maximum number of CPUs to activate */
unsigned int setup_max_cpus = NR_CPUS;
EXPORT_SYMBOL(setup_max_cpus);

/* to apply underclocking or not */
bool activate_underclocking = false;
EXPORT_SYMBOL(activate_underclocking);

/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */

void __weak arch_disable_smp_support(void) { }

static int __init nosmp(char *str)
{
	setup_max_cpus = 0;
	arch_disable_smp_support();

	return 0;
}

early_param("nosmp", nosmp);

static int __init CPUunderclocking(char *str)
{
	activate_underclocking = true;
	return 0;
}
early_param("CPUunderclocking", CPUunderclocking);
//redefinging because this patch is supposed to be independend of the EHCI-fix one.
#define _prik2(fmt, a...) printk("CPUunderclocking: " fmt " {%s %s:%i}", ##a, __func__, __FILE__, __LINE__ )
//KERN_* from: include/linux/kern_levels.h
#define printka(fmt, a...) _prik2(KERN_ALERT fmt, ##a)
#define printkw(fmt, a...) _prik2(KERN_WARNING fmt, ##a)
#define printkn(fmt, a...) _prik2(KERN_NOTICE fmt, ##a)
#define printki(fmt, a...) _prik2(KERN_INFO fmt, ##a)
#define printkd(fmt, a...) _prik2(KERN_DEBUG fmt, ##a)
#define BUGIFNOT(a) BUG_ON(!(a))

// special divisors for family 0x12 (aka 18 in decimal)
static const double DIVISORS_12[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 0.0 };
//XXX: that 1.5 is needed for the 14.0 multi for example. (5+16)/1.5 = 14

//top voltage, fixed value used for calculating VIDs and stuff, do not change!!!
#define V155 1.55
#define V1325 1.325 //my original turbo state voltage!

#define DEFAULTREFERENCECLOCK 100 //MHz
#define REFERENCECLOCK DEFAULTREFERENCECLOCK //for my CPU, is the same 100MHz (unused)

#define NUMPSTATES 8
#define NUMCPUCORES 4 //4 cores, for my CPU
#define CPUFAMILY 0x12
#define CPUMODEL 0x1
#define CPUMINMULTI 1.0
#define CPUMAXMULTI 40.0 //24+16
#define CPUVIDSTEP 0.0125 //fixed; default step for pre SVI2 platforms

#define CPUMINVID 88 //1.55 - 88*0.0125 = 0.450
#define CPUMINVOLTAGE 0.4500

#define CPUMAXVID 18 //1.55 - 18*0.0125 = 1.325; multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked
#define CPUMAXVOLTAGE 1.3250 //multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked

#define CPUMAXVIDunderclocked 37 //1.0875V fid:6 did:0 multi:22.00 vid:37
#define CPUMAXVOLTAGEunderclocked 1.0875 //1.55 - 37*0.0125 = 1.0875; fid:6 did:0 multi:22.00 vid:37

#define CPUMINMULTIunderclocked 8 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
#define CPUMAXMULTIunderclocked 22 //1.0875V fid:6 did:0 multi:22.00 vid:37

#define CPUMINVIDunderclocked 67 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
#define CPUMINVOLTAGEunderclocked 0.7125 //1.55 - 67*0.0125 = .7125

struct PStateInfo {
  u32 fid;//frequency identifier (was deduced from multi)
  u32 did;//divisor index (was deduced from multi)
//  double multi; //multiplier ( multiply with the reference clock of 100Mhz eg. multi*REFERENCECLOCK)
  u32 multi;
  double strvid; //real life voltage eg. 1.325V as a double
  int VID; //vid, eg. 18 (for 1.325V) or 67 (for 1.0875V)
  u32 regIndex;
  u32 datahi;
  u32 datalo;
};

const struct PStateInfo bootdefaults_psi[NUMPSTATES]={//XXX: fyi only, do not use this!
  {30, 2, 23.0, 1.3250, 18, 0xc0010064, 0x8000017d, 0x000025e2}, //P0, boost
  {26, 3, 14.0, 1.0625, 39, 0xc0010065, 0x80000140, 0x00004fa3}, //P1, normal
  {23, 3, 13.0, 1.0250, 42, 0xc0010066, 0x80000137, 0x00005573},
  {20, 3, 12.0, 0.9875, 45, 0xc0010067, 0x80000132, 0x00005b43},
  {17, 3, 11.0, 0.9750, 46, 0xc0010068, 0x8000012e, 0x00005d13},
  {14, 3, 10.0, 0.9625, 47, 0xc0010069, 0x8000012b, 0x00005ee3},
  {11, 3,  9.0, 0.9500, 48, 0xc001006a, 0x80000127, 0x000060b3},
  {16, 4,  8.0, 0.9250, 50, 0xc001006b, 0x80000125, 0x00006504}  //P7, normal
};
//bootdefaults_psi;//prevent -Wunused-variable warning; nvm, got statement has no effect  warning. What I actually need is:  __attribute__((unused))  src: https://stackoverflow.com/questions/15053776/how-do-you-disable-the-unused-variable-warnings-coming-out-of-gcc
const struct PStateInfo allpsi[NUMPSTATES]={//stable underclocking for my CPU:
  {6, 0, 22.0, 1.0875, 37, 0xc0010064, 0x8000017d, 0x00004a60}, //P0, boost
  {4, 0, 20.0, 1.0250, 42, 0xc0010065, 0x80000140, 0x00005440}, //P1, normal
  {2, 0, 18.0, 0.9625, 47, 0xc0010066, 0x80000137, 0x00005e20},
  {1, 0, 17.0, 0.9375, 49, 0xc0010067, 0x80000132, 0x00006210},
  {0, 0, 16.0, 0.9000, 52, 0xc0010068, 0x8000012e, 0x00006800},
  {5, 1, 14.0, 0.8625, 55, 0xc0010069, 0x8000012b, 0x00006e51},
  {2, 1, 12.0, 0.8125, 59, 0xc001006a, 0x80000127, 0x00007621},
  {0, 2,  8.0, 0.7125, 67, 0xc001006b, 0x80000125, 0x00008602}  //P7, normal
};

static u32 __init GetBits(u64 value, unsigned char offset, unsigned char numBits) {
    const u64 mask = (((u64)1 << numBits) - (u64)1); // 2^numBits - 1; after right-shift
    return (u32)((value >> offset) & mask);
}

static u64 __init SetBits(u64 value, u32 bits, unsigned char offset, unsigned char numBits) {
    const u64 mask = (((u64)1 << numBits) - (u64)1) << offset; // 2^numBits - 1, shifted by offset to the left
    value = (value & ~mask) | (((u64)bits << offset) & mask);
    return value;
}


static int __init Wrmsr(const u32 regIndex, const u64 value) {
//  const u64 value=*ref2value;
  const u32 data_lo=(u32)(value & 0xFFFFFFFF);
  const u32 data_hi=(u32)(value >> 32);
  int firsterr = 0;
  int err;
  int tmp;
  bool safe=false;
  u32 cpucore;
  if (setup_max_cpus < NUMCPUCORES) {
    printka("Unexpected number of cores. Expected: NUMCPUCORES=%d. Found: setup_max_cpus=%d. Continuing.", NUMCPUCORES, setup_max_cpus);
  }
  for (cpucore = 0; cpucore < NUMCPUCORES; cpucore++) {
    printkd("  !! Wrmsr: core:%d/%d idx:%x valx:%08x%08x... ",
        cpucore, setup_max_cpus,
        regIndex,
        data_hi,
        data_lo);
    //safety check:
//    BUG_ON(allpsi[);
    safe=false;
    for (tmp=0; 0 < NUMPSTATES; tmp++) {
      if (
          ((allpsi[tmp].regIndex == regIndex) && (allpsi[tmp].datahi == data_hi) && (allpsi[tmp].datalo == data_lo))
          ((bootdefaults_psi[tmp].regIndex == regIndex) && (bootdefaults_psi[tmp].datahi == data_hi) && (bootdefaults_psi[tmp].datalo == data_lo))
          ||
          ( (0xc0010062 == regIndex) && (0 == data_hi) && (data_lo>=0) && (data_lo <=6) ) //change current pstate = allowed
         )
      {
        safe=true;
        break;
      }
    }
    if (!safe) {
      printka("safetyfail\n",
          "    !! Wrmsr: not allowing msr write with those values. (this was meant to indicate a bug in code)\n");
      return 1;
    }
    BUGIFNOT(safe);
    err = wrmsr_safe_on_cpu(cpucore, regIndex, data_lo, data_hi);
    if (err) {
      printka("failed.\n"
            "    !! Wrmsr: failed, err:%d on core:%d/%d idx:%x valx:%08x%08x. Continuing.\n",
          err,
          cpucore, setup_max_cpus,
          regIndex,
          data_hi,
          data_lo);
      //break;
      if (!firsterr) {
        firsterr=err;
      }
    }else{
      printkd("done.\n");
    }
  }
  return firsterr;//0 on success
}

static u64 __init Rdmsr(const u32 regIndex) {
  u64 result[NUMCPUCORES]={0,0,0,0};
  int firsterr = 0;
  int err;
  u32 data_lo;
  u32 data_hi;
  int cpucore;//for the 'for'
  if (setup_max_cpus < NUMCPUCORES) {
    printka("Unexpected number of cores. Expected: NUMCPUCORES=%d. Found: setup_max_cpus=%d. Continuing.", NUMCPUCORES, setup_max_cpus);
  }

  for (cpucore = 0; cpucore < NUMCPUCORES; cpucore++) {
    printkd("  !! Rdmsr: core:%d idx:%x ... %lu bytes ... ", cpucore, regIndex, sizeof(result[cpucore]));//8 bytes
    data_lo=0;//not needed to init them, but just in case there's an err, they shouldn't be equal to prev core's values!
    data_hi=0;
    err = rdmsr_safe_on_cpu(cpucore, regIndex, &data_lo, &data_hi);
    if (err) {
      printka("failed.\n"
            "    !! Rdmsr: failed, err:%d on core:%d/%d idx:%x resultx:%08x%08x. Continuing.\n",
          err,
          cpucore, setup_max_cpus,
          regIndex,
          data_hi,
          data_lo);
      if (!firsterr) {
        firsterr=err;
      }
    }else {
      result[cpucore]=(u64)( ((u64)data_hi << 32) + data_lo);
      printkd("done. (resultx==%08x%08x)\n", (u32)(result[cpucore] >> 32), (u32)(result[cpucore] & 0xFFFFFFFF));//just in case unsigned int gets more than 32 bits for wtw reason in the future! leave the & there.
      if (cpucore>0) {
        if (result[cpucore-1] != result[cpucore]) {
          printkw("    !! Rdmsr: different results for cores(this is expected to be so, depending on load)\n");
          printkw("    !! core[%d]  != core[%d]\n", cpucore-1, cpucore);
        }
      }
    }
  }

  if (firsterr) {
    return 0;//return 0 on error
  } else {//return the actual value(for core0) on success
    return result[0];//return only the core0 result
  }
}

/*void __init multifromfidndid(double *dest_multi, const int fid, const int did) {//kernel/smp.c:702:1: error: SSE register return with SSE disabled   (that's due to the use of type:  double)

  double multi= (fid + 16) / DIVISORS_12[did];

  BUG_ON(NULL == dest_multi);

  if ((multi < CPUMINMULTI) || (multi > CPUMAXMULTI)) {
    printka("!! unexpected multiplier, you're probably running inside virtualbox fid:%d  did:%d multi:%f",
      fid, did, multi);
  }
  BUGIFNOT(multi>=CPUMINMULTI);
  BUGIFNOT(multi<=CPUMAXMULTI);
//  return multi;
  *dest_multi=multi;
}

static void __init FindFraction(double value, const double* divisors,
    int *numerator, int *divisorIndex,
    const int minNumerator, const int maxNumerator) {
  // limitations: non-negative value and divisors

  int i;
  double bestValue;
  // count the null-terminated and ascendingly ordered divisors
  int numDivisors = 0;

  BUG_ON(NULL == divisors);
  BUG_ON(NULL == numerator);
  BUG_ON(NULL == divisorIndex);

  for (; divisors[numDivisors] > 0; numDivisors++) { }

  // make sure the value is in a valid range
  value = max(minNumerator / divisors[numDivisors-1], min(maxNumerator / divisors[0], value));

  // search the best-matching combo
  bestValue = -1.0; // numerator / divisors[divisorIndex]
  for (i = 0; i < numDivisors; i++) {
    const double d = divisors[i];
    const int n = max(minNumerator, min(maxNumerator, (int)(value * d)));
    const double myValue = n / d;

    if (myValue <= value && myValue > bestValue) {
      *numerator = n;
      *divisorIndex = i;
      bestValue = myValue;

      if (bestValue == value)
        break;
    }
  }
}

inline void __init multi2fidndid(const double multi, int *fid, int *did) {

  const int minNumerator = 16; // numerator: 0x10 = 16 as fixed offset
  const int maxNumerator = 31 + minNumerator; // 5 bits => max 2^5-1 = 31

  int numerator, divisorIndex;

  BUG_ON( NULL == fid);
  BUG_ON( NULL == did);
  BUGIFNOT(multi>=CPUMINMULTI);
  BUGIFNOT(multi<CPUMAXMULTI);

  FindFraction(multi, DIVISORS_12, &numerator, &divisorIndex, minNumerator, maxNumerator);

  *fid = numerator - minNumerator;
  *did = divisorIndex;
}*/


static bool __init WritePState(const u32 numpstate, const struct PStateInfo *info) {//FIXME: so what if I'm passing the entire struct? or should I leave it as pointer? but don't want struct's contents modified!
  /*const*/ u32 regIndex;//kernel/smp.c:780:3: warning: ISO C90 forbids mixed declarations and code [-Wdeclaration-after-statement]
  u64 msr;
  int fidbefore;
  int didbefore;
//  double Multi;
  int VID;
  int fid, did;

  BUG_ON(NULL == info);
  BUGIFNOT(numpstate >=0);//done: BUG_ON is the reverse of assert!!
  BUGIFNOT(numpstate < NUMPSTATES);

  regIndex = 0xc0010064 + numpstate;//kernel/smp.c:788:12: error: assignment of read-only variable ‘regIndex’
  //XXX: see? that's what ISO C90 does, prevents me from const-ifying a var if I've to do an assert/BUG_ON before assigning its initial value! AND makes me put vars at top which I should otherwise not have access to until later on in code, thus allowing me the opportunity to typo or misused a different but similarly names var and thus introduce a bug.
  msr = Rdmsr(regIndex);

  fidbefore = GetBits(msr, 4, 5);
  didbefore = GetBits(msr, 0, 4);
//  multifromfidndid(&Multi, fidbefore, didbefore); FIXME: find a way to make double work, or do fixed-point arithmethic(fpa) somehow
  VID = GetBits(msr, 9, 7);

//  printkd("!! Write PState(1of2) read : fid:%d did:%d vid:%d Multi:%f\n", fidbefore, didbefore, VID, Multi);
  printkd("!! Write(1of2) PState%d read : fid:%d did:%d vid:%d Multi:N/A\n", 
      numpstate, fidbefore, didbefore, VID);//FIXME:

  BUGIFNOT(info->multi >= CPUMINMULTIunderclocked);//FIXME: smp.c:809: undefined reference to `__gedf2'  due to double no doubt!
  BUGIFNOT(info->multi <= CPUMAXMULTIunderclocked);

  //multi2fidndid(info->multi, &fid, &did);//FIXME:
  fid=info->fid;
  did=info->did;

  if ((fid != fidbefore) || (did != didbefore)) {
    msr=SetBits(msr, fid, 4, 5);
    msr=SetBits(msr, did, 0, 4);

    BUGIFNOT(info->VID >= CPUMAXVIDunderclocked);
    BUGIFNOT(info->VID <= CPUMINVIDunderclocked);
    msr=SetBits(msr, info->VID, 9, 7);

    printkd("!! Write(2of2) PState%d write:%d did:%d vid:%d "//"(multi:%02.2f)"
        "(multi:%d)"
        " ... "
        ,numpstate, fid, did, info->VID, info->multi);
    Wrmsr(regIndex, msr);
    printkd("done.\n");
    return true;
  } else {
    printkd("!! Write(2of2) PState%d no write needed: same values. Done.\n", numpstate);
    return false;
  }
}

int __init GetCurrentPState(void) {
  const u64 msr = Rdmsr(0xc0010071);
  const int i = GetBits(msr, 16, 3);//0..7
  return i;
}

void __init SetCurrentPState(int numpstate) {
  u32 regIndex = 0xc0010062;
  u64 msr;
  int i=-1;
  int j=-1;

  BUG_ON(numpstate < 0);
  BUG_ON(numpstate >= NUMPSTATES);
//    throw ExceptionWithMessage("P-state index out of range");

  // //so excluding the turbo state, however! isn't P0 the turbo state? unless this means that pstates here start from 0 to 7  and represent P7 to P0 in this order! (need to FIXME: verify this!) nope, P0 is turbo state here too, confirmed by http://review.coreboot.org/gitweb?p=coreboot.git;a=commitdiff;h=363010694dba5b5c9132e78be357a1098bdc0aba which says "/* All cores: set pstate 0 (1600 MHz) early to save a few ms of boot time */"
  //ok so I got it: https://github.com/johkra/amdmsrtweaker-lnx/commit/11a4fe2f486a6686bd5e64bc0e6859145a890ef2#commitcomment-13245640
  //decrease the turbo state(s) because index=0 is P1 ... and there is no way to select turbo state! (I may still be wrong, but this explaination makes sense why this was originally coded this way)
  numpstate -= 1;//NumBoostStates;//XXX: no idea why decrease by 1 here then.
  if (numpstate < 0)
    numpstate = 0;

  msr = Rdmsr(regIndex);
  msr=SetBits(msr, numpstate, 0, 3);
  Wrmsr(regIndex, msr);

  //Next, wait for the new pstate to be set, code from: https://chromium.googlesource.com/chromiumos/third_party/coreboot/+/c02b4fc9db3c3c1e263027382697b566127f66bb/src/cpu/amd/model_10xxx/fidvid.c line 367
  regIndex=0xC0010063;
  do {
    msr = Rdmsr(regIndex);
    i = GetBits(msr, 0, 16);
    j = GetBits(msr, 0, 64);
    printkd("i(16bits)=%d j(64bits)=%d wantedpstate=%d\n",i,j,numpstate);//gets to only be printed once, because it's already set by the time this gets reached, apparently.
  } while (i != numpstate);
}



static void __init apply_underclocking_if_needed(void)
{
  bool modded=false;
  u32 i;
  int currentPState;
  int lastpstate;
  int tempPState;

  printki("activating(?)=%d\n", activate_underclocking);
  if (activate_underclocking) {
    printki("activating...");
    for (i = 0; i < NUMPSTATES; i++) {
      modded=WritePState(i, &allpsi[i]) | modded;
    }

    if (modded) {
      printkd("Switching to another p-state temporarily so to ensure that the current one uses newly applied values\n");

      currentPState = GetCurrentPState();

      //we switch to another pstate temporarily, then back again so that it takes effect (apparently that's why, unsure, it's not my coding)
      lastpstate= NUMPSTATES - 1;//aka the lowest speed one
      tempPState = (currentPState == lastpstate ? 0 : lastpstate);
      //    const int tempPState = ((currentPState + 1) % NUMPSTATES);//some cores may already be at current+1 pstate; so don't use this variant
      printkd("!! currentpstate:%d temppstate:%d\n", currentPState, tempPState);
      SetCurrentPState(tempPState);
      printkd("!! currentpstate:%d\n", GetCurrentPState());
      SetCurrentPState(currentPState);
      printkd("!! currentpstate:%d\n", GetCurrentPState());
    }
  }
  printki("done.\n");
}

/* this is hard limit */
static int __init nrcpus(char *str)
{
	int nr_cpus;

	get_option(&str, &nr_cpus);
	if (nr_cpus > 0 && nr_cpus < nr_cpu_ids)
		nr_cpu_ids = nr_cpus;

	return 0;
}

early_param("nr_cpus", nrcpus);

static int __init maxcpus(char *str)
{
	get_option(&str, &setup_max_cpus);
	if (setup_max_cpus == 0)
		arch_disable_smp_support();

	return 0;
}

early_param("maxcpus", maxcpus);

/* Setup number of possible processor ids */
int nr_cpu_ids __read_mostly = NR_CPUS;
EXPORT_SYMBOL(nr_cpu_ids);

/* An arch may set nr_cpu_ids earlier if needed, so this would be redundant */
void __init setup_nr_cpu_ids(void)
{
	nr_cpu_ids = find_last_bit(cpumask_bits(cpu_possible_mask),NR_CPUS) + 1;
}

void __weak smp_announce(void)
{
	printk(KERN_INFO "Brought up %d CPUs\n", num_online_cpus());
}

//-------------------------------

#define _prik(fmt, a...) printk(fmt " {%s %s:%i}\n", ##a, __func__, __FILE__, __LINE__ )
//KERN_* from: include/linux/kern_levels.h
#define prika(fmt, a...) _prik(KERN_ALERT fmt, ##a)
#define prikw(fmt, a...) _prik(KERN_WARNING fmt, ##a)
#define prikn(fmt, a...) _prik(KERN_NOTICE fmt, ##a)
#define priki(fmt, a...) _prik(KERN_INFO fmt, ##a)
#define prikd(fmt, a...) _prik(KERN_DEBUG fmt, ##a)
//#define priknoln(fmt, a...) printk(fmt " {%s %s:%i}", ##a, __func__, __FILE__, __LINE__ )

/* Local version of HC_LENGTH macro as ehci struct is not available here */
#define EARLY_HC_LENGTH(p)  (0x00ff & (p)) /* bits 7 : 0 */                                                   


static struct ehci_caps __iomem *ehci_caps;
static struct ehci_regs __iomem *ehci_regs; // include/linux/usb/ehci_def.h
static u32 n_ports=0;

static void ehci_status(char *str) {
  u32 portsc;
  int i;

  if ((NULL == str) || (str[0] != '\0')) {
    prikd("%s",str);
  }
  prikd("  ehci cmd     : %08X", readl(&ehci_regs->command));
  prikd("  ehci conf flg: %08X", readl(&ehci_regs->configured_flag));
  prikd("  ehci status  : %08X", readl(&ehci_regs->status));
  for (i = 1; i <= n_ports; i++) {
    portsc = readl(&ehci_regs->port_status[i-1]);
    prikd("port %d status %08X", i, portsc);
  }
}

/* The code in early_ehci_bios_handoff() is derived from the usb pci
 * quirk initialization, but altered so as to use the early PCI
 * routines. */
#define EHCI_USBLEGSUP_BIOS (1 << 16) /* BIOS semaphore */
#define EHCI_USBLEGCTLSTS 4   /* legacy control/status */
static int __init __attribute__((unused)) early_ehci_bios_handoff(u32 bus, u32 slot, u32 func)
{
  u32 hcc_params = readl(&ehci_caps->hcc_params);
  int offset = (hcc_params >> 8) & 0xff;
  u32 cap;
  int msec;

  prikd("start");

  if (!offset) {
    prika("offset is 0");
    return 0;
  } else {
    prikd("offset is %i", offset);
  }

  cap = read_pci_config(bus, slot, func, offset);
  prikd("EHCI BIOS state (hex~dec) %08X~%u", cap, cap);

  if ((cap & 0xff) == 1 && (cap & EHCI_USBLEGSUP_BIOS)) {
    prikn("BIOS handoff");//not reached now, but was reached with dbgp
    write_pci_config_byte(bus, slot, func, offset + 3, 1);
  }

  /* if boot firmware now owns EHCI, spin till it hands it over. */
  msec = 1000;
  while ((cap & EHCI_USBLEGSUP_BIOS) && (msec > 0)) {
    mdelay(10);
    msec -= 10;
    cap = read_pci_config(bus, slot,
        func, offset);
    //%08X is 8 (hex)nibbles = 32bits
    prikd("EHCI BIOS state (hex~dec) %08X~%u", cap, cap);
  }

  if (cap & EHCI_USBLEGSUP_BIOS) {
    /* well, possibly buggy BIOS... try to shut it down, 
     * and hope nothing goes too wrong */
    prika("BIOS handoff failed: %08X~%u", cap, cap);
    write_pci_config_byte(bus, slot, func, offset + 2, 0);
  }

  /* just in case, always disable EHCI SMIs */
  //this doesn't seem to have any effect, in my test case
  write_pci_config_byte(bus, slot, func,
      offset + EHCI_USBLEGCTLSTS, 0);
  prikd("disabled EHCI SMIs");

  prikd("stop");
  return 1; //good
}


static int __attribute__((unused)) ehci_startup(void)
{
  u32 cmd, status;
  int loop;

  ehci_status("EHCI startup");
  //each port has only (PORT_OWNER | PORT_POWER) aka 0x00003000  for my case
  //controller is 00001000 aka STS_HALT
  //command is 0x00080B00 aka CMD_PARK | CMD_PARK_CNT(3) | r/w intr rate in microframes, equal to the default:(1<<19 == 8 ~ aka 1/msec)

  /* Start the ehci running */
  cmd = readl(&ehci_regs->command);
  cmd &= ~(CMD_LRESET | CMD_IAAD | CMD_PSE | CMD_ASE | CMD_RESET);
//  cmd |= CMD_RUN; //looks like this is optional
  cmd &= ~CMD_RUN; //thus it works with this too (tested!)
  writel(cmd, &ehci_regs->command);

  /* Ensure everything is routed to the EHCI */
  writel(FLAG_CF, &ehci_regs->configured_flag);//this is the ONLY needed thing!

  /* Wait until the controller is no longer halted */
  loop = 1000;
  do {
    status = readl(&ehci_regs->status);
    if (!(status & STS_HALT))
      break;
    udelay(1);
  } while (--loop > 0);

  if (!loop) {
    prika("EHCI can not be started");
    return 0;//bad
  }
  ehci_status("EHCI started");
  //for my case:
  //by now, each port that has nothing connected to it has only (PORT_POWER) aka 0x00001000
  //except the ports (port 3) that have something connected to it, those are:
  // 0x00001803 aka PORT_POWER | USB2.0(?) | PORT_CSC | PORT_CONNECT
  // EHCI controller status is 00000000 (not nolonger STS_HALT )
  // command is now: 00080B01 the extra CMD_RUN is set
  return 1;//good
}

static int dbgp_ehci_controller_reset(void)
{
  int loop = 250 * 1000;
  u32 cmd;

  /* Reset the EHCI controller */
  cmd = readl(&ehci_regs->command);
  cmd |= CMD_RESET;
  writel(cmd, &ehci_regs->command);
  do {
    cmd = readl(&ehci_regs->command);
  } while ((cmd & CMD_RESET) && (--loop > 0));

  if (!loop) {
    prika("can not reset ehci");
    return 0;
  }
  ehci_status("ehci reset done");
  return 1;
}

static int __init __attribute__((unused)) ehci_setup222(void)
{
  prikd("started");

  /* Only reset the controller if it is not already in the
   * configured state */
  if (!(readl(&ehci_regs->configured_flag) & FLAG_CF)) {
    prikd("controller reset is next:");
    if (!dbgp_ehci_controller_reset()) {//OPTIONAL apparently
      prika("failed EHCI controller reset (shouldn't happen!)");
      return 0;
    }
  } else {
    ehci_status("ehci skip - already configured");
  }

  ehci_status("before setting FLAG_CF");
  /* Ensure everything is routed to the EHCI */
  writel(FLAG_CF, &ehci_regs->configured_flag);
  //ehci_startup(); // NEEDED! because FLAG_CF was inside it, now is out ^ above
  ehci_status("after setting FLAG_CF");

  prikd("ended");
  return 1;
}

static u32 __init isEHCI(u32 bus, u32 slot, u32 func)
{
  u32 class;

  class = read_pci_config(bus, slot, func, PCI_CLASS_REVISION);
  if ((class >> 8) != PCI_CLASS_SERIAL_USB_EHCI) {
    return 0; //not usb ehci
  } else {
    return 1; //good
  }
}


int __init kline(u32 bus, u32 slot, u32 func) 
{
  void __iomem *ehci_bar;
  u32 bar_val;
  u8 byte;
  u32 hcs_params;

  if (!isEHCI(bus, slot, func)) {
    prika("There is no EHCI controller at passed params: %02x:%02x.%1x", bus, slot, func);
    return 0;//bad
  }

  prikd("Found EHCI on %02x:%02x.%1x", bus, slot, func);

  bar_val = read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_0);
  prikd("bar_val: %02x", bar_val);
  if (bar_val & ~PCI_BASE_ADDRESS_MEM_MASK) {
    prika("only simple 32bit mmio bars supported");
    return 0;//bad
  }

  /* double check if the mem space is enabled */
  byte = read_pci_config_byte(bus, slot, func, 0x04);
  if (!(byte & 0x2)) {
    byte  |= 0x02;
    write_pci_config_byte(bus, slot, func, 0x04, byte);
    prikw("mmio for ehci enabled");
  }

  /*
   * FIXME I don't have the bar size so just guess that
   * PAGE_SIZE (aka 4096 for me: sudo getconf PAGE_SIZE)
   * is more than enough.  1K is the biggest I have seen.
   */
  set_fixmap_nocache(FIX_SMPEHCI_BASE, bar_val & PAGE_MASK);
  ehci_bar = (void __iomem *)__fix_to_virt(FIX_SMPEHCI_BASE);
  ehci_bar += bar_val & ~PAGE_MASK;
  prikd("ehci_bar: %p", ehci_bar);

  ehci_caps  = ehci_bar;
  ehci_regs  = ehci_bar + EARLY_HC_LENGTH(readl(&ehci_caps->hc_capbase));

  hcs_params = readl(&ehci_caps->hcs_params);
  n_ports    = HCS_N_PORTS(hcs_params);
  prikd("n_ports:    %d", n_ports);

  //ehci_status("before bios_handoff");

  //early_ehci_bios_handoff(bus, slot, func);//looks like not needed

  //ehci_status("before ehci_setup");


//when BIOS USB Legacy is Disabled then ehci status is STS_PCD | STS_FLR | STS_HALT
//and remains this way because FLAG_CF was already set when USB Legacy is Disabled.
//otherwise, when USB Legacy is Enabled, it's only STS_HALT, and FLAG_CF isn't set
//and it will also crash APs after INIT is sent if mouse is moved.

  if (! (readl(&ehci_regs->configured_flag) & FLAG_CF) ) { //if FLAG_CF not set
    prikd("BIOS USB Legacy is likely Enabled.");
    ehci_status("before setting FLAG_CF");
    //apply the workaround which prevents mouse movements from getting APs stuck when INIT
    /* Ensure everything is routed to the EHCI */
    writel(FLAG_CF, &ehci_regs->configured_flag);
    ehci_status("after setting FLAG_CF");
  } else {
    ehci_status("FLAG_CF was already set which likely means BIOS USB Legacy is Disabled.");
  }

  //ehci_setup222();//part of this is needed; only the FLAG_CF part was needed!

//  ehci_status("after ehci_setup");

  ehci_regs = NULL;
  ehci_caps = NULL;
  n_ports = 0;

  prikd("ended");
  return 1; //good
}

int __init early_reset_EHCIs(void)
{ //all this code is ripped from: drivers/usb/early/ehci-dbgp.c
  u32 bus, slot, func;
  u32 failed=0;

  prikd("started");

  if (!early_pci_allowed()) {
    prika("early_pci_ not allowed!");
    return 0;//bad
  } else {
    prikd("early_pci_allowed");
  }

  //find all (2)EHCI controllers and reset the bejesus out of them and their (5)usb ports(each)
  for (bus = 0; bus < 256; bus++) {
    for (slot = 0; slot < 32; slot++) {
      for (func = 0; func < 8; func++) {
        if (isEHCI(bus,slot,func) && !kline(bus,slot,func)) {
          failed++;
        }
      }
    }
  }
  prikd("ended, failed %u times", failed);
  return 1;//good
}


//-------------------------------

/* Called by boot processor to activate the rest. */
void __init smp_init(void)
{
	unsigned int cpu;

	idle_threads_init();

//-------------------------------
  //reset EHCI controllers and their USB ports before bringing up the rest of the CPUs (APs) to avoid "Not responding" when there's USB activity, such as moving the mouse, right after sending INIT to the AP
  if (!early_reset_EHCIs()) {
    prika("failed to reset EHCIs, you may get CPUx Not responding when e.g. you're moving your mouse, under certain BIOSes. The other workaround is to disable USB Legacy in BIOS.");
  }else{
    priki("All EHCI controllers were reset.");
  }
//-------------------------------

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(cpu) {
		if (num_online_cpus() >= setup_max_cpus)
			break;
		if (!cpu_online(cpu))
			cpu_up(cpu);
	}

	/* Any cleanup work */
	smp_announce();
  apply_underclocking_if_needed();
	smp_cpus_done(setup_max_cpus);
}

/*
 * Call a function on all processors.  May be used during early boot while
 * early_boot_irqs_disabled is set.  Use local_irq_save/restore() instead
 * of local_irq_disable/enable().
 */
int on_each_cpu(void (*func) (void *info), void *info, int wait)
{
	unsigned long flags;
	int ret = 0;

	preempt_disable();
	ret = smp_call_function(func, info, wait);
	local_irq_save(flags);
	func(info);
	local_irq_restore(flags);
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL(on_each_cpu);

/**
 * on_each_cpu_mask(): Run a function on processors specified by
 * cpumask, which may include the local processor.
 * @mask: The set of cpus to run on (only runs on online subset).
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed
 *        on other CPUs.
 *
 * If @wait is true, then returns once @func has returned.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.  The
 * exception is that it may be used during early boot while
 * early_boot_irqs_disabled is set.
 */
void on_each_cpu_mask(const struct cpumask *mask, smp_call_func_t func,
			void *info, bool wait)
{
	int cpu = get_cpu();

	smp_call_function_many(mask, func, info, wait);
	if (cpumask_test_cpu(cpu, mask)) {
		unsigned long flags;
		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
	}
	put_cpu();
}
EXPORT_SYMBOL(on_each_cpu_mask);

/*
 * on_each_cpu_cond(): Call a function on each processor for which
 * the supplied function cond_func returns true, optionally waiting
 * for all the required CPUs to finish. This may include the local
 * processor.
 * @cond_func:	A callback function that is passed a cpu id and
 *		the the info parameter. The function is called
 *		with preemption disabled. The function should
 *		return a blooean value indicating whether to IPI
 *		the specified CPU.
 * @func:	The function to run on all applicable CPUs.
 *		This must be fast and non-blocking.
 * @info:	An arbitrary pointer to pass to both functions.
 * @wait:	If true, wait (atomically) until function has
 *		completed on other CPUs.
 * @gfp_flags:	GFP flags to use when allocating the cpumask
 *		used internally by the function.
 *
 * The function might sleep if the GFP flags indicates a non
 * atomic allocation is allowed.
 *
 * Preemption is disabled to protect against CPUs going offline but not online.
 * CPUs going online during the call will not be seen or sent an IPI.
 *
 * You must not call this function with disabled interrupts or
 * from a hardware interrupt handler or from a bottom half handler.
 */
void on_each_cpu_cond(bool (*cond_func)(int cpu, void *info),
			smp_call_func_t func, void *info, bool wait,
			gfp_t gfp_flags)
{
	cpumask_var_t cpus;
	int cpu, ret;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	if (likely(zalloc_cpumask_var(&cpus, (gfp_flags|__GFP_NOWARN)))) {
		preempt_disable();
		for_each_online_cpu(cpu)
			if (cond_func(cpu, info))
				cpumask_set_cpu(cpu, cpus);
		on_each_cpu_mask(cpus, func, info, wait);
		preempt_enable();
		free_cpumask_var(cpus);
	} else {
		/*
		 * No free cpumask, bother. No matter, we'll
		 * just have to IPI them one by one.
		 */
		preempt_disable();
		for_each_online_cpu(cpu)
			if (cond_func(cpu, info)) {
				ret = smp_call_function_single(cpu, func,
								info, wait);
				WARN_ON_ONCE(ret);
			}
		preempt_enable();
	}
}
EXPORT_SYMBOL(on_each_cpu_cond);

static void do_nothing(void *unused)
{
}

/**
 * kick_all_cpus_sync - Force all cpus out of idle
 *
 * Used to synchronize the update of pm_idle function pointer. It's
 * called after the pointer is updated and returns after the dummy
 * callback function has been executed on all cpus. The execution of
 * the function can only happen on the remote cpus after they have
 * left the idle function which had been called via pm_idle function
 * pointer. So it's guaranteed that nothing uses the previous pointer
 * anymore.
 */
void kick_all_cpus_sync(void)
{
	/* Make sure the change is visible before we kick the cpus */
	smp_mb();
	smp_call_function(do_nothing, NULL, 1);
}
EXPORT_SYMBOL_GPL(kick_all_cpus_sync);

/**
 * wake_up_all_idle_cpus - break all cpus out of idle
 * wake_up_all_idle_cpus try to break all cpus which is in idle state even
 * including idle polling cpus, for non-idle cpus, we will do nothing
 * for them.
 */
void wake_up_all_idle_cpus(void)
{
	int cpu;

	preempt_disable();
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;

		wake_up_if_idle(cpu);
	}
	preempt_enable();
}
EXPORT_SYMBOL_GPL(wake_up_all_idle_cpus);
