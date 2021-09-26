
## Project Instruction Updates:

1. Complete the function MemoryScheduler() in memory_coordinator.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./memory_coordinator <interval>`

### Notes:

1. Make sure that the VMs and the host has sufficient memory after any release of memory.
2. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
3. Domain should not release memory if it has less than or equal to 100MB of unused memory.
4. Host should not release memory if it has less than or equal to 200MB of unused memory.
5. While submitting, write your algorithm and logic in this Readme.

### Code description and Algorithm

Overview of code
1. First all active domain info is collected.
2. Set the interval of stats collection to the interval in which MemoryScheduler is invoked.
3. Store the maximum size that each of the VMs' balloons can attain
4. Collect current info on memory stats of each of the domains.
5. Make decision on whether to shrink or inflate balloon

#### Decision algorithm:
1. Set thresholds of unused sections for VM (100 MB) as well as host (200 MB).
2. Get host free memory info (using virNodeGetFreeMemory)
3. For each domain do
```
if host available memory > HOST_UNUSED_THRESHOLD then
    if max balloon size of domain has not reached and domain memory stats < DOMAIN_UNUSED_THRESHOLD then
        increase the memory allocated to the domain by the minimum of allowed balloon expansion i.e max balloon size - actual balloon size, memory increment to increase free memory to DOMAIN_UNUNSED_THRESHOLD and the maximum balloon increment value (to avoid very large memory allocations at a time)
    else if unused memory > DOMAIN_UNUSED_THRESHOLD then
        RECLAIM memory percentage wise. For eg. if 200 MB is unused and DOMAIN_UNUSED_THRESHOLD is 100 MB then decrement would be min(30% of 100, maximum deflation allowed) to reduce sudden reclamation of huge amount of memory.
else
    only reclaim memory by above logic
```
4. Repeat every interval

I have used 1 second as the memory interval for generating my log files. It is important that this run frequently to quickly free up space and allocate them.