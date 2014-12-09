/*-
 *   BSD LICENSE
 * 
 *   Copyright(c) 2014, Choonho Son choonho.som@gmail.com
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "snort-main.h"

#include <rte_log.h>
#include <signal.h>

#include "app-probe.h"
#include "app-display.h"
#include "app-init.h"
#include "app-logs.h"

probe_t probe;


/**************************************************************************
 * app_usage - Display the help for the command line.
 * 
 * DESCRIPTION
 * Display the help message for the command line.
 *
 * RETURNS: N/A
 */

static void
app_usage(const char *prgname)
{
    printf("Usage: %s [EAL options] -- [-h]\n"
            " -m <string> matrix for mapping ports to logical cores\n"
            " -q <number> number of Queues per port\n"
            " -H <collector IP> (default:127.0.0.1)\n"
            " -P <collector Port> (default:2055)\n"
            " -h        Display the help information\n",
            prgname);
}

static void
parse_app_collector_host(const char *str)
{
    sscanf(str, "%s", probe.collector.addr);
}
static void
parse_app_collector_port(const char *str)
{
    sscanf(str, "%d", &probe.collector.port);
}


static void
parse_app_num_queues(const char *str)
{
    int num;
    sscanf(str, "%d", &num);
    probe.nb_queues = num;
}

static void
parse_app_l2p(const char *str)
{
    /* str ex) "1:0.0,2:0.1"
     * means lcore(1) processes port(0) + qeuue(0)
     *       lcore(2) processes port(0) + queue(1)
     */
    char temp[64];
    strcpy(temp, str);
    char *ptr;
    int lid,pid,qid;
    int idx=0;
    int lcore_count = 0;
    uint8_t lcore_id;

    ptr = strtok(temp, ",");
    sscanf(ptr, "%d:%d.%d", &lid, &pid, &qid);
    probe.l2p[idx].lcore_id = lid;
    probe.l2p[idx].port_id = pid;
    probe.l2p[idx].queue_id = qid;
    idx++;
    while (ptr = strtok(NULL, ",")) {
        sscanf(ptr, "%d:%d.%d", &lid, &pid, &qid);
        probe.l2p[idx].lcore_id = lid;
        probe.l2p[idx].port_id = pid;
        probe.l2p[idx].queue_id = qid;
        idx++;
    }
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        lcore_count++;
    }
    if (idx != lcore_count) {
        APP_INIT_LOG(ERR, "ERROR: lcore_count(%d) and mapping(%d) is not match\n", --lcore_count, --idx);
    }    
}

/****************************************************************************** 
 * app_parse_args - Parse the argument given in the command line of the application 
 *
 * DESCRIPTION
 * Main parsing routine for the command line.
 * 
 * RETURNS: N/A
 */

static int
app_parse_args(int argc, char **argv)
{
    int opt, ret;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    static struct option lgopts[] = {
        {NULL, 0, 0, 0}
    };

    argvopt = argv;

    while ((opt = getopt_long(argc, argvopt, "m:H:P:q:",
                  lgopts, &option_index)) != EOF) {

        switch (opt) {
        case 'm':
            /* lcore:port.queue mapping 
             * ex) -m "1:0.0" means lcore1 processes queueue 0 of port 0
             */
            parse_app_l2p(optarg);
            break;
        case 'H':
            // Collector IP
            parse_app_collector_host(optarg);
            break;
        case 'P':
            // Collector Port
            parse_app_collector_port(optarg);
            break;
        case 'q':
            // Number of Queues / Port 
            parse_app_num_queues(optarg);
            break;
        /* long options */
        case 0:
            app_usage(prgname);
            return -1;

        default:
            app_usage(prgname);
            return -1;
        }
    }

    if (optind >= 0)
        argv[optind-1] = prgname;

    ret = optind-1;
    optind = 0; /* reset getopt lib */
    return ret;
}

void init_probe(probe_t *p)
{
    memset(p, 0, sizeof(probe));
    /* set default vaule */
    strcpy(p->collector.addr, "127.0.0.1");
    p->collector.port = 2055;

    p->nb_queues = 1;
}


int main(int argc, char **argv)
{
    int32_t ret;
    uint8_t lcore_id;

    /* Signal */
    signal(SIGINT,(void *)app_print);
 

    clrscr();
    // call before the rte_eal_init()
    (void)rte_set_application_usage_hook(app_usage);

    init_probe(&probe);
    
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Failed in rte_eal_init\n");
    argc -= ret;
    argv += ret;
    
    ret = app_parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid arguments\n");
  
    app_init(&probe);

    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        rte_eal_remote_launch(launch_probe, NULL, lcore_id);
    }
    rte_delay_ms(5000);     // wait for the lcores to start up

    // Wait for all of the cores to stop runing and exit.

    clrscr();
    app_logo(8, 0, APP_NAME); 
    //process_hashtable();
    rte_eal_mp_wait_lcore(); 

    return 0;
}

