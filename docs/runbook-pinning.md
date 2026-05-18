# Phase D Pinning Runbook

## Hugepages
- Reserve explicit hugepages (2 MiB pages):
  `sudo sysctl -w vm.nr_hugepages=1024`
- Verify:
  `grep -E 'HugePages_Total|HugePages_Free|Hugepagesize' /proc/meminfo`
- If explicit hugepages are not configured, benchmark falls back to anonymous mmap + `madvise(MADV_HUGEPAGE)`.

## CPU Isolation (`isolcpus`)
- Example kernel cmdline:
  `isolcpus=2-5 nohz_full=2-5 rcu_nocbs=2-5`
- Apply via bootloader config, reboot, then verify:
  `cat /proc/cmdline`

## NUMA Topology
- Inspect hardware:
  `numactl --hardware`
- Verify CPU-to-node mapping:
  `numactl --show`

## Running Tuned Bench
- Example:
  `./build/phase_d_latency_bench --config tuned --producer-cpu 2 --consumer-cpu 3 --ring-numa-node 0 --hugepages --events 5000000`

## References
- Linux kernel parameters: https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html
- HugeTLB admin guide: https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html
- NUMA memory policy: https://www.kernel.org/doc/html/latest/admin-guide/mm/numa_memory_policy.html
