# 4‑Core DPDK Packet Processing Pipeline

This project implements a **lock‑free, multi‑core packet processing pipeline** using DPDK. It distributes incoming packets across two worker cores via lock‑free rings, achieving linear scaling with zero contention.

---

## Architecture
Core 0 (RX) ──ring0──> Core 1 (Worker 1) ──ring1──┐
└──ring2──> Core 2 (Worker 2) ──ring3──┴──> Core 3 (TX)


### Core Roles

| Core | Function |
|------|----------|
| **Core 0 (RX)** | Receives packets from NIC, distributes to workers in **round‑robin**. |
| **Core 1 (Worker 1)** | Dequeues from ring0, processes packets (e.g., decrement TTL, count), enqueues to ring1. |
| **Core 2 (Worker 2)** | Dequeues from ring2, processes packets, enqueues to ring3. |
| **Core 3 (TX)** | Collects packets from both workers, transmits them. |

### Communication

- **Lock‑free DPDK rings** (`rte_ring`) pass packets between stages.
- No mutexes, no spinlocks – **zero contention**.
- Each core runs independently on its own dedicated logic core.

---

## Key Design Decisions

| Decision | Reason |
|----------|--------|
| **Lock‑free rings** | Avoid cache bouncing and contention; scale linearly with cores. |
| **Round‑robin distribution** | Simple and even load balancing across workers. |
| **Per‑core dedicated RX/TX** | No shared data structures between cores (except rings). |
| **Burst processing** | Process `BURST_SIZE` packets at a time for better cache locality. |

---

## Features

- ✅ **4‑core pipeline** (RX → Workers → TX)
- ✅ **Lock‑free inter‑core communication** (DPDK rings)
- ✅ **Round‑robin load balancing** across workers
- ✅ **Per‑port packet counting**
- ✅ **Burst processing** for cache efficiency

---

## Performance

- **Near‑linear scaling** as cores are added.
- **Zero mutex contention** – rings handle all inter‑core traffic.
- **Packets stay in cache** as they move through the pipeline.

---

## Build & Run

### Prerequisites
- DPDK installed and configured
- Hugepages enabled

### Build with Make
```bash
make


# With null devices (for testing)
sudo ./pipeline -l 0-3 -n 2 --vdev=net_null0 --vdev=net_null1

# With PCAP devices (to capture traffic)
sudo ./pipeline -l 0-3 -n 2 --vdev=net_pcap0,iface=lo --vdev=net_pcap1,iface=lo

Implementation
The main pipeline logic is in basicfwd.c. Key components:

rx_core() – receives packets, distributes to workers.

worker_core() – processes packets and forwards to TX.

tx_core() – collects from workers and transmits.

rte_ring – lock‑free queues between stages.

Repository Structure

.
├── basicfwd.c          # Main pipeline implementation
├── Makefile            # Build with make
├── meson.build         # Build with meson
└── README.md





