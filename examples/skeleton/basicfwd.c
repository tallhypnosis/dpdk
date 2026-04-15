/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h> 
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_ip.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */

/* Main functional part of port initialization. 8< */
static uint64_t *ptr;
static uint16_t rx_port = 0;
static uint16_t tx_port = 1;
struct rte_ring *rx_to_worker_ring;

struct rte_ring *worker_to_tx_ring;

struct rte_ring *rx_to_worker2_ring;
struct rte_ring *worker2_to_tx_ring;
static volatile uint32_t keep_running = 1;

static struct port_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t worker1_processed;
    uint64_t worker2_processed;
} stats[RTE_MAX_ETHPORTS];

void sigint_handler(int sig) {
    signal(SIGINT, SIG_DFL);
    printf("\nCaught SIGINT, (Ctrl_C). Exiting...\n");
    keep_running = 0;
}

static int rx_core(__rte_unused void*arg) {
    struct rte_mbuf *bufs[BURST_SIZE];
    while(keep_running) {
        uint16_t nb_rx = rte_eth_rx_burst(rx_port, 0, bufs, BURST_SIZE);
        if(nb_rx > 0) {
            for(int i=0; i<nb_rx; i++) {
            if(i&1) { 
                rte_ring_enqueue(rx_to_worker_ring, bufs[i]);
            } else {
                rte_ring_enqueue(rx_to_worker2_ring, bufs[i]);
            }
            }
            stats[rx_port].rx_packets += nb_rx;
            for(int i=0; i<nb_rx; i++) {
                stats[rx_port].rx_bytes += bufs[i]->pkt_len;
            }
        }
    }
    return 0;
}

static int worker_core(__rte_unused void *arg) {
   struct rte_mbuf *bufs[BURST_SIZE];
    while(keep_running) {
        uint16_t nb_rx = rte_ring_dequeue_burst(rx_to_worker_ring, (void**)bufs, BURST_SIZE, NULL);
 if(nb_rx == 0) continue;
  for(int i=0; i<nb_rx; i++) {
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(bufs[i], struct rte_ipv4_hdr*, sizeof(struct rte_ether_hdr));
    ip->time_to_live--;
    }    
 rte_ring_enqueue_burst(worker_to_tx_ring, (void**)bufs, nb_rx, NULL);
                stats[rx_port].worker1_processed += nb_rx;
    }
    return 0;
}

static int worker_core2(__rte_unused void *arg) {
   struct rte_mbuf *bufs[BURST_SIZE];
    while(keep_running) {
        uint16_t nb_rx = rte_ring_dequeue_burst(rx_to_worker2_ring, (void**)bufs, BURST_SIZE, NULL);
 if(nb_rx == 0) continue;
  for(int i=0; i<nb_rx; i++) {
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(bufs[i], struct rte_ipv4_hdr*, sizeof(struct rte_ether_hdr));
    ip->time_to_live--;
    }    
 rte_ring_enqueue_burst(worker2_to_tx_ring, (void**)bufs, nb_rx, NULL);
                stats[rx_port].worker2_processed += nb_rx;
    }
    return 0;
}




static int tx_core(__rte_unused void *arg){
    struct rte_mbuf *bufs1[BURST_SIZE], *bufs2[BURST_SIZE];   
    while(keep_running) {
        uint16_t nb_rx1 = rte_ring_dequeue_burst(worker_to_tx_ring, (void**)bufs1, BURST_SIZE, NULL);
        uint16_t nb_rx2 = rte_ring_dequeue_burst(worker2_to_tx_ring, (void**)bufs2, BURST_SIZE, NULL);
        uint16_t nb_rx = nb_rx1 + nb_rx2;
        if(nb_rx == 0) continue;
         
        struct rte_mbuf *tx_bufs[BURST_SIZE * 2];
        int idx = 0;
        for(int i=0; i<nb_rx1; i++) tx_bufs[idx++] = bufs1[i];
        for(int i=0; i<nb_rx2; i++) tx_bufs[idx++] = bufs2[i]; 
        uint16_t nb_tx =  rte_eth_tx_burst(tx_port, 0, tx_bufs, nb_rx);
        stats[tx_port].tx_packets += nb_tx;
        for(int i=0; i<nb_tx; i++) {
        stats[tx_port].tx_bytes += tx_bufs[i]->pkt_len;
    }
    if(unlikely(nb_tx < nb_rx)) {
        for (int i=nb_tx; i<nb_rx; i++) {
            rte_pktmbuf_free(tx_bufs[i]);
        }
    }
    }
    return 0;
}


int port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}
/* >8 End of main functional part of port initialization. */

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */

 /* Basic forwarding application lcore. 8< */
/* >8 End Basic forwarding application lcore. */

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	uint16_t nb_ports;
	uint16_t portid;
    unsigned lcore_id = 0;
    signal(SIGINT, sigint_handler);
	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports < 2 || nb_ports & 1)
		rte_exit(EXIT_FAILURE, "Error: number of ports must be even\n");
	uint64_t per_port_count[nb_ports];
	memset(per_port_count, 0, nb_ports * (sizeof(uint64_t)));
	ptr = per_port_count;
	/* Creates a new mempool in memory to hold the mbufs. */

	/* Allocates mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. 8< */
	RTE_ETH_FOREACH_DEV(portid)
		if (port_init(portid, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
					portid);
	/* >8 End of initializing all ports. */

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
    
    rx_to_worker_ring = rte_ring_create("rx_to_worker", 1024, rte_socket_id(), 0);
    if(rx_to_worker_ring == NULL) 
        rte_exit(EXIT_FAILURE, "Cannot create rx_to_worker ring\n");
        
    worker_to_tx_ring = rte_ring_create("worker_to_tx", 1024, rte_socket_id(), 0);
    if(worker_to_tx_ring == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create worker_to_tx ring\n");
	/* Call lcore_main on the main core only. Called on single lcore. 8< */
    
    rx_to_worker2_ring = rte_ring_create("rx_to_worker2", 1024, rte_socket_id(), 0);
    if(rx_to_worker2_ring == NULL) 
        rte_exit(EXIT_FAILURE, "Cannot create rx_to_worker2_ring\n");
    
    worker2_to_tx_ring = rte_ring_create("worker2_to_tx_ring", 1024, rte_socket_id(), 0);
    if(worker2_to_tx_ring == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create worker2_to_tx_ring\n");

    unsigned rx_lcore = rte_get_next_lcore(lcore_id++, 1, 0);
    unsigned worker1_lcore = rte_get_next_lcore(lcore_id++, 1, 0);
    unsigned worker2_lcore = rte_get_next_lcore(lcore_id++, 1, 0);
    unsigned tx_lcore = rte_get_next_lcore(lcore_id++, 1, 0);
 
    rte_eal_remote_launch(rx_core, NULL, rx_lcore);
    rte_eal_remote_launch(worker_core, NULL, worker1_lcore);
    rte_eal_remote_launch(worker_core2, NULL, worker2_lcore);
    rte_eal_remote_launch(tx_core, NULL, tx_lcore); 
	/* >8 End of called on single lcore. */        


    
    printf("Pipeline running. Press Ctrl+C to quit.\n"); 
    while(keep_running) {
        printf("%"PRIu64"packets received\n", stats[rx_port].rx_packets);
        printf( "%"PRIu64"packets transmitted\n", stats[tx_port].tx_packets); 
        printf( "%"PRIu64"bytes received\n", stats[rx_port].rx_bytes);
        printf("%"PRIu64"bytes transmitted\n",  stats[tx_port].tx_bytes);
        printf("%"PRIu64"packtes processed by the worker1\n", stats[rx_port].worker1_processed);
        printf("%"PRIu64"packtes processed by the worker2\n", stats[rx_port].worker2_processed);

        sleep(1); 
    }
	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
