/* This is a sample demonstration application that showcases usage of rpmsg 
This application is meant to run on the remote CPU running baremetal code. 
This application echoes back data that was sent to it by the master core. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "open_amp.h"
#include "rsc_table.h"
#include "baremetal.h"

#define SHUTDOWN_MSG	0xEF56A55A

/* Internal functions */
static void rpmsg_channel_created(struct rpmsg_channel *rp_chnl);
static void rpmsg_channel_deleted(struct rpmsg_channel *rp_chnl);
static void rpmsg_read_cb(struct rpmsg_channel *, void *, int, void *, unsigned long);
static void sleep();
static void init_system();

/* Globals */
static struct rpmsg_channel *app_rp_chnl;
static struct rpmsg_endpoint *rp_ept;
static struct remote_proc *proc = NULL;
static struct rsc_table_info rsc_info;
extern const struct remote_resource_table resources;

/* Application entry point */
int main() {

    /* Switch to System Mode */
    SWITCH_TO_SYS_MODE();

    /* Initialize HW system components */
    init_system();

    rsc_info.rsc_tab = (struct resource_table *)&resources;
    rsc_info.size = sizeof(resources);

    /* Initialize RPMSG framework */
    remoteproc_resource_init(&rsc_info, rpmsg_channel_created, rpmsg_channel_deleted, rpmsg_read_cb,
                    &proc);

    while (1) {
        sleep();
    }

    return 0;
}

static void rpmsg_channel_created(struct rpmsg_channel *rp_chnl) {
    app_rp_chnl = rp_chnl;
    rp_ept = rpmsg_create_ept(rp_chnl, rpmsg_read_cb, RPMSG_NULL,
                    RPMSG_ADDR_ANY);
}

static void rpmsg_channel_deleted(struct rpmsg_channel *rp_chnl) {

}

static void rpmsg_read_cb(struct rpmsg_channel *rp_chnl, void *data, int len,
                void * priv, unsigned long src) {
    if ((*(int *) data) == SHUTDOWN_MSG) {
        remoteproc_resource_deinit(proc);
    } else {
        /* Send data back to master*/
        rpmsg_send(rp_chnl, data, len);
    }
}

void sleep() {
    int i;
    for (i = 0; i < 1000; i++);
}


static void init_system() {

    /* Place the vector table at the image entry point */
    arm_arch_install_isr_vector_table(RAM_VECTOR_TABLE_ADDR);

    /* Enable MMU */
    arm_ar_mem_enable_mmu();

    /* Initialize ARM stacks */
    init_arm_stacks();

    /* Initialize GIC */
    cyclone_v_soc_dk_gic_initialize();
}
