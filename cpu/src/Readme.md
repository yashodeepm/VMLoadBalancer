## Project Instruction Updates:

1. Complete the function CPUScheduler() in vcpu_scheduler.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./vcpu_scheduler <interval>`
5. While submitting, write your algorithm and logic in this Readme.

## Code description and Algorithm
Let me start by first giving an overview of what is going in the code and in the next section I will add the algorithm used by the actual scheduling logic

1. First we get all the vCPUs present in the node (Using `virConnectListAllDomains`)
2. Next, we get the vCPU info of each of the running domains (Using `virDomainGetVcpus`)
3. Then, we get the pCPU count (Using the `virNodeGetCPUMap`)
4. We then get the per domain per pCPU stats (Using `virDomainGetCPUStats`)
5. We have global variables which store the per domain per CPU stats of the previous invocation to the function and also the previous domain info (which was returned by the `virDomainGetVcpus` function)
6. If these global variables are NULL, we do not make a decision now and store the current variable values here and return.
7. Next, we calculate the relative standard deviation of the pCPUs using the per domain per pCPU stats. We basically do this: ` CPU util of pCPU[0] = 100 * sum(pCPU[0] vcpu_time of each domain - previous pCPU[0] vcpu_time stored in global array)/interval`. Now relative standard deviation is calulated on these load values. Note that the API response returns vCPUs and pCPUs in a constant ordering throughout the execution of the scheduler. So there is no need to worry about associations between different API results.
8. vCPU load can be calculated similarly using the other stored global array (output of `virDomainGetVcps`)
9. If this is < 5%, then pinning is not changed (tp promote efficiency), else pinning decision is made based on the algorithm.

##Algorithm
I have used the Greedy multiway partitioning algorithm for implementation of the decision algorithm. Reference: https://www.jimherold.com/computer-science/greedy-number-partitions-in-java

1. We create pCPU number of partitions each of which can hold a number of vCPUs.
2. We sort the vCPU load array.
3.  For each domain present in the vCPU load array do
		get the least loaded partition from the partitions array based on the total load each partition is holding (Initially they are all 0).
		Assign the current domain picked to the least loaded partition
		Update the load of that partition by adding the current domain load to the partition load
	repeat until all domains assigned
4. Apply the pin config by reading the domains in each partition.



