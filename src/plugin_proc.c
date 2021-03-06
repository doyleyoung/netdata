#include "common.h"

static struct proc_module {
    const char *name;
    const char *dim;

    int enabled;

    int (*func)(int update_every, usec_t dt);
    usec_t last_run_usec;
    usec_t duration;

    RRDDIM *rd;

} proc_modules[] = {

        // system metrics
        { .name = "/proc/stat", .dim = "stat", .func = do_proc_stat },
        { .name = "/proc/uptime", .dim = "uptime", .func = do_proc_uptime },
        { .name = "/proc/loadavg", .dim = "loadavg", .func = do_proc_loadavg },
        { .name = "/proc/sys/kernel/random/entropy_avail", .dim = "entropy", .func = do_proc_sys_kernel_random_entropy_avail },

        // CPU metrics
        { .name = "/proc/interrupts", .dim = "interrupts", .func = do_proc_interrupts },
        { .name = "/proc/softirqs", .dim = "softirqs", .func = do_proc_softirqs },

        // memory metrics
        { .name = "/proc/vmstat", .dim = "vmstat", .func = do_proc_vmstat },
        { .name = "/proc/meminfo", .dim = "meminfo", .func = do_proc_meminfo },
        { .name = "/sys/kernel/mm/ksm", .dim = "ksm", .func = do_sys_kernel_mm_ksm },
        { .name = "/sys/devices/system/edac/mc", .dim = "ecc", .func = do_proc_sys_devices_system_edac_mc },

        // network metrics
        { .name = "/proc/net/dev", .dim = "netdev", .func = do_proc_net_dev },
        { .name = "/proc/net/netstat", .dim = "netstat", .func = do_proc_net_netstat },
        { .name = "/proc/net/snmp", .dim = "snmp", .func = do_proc_net_snmp },
        { .name = "/proc/net/snmp6", .dim = "snmp6", .func = do_proc_net_snmp6 },
        { .name = "/proc/net/softnet_stat", .dim = "softnet", .func = do_proc_net_softnet_stat },
        { .name = "/proc/net/ip_vs/stats", .dim = "ipvs", .func = do_proc_net_ip_vs_stats },

        // firewall metrics
        { .name = "/proc/net/stat/conntrack", .dim = "conntrack", .func = do_proc_net_stat_conntrack },
        { .name = "/proc/net/stat/synproxy", .dim = "synproxy", .func = do_proc_net_stat_synproxy },

        // disk metrics
        { .name = "/proc/diskstats", .dim = "diskstats", .func = do_proc_diskstats },

        // NFS metrics
        { .name = "/proc/net/rpc/nfsd", .dim = "nfsd", .func = do_proc_net_rpc_nfsd },
        { .name = "/proc/net/rpc/nfs", .dim = "nfs", .func = do_proc_net_rpc_nfs },

        // IPC metrics
        { .name = "ipc", .dim = "ipc", .func = do_ipc },

        // the terminator of this array
        { .name = NULL, .dim = NULL, .func = NULL }
};

void *proc_main(void *ptr) {
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;

    info("PROC Plugin thread created with task id %d", gettid());

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("Cannot set pthread cancel type to DEFERRED.");

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("Cannot set pthread cancel state to ENABLE.");

    int vdo_cpu_netdata = config_get_boolean("plugin:proc", "netdata server resources", 1);

    // check the enabled status for each module
    int i;
    for(i = 0 ; proc_modules[i].name ;i++) {
        proc_modules[i].enabled = config_get_boolean("plugin:proc", proc_modules[i].name, 1);
        proc_modules[i].last_run_usec = 0ULL;
        proc_modules[i].duration = 0ULL;
        proc_modules[i].rd = NULL;
    }

    usec_t step = rrd_update_every * USEC_PER_SEC;
    for(;;) {
        usec_t now = now_monotonic_usec();
        usec_t next = now - (now % step) + step;

        while(now < next) {
            sleep_usec(next - now);
            now = now_monotonic_usec();
        }

        if(unlikely(netdata_exit)) break;

        // BEGIN -- the job to be done

        for(i = 0 ; proc_modules[i].name ;i++) {
            if(unlikely(!proc_modules[i].enabled)) continue;

            debug(D_PROCNETDEV_LOOP, "PROC calling %s.", proc_modules[i].name);

            proc_modules[i].enabled = !proc_modules[i].func(rrd_update_every, (proc_modules[i].last_run_usec > 0)?now - proc_modules[i].last_run_usec:0ULL);
            proc_modules[i].last_run_usec = now;

            now = now_monotonic_usec();
            proc_modules[i].duration = now - proc_modules[i].last_run_usec;

            if(unlikely(netdata_exit)) break;
        }

        // END -- the job is done

        // --------------------------------------------------------------------

        if(vdo_cpu_netdata) {
            static RRDSET *st = NULL;
            if(unlikely(!st)) {
                st = rrdset_find_bytype("netdata", "plugin_proc_modules");

                if(!st) {
                    st = rrdset_create("netdata", "plugin_proc_modules", NULL, "proc", NULL, "NetData Proc Plugin Modules Durations", "milliseconds/run", 132001, rrd_update_every, RRDSET_TYPE_STACKED);

                    for(i = 0 ; proc_modules[i].name ;i++) {
                        if(unlikely(!proc_modules[i].enabled)) continue;
                        proc_modules[i].rd = rrddim_add(st, proc_modules[i].dim, NULL, 1, 1000, RRDDIM_ABSOLUTE);
                    }
                }
            }
            else rrdset_next(st);

            for(i = 0 ; proc_modules[i].name ;i++) {
                if(unlikely(!proc_modules[i].enabled)) continue;
                rrddim_set_by_pointer(st, proc_modules[i].rd, proc_modules[i].duration);
            }
            rrdset_done(st);

            global_statistics_charts();
            registry_statistics();
        }
    }

    info("PROC thread exiting");

    static_thread->enabled = 0;
    static_thread->thread = NULL;
    pthread_exit(NULL);
    return NULL;
}
