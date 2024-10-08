#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include <cadef.h>

#include "germ.h"
#include "exp_mon.h"
#include "log.h"

extern atomic_char   count;
extern char          tmp_datafile_dir[MAX_FILENAME_LEN];
extern char          datafile_dir[MAX_FILENAME_LEN];
extern char          filename[MAX_FILENAME_LEN];
extern char          datafile_run[MAX_FILENAME_LEN];
extern char          spectrafile_run[MAX_FILENAME_LEN];
extern unsigned long filesize;
extern unsigned long runno;
extern pthread_mutex_t tmp_datafile_dir_lock;
extern pthread_mutex_t datafile_dir_lock;
extern pthread_mutex_t filename_lock;

extern pv_obj_t  pv[NUM_PVS];
extern unsigned int  nelm;
extern unsigned int  monch;

extern unsigned char tsen_proc, chen_proc;
extern unsigned char tsen_ctrl, chen_ctrl;
extern char          tsen[MAX_NELM], chen[MAX_NELM];
extern unsigned char restart;

extern char ca_dtype[7][11];
extern char gige_ip_addr[16];
extern atomic_char exp_mon_thread_ready;

//------------------------------------------------------------------------
void print_en(char* en)
{
#ifdef _DBG_
    int width = 10;
    unsigned int i, j, I, J;
    I = nelm/width;
    J = nelm % width;

    printf("  ");
    for(j=0; j<width; j++)
        printf("%6d", j);
    printf("\n");
    
    for(i=0; i<6*width+2; i++)
        printf("-");
    printf("\n");

    for(i=0; i<I; i++)
    {
        //printf("");
        printf("%2d", i);
        for(j=0; j<width; j++)
            printf("%6d", en[i*width+j]);
        printf("\n");
    }
    printf("%2d", i);
    for(j=0; j<J; j++)
        printf("%6d", en[i*width+j]);
    printf("\n\n");
#endif
}
        

//========================================================================
// Enables/disables the objects: test pulse, channel.
//========================================================================
void en_array_proc( unsigned char pv_proc,      // Can be PV_TSEN_PROC or PV_TSEN_PROC.
                    unsigned char pv_ctrl,      // Can be PV_TSEN_CTRL or PV_CHEN_CTRL.
                    unsigned char pv_en,        // Can be PV_TSEN or PV_CHEN.
                    unsigned char en_val)       // Value to enable the target. 1 for PV_TSEN, 0 for PV_CHEN.
{
    unsigned char dis_val;

    log("processing %s...\n", pv[pv_en].my_name);
    log("original:\n");
    print_en((char*)(pv[pv_en].my_var_p));

    switch(*(unsigned char*)(pv[pv_ctrl].my_var_p))
    {
        //--------------------------------------------

        case EN_CTRL_ENABLE:
            log("enable %d\n", monch);
            *(((unsigned char*)(pv[pv_en].my_var_p))+monch) = en_val;
            break;

        //--------------------------------------------

        case EN_CTRL_ENABLE_ALL:
            log("enable all.\n");
            memset(pv[pv_en].my_var_p, en_val, nelm);
            break;

        //--------------------------------------------

        case EN_CTRL_DISABLE:
            log("disable %d\n", monch);
            dis_val = 1 - en_val;
            *((unsigned char*)(pv[pv_en].my_var_p)+monch) = dis_val;
            break;

        //--------------------------------------------

        case EN_CTRL_DISABLE_ALL:
            log("disable all.\n");
            dis_val = 1 - en_val;
            memset(pv[pv_en].my_var_p, dis_val, nelm);
            break;

        //--------------------------------------------

        default:
            log("invalid option (%d).\n", *(unsigned char*)(pv[pv_ctrl].my_var_p));
    }

    log("after change:\n");
    print_en((char*)(pv[pv_en].my_var_p));

    pvs_put(pv_en, nelm);

    *(unsigned char*)(pv[pv_proc].my_var_p) = 0;
    pv_put(pv_proc);
}

//========================================================================
// Update on data file name related PV updates.
//    typedef struct event_handler_args {
//        void            *usr;   /* user argument supplied with request */
//        chanId          chid;   /* channel id */
//        long            type;   /* the type of the item returned */
//        long            count;  /* the element count of the item returned */
//        const void      *dbr;   /* a pointer to the item returned */
//        int             status; /* ECA_XXX status of the requested op from the server */
//    } evargs;
//------------------------------------------------------------------------
void pv_update(struct event_handler_args eha)
{
    uint8_t count_status;
    char str[MAX_FILENAME_LEN];

    if (ECA_NORMAL != eha.status)
    {
        err( "CA subscription status is %d instead of %d (ECA_NORMAL)!\n",
                eha.status,
                ECA_NORMAL);
        return;
    }

    log("updating %s.\n", ca_name(eha.chid));

	// monch
    if ((unsigned long)eha.chid == (unsigned long)(pv[PV_MONCH].my_chid))
    {
        monch = *(unsigned int*)eha.dbr;
        pv_put(PV_MONCH_RBV);
    }
    // tsen_proc
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_TSEN_PROC].my_chid))
    {
        if (1 == *(unsigned char*)eha.dbr)
            en_array_proc(PV_TSEN_PROC, PV_TSEN_CTRL, PV_TSEN, 1);
    }
    // chen_proc
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_CHEN_PROC].my_chid))
    {
        en_array_proc(PV_CHEN_PROC, PV_CHEN_CTRL, PV_CHEN, 0);
    }
    // tsen
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_TSEN].my_chid))
    {
        memcpy(tsen, eha.dbr, nelm);
    }
    // chen
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_CHEN].my_chid))
    {
        memcpy(chen, eha.dbr, nelm);
    }
    // tsen_ctrl
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_TSEN_CTRL].my_chid))
    {
        tsen_ctrl = *(char*)eha.dbr;
    }
    // chen_ctrl
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_CHEN_CTRL].my_chid))
    {
        chen_ctrl = *(char*)eha.dbr;
    }
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_RESTART].my_chid))
    {
        if (*(unsigned char*)eha.dbr==1)
        {
            printf("[%s]: IOC restarted. Exiting...\n", __func__);
            restart = 0;
            pv_put(PV_RESTART);
            exit(0);
        }
    }
    // filesize
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_FILESIZE].my_chid))
    {
        atomic_store_explicit(&filesize, *(unsigned long*)eha.dbr, memory_order_relaxed);
        pv_put(PV_FILESIZE_RBV);
    }
    // count
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_COUNT].my_chid))
    {
        count_status = *(unsigned long*)eha.dbr;
        if(count_status==1)
        {
            atomic_store(&count, 1);
            info("start counting.\n");
        }
        else
        {
            atomic_store(&count, 0);
            info("stop counting.\n");
        }
    }
    //tmp_datafile_dir
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_TMP_DATAFILE_DIR].my_chid))
    {
        write_protected_string((char*)eha.dbr, tmp_datafile_dir, MAX_FILENAME_LEN, &tmp_datafile_dir_lock);
        read_protected_string(tmp_datafile_dir, str, MAX_FILENAME_LEN, &tmp_datafile_dir_lock);
        info("new temp data directory is %s at %p\n", str, (void*)tmp_datafile_dir);
    }
    //datafile_dir
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_DATAFILE_DIR].my_chid))
    {
        write_protected_string((char*)eha.dbr, datafile_dir, MAX_FILENAME_LEN, &datafile_dir_lock);
        read_protected_string(datafile_dir, str, MAX_FILENAME_LEN, &datafile_dir_lock);
        info("new data directory is %s\n", str);
    }
    //filename
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_FILENAME].my_chid))
    {
        write_protected_string((char*)eha.dbr, filename, MAX_FILENAME_LEN, &filename_lock);
        read_protected_string(filename, str, MAX_FILENAME_LEN, &filename_lock);
        info("new filename is %s\n", str);
        pvs_put(PV_FILENAME_RBV, MAX_FILENAME_LEN);
    }
    // runno
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_RUNNO].my_chid))
    {
        atomic_store_explicit(&runno, *(unsigned long*)eha.dbr, memory_order_relaxed);
        pv_put(PV_RUNNO_RBV);
    }
    // ipaddr
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_IPADDR].my_chid))
    {
        strcpy(gige_ip_addr, eha.dbr);
        log("new IP address is %s\n", gige_ip_addr);
        pv_put(PV_IPADDR_RBV);
    }
    // nelm
    else if ((unsigned long)eha.chid == (unsigned long)(pv[PV_NELM].my_chid))
    {
        nelm = *(unsigned int*)eha.dbr;
        pv_put(PV_NELM_RBV);
    }
}
//------------------------------------------------------------------------


//========================================================================

void pv_subscribe(unsigned char i)
{
    SEVCHK(ca_create_subscription(pv[i].my_dtype,
                                  0, //pv[i].my_nelm,
                                  pv[i].my_chid,
                                  DBE_VALUE,
                                  pv_update,
                                  &pv[i],
                                  &pv[i].my_evid),
            "ca_create_subscription");

}

//========================================================================

void* exp_mon_thread(void * arg)
{
    printf("#####################################################\n");
    log("Initializing exp_mon_thread...\n");

    create_channel(__func__, FIRST_EXP_MON_PV, LAST_EXP_MON_PV);

    SEVCHK(ca_context_create(ca_disable_preemptive_callback),"ca_context_create @exp_mon_thread");

    // Get it for udp_conn_thread to initialize UDP connection
    pv_get(PV_IPADDR);
    pv_put(PV_IPADDR_RBV);

    for (int i=FIRST_EXP_MON_RD_PV; i<=LAST_EXP_MON_RD_PV; i++)
    {
        pv_subscribe(i);
    }

    atomic_store(&exp_mon_thread_ready, 1);

    log("exp_mon_thread initialization finished.\n");
    log("monitoring PV changes...\n");

    printf("=====================================================\n");

    SEVCHK(ca_pend_event(0.0),"ca_pend_event @exp_mon_thread");

    return NULL;
}
