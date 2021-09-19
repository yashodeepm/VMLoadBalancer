#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#include <assert.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

typedef struct VcpuLoadInfo {
	virDomainPtr domain;
	double vcpuLoad;
} VcpuLoadInfo;

typedef struct Partition {
	double partitionSum;
	virDomainPtr * domain;
	int fill_index;
} Partition;

void CPUScheduler(virConnectPtr conn,int interval);
double computeVcpuLoad(virVcpuInfo prevDomainInfo, virVcpuInfo currDomainInfo, int interval);
void computePcpuLoad(virTypedParameterPtr * perDomainCPUStats, double * pcpuLoad, int pcpuCount, int vcpuCount, int nparams, int interval);
double computeStandardDeviation(double * cpuLoad, int cpuCount);
int compareVcpuLoad(const void * load1, const void * load2);
int comparePartitionSum(const void * p1, const void * p2);

virVcpuInfoPtr prevDomainInfo = NULL;
virTypedParameterPtr * prevPerDomainCPUStats = NULL;

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	virDomainPtr * domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE; // filter out only the active guestOS

	int vcpuCount = virConnectListAllDomains(conn, &domains, flags);
	if (vcpuCount < 0)
		perror("Issue returning active domains\n");
	// vcpuCount is the number of domains returned. domains[0] gives the address to the virDomain object but it is an incomplete struct and is not visible. Use this domain to pass on to functions and process domains.
	// Get Vcpu info for each domain collected.
	virVcpuInfoPtr vcpuInfoPtr = (virVcpuInfoPtr)malloc(vcpuCount * sizeof(virVcpuInfo));
	
	// Construct VCPU info array
	for(int i = 0;i < vcpuCount; i++) {
		int ret = virDomainGetVcpus(domains[i], &vcpuInfoPtr[i], 1, NULL, 0); // keeping maxinfo as 1 because number of vCPUs per VM is 1
		if(ret < 0)
			perror("Issue getting VCPU info\n");
	}

	// Get pCPU info
	unsigned int pcpuCount;
	int ret = virNodeGetCPUMap(conn, NULL, &pcpuCount, 0);
	if (ret < 0) 
		perror("Unable to get CPU map\n");

	// Get per domain per CPU details
	virTypedParameterPtr * perDomainCPUStats = (virTypedParameterPtr*)malloc(vcpuCount * sizeof(virTypedParameter));
	int nparams = virDomainGetCPUStats(domains[0], NULL, 0, 0, 1, 0); // Getting value of nparams for first domain because they will be same for all others. See documentation of API for details

	for(int i = 0;i < vcpuCount;i++) {
		virTypedParameterPtr domainCPUStats = (virTypedParameterPtr)malloc(pcpuCount * nparams * sizeof(virTypedParameter));
		virDomainGetCPUStats(domains[i], domainCPUStats, nparams, 0, pcpuCount, 0);
		perDomainCPUStats[i] = domainCPUStats;
	}

	/**
	 * 1. If prevDomainInfo is NULL, then populate it and return for next iteration.
	 * 2. If prevDomainInfo is not NULL then use current time to calculate the current load produced by each vCPU.
	 * 3. Use this load to divide it into partitions and pin accordingly. To reduce overhead of pin changes, 
	 *    calculate the relative standard deviation of the current load. If already < 5% then do not make the pin change.
	 **/
	
	if(prevDomainInfo == NULL) { prevDomainInfo = vcpuInfoPtr; prevPerDomainCPUStats = perDomainCPUStats; return; }

	// The virDomainGetVcpus API returns domains in the order of their IDs so for an execution it does not change
	double * vcpuLoad = (double*)malloc(vcpuCount * sizeof(double));
	for(int i = 0;i < vcpuCount; i++) { vcpuLoad[i] = computeVcpuLoad(prevDomainInfo[i], vcpuInfoPtr[i], interval); }
	
	// For computing the current pCPU load
	double * pcpuLoad = (double*)malloc(pcpuCount * sizeof(double));
	computePcpuLoad(perDomainCPUStats, pcpuLoad, pcpuCount, vcpuCount, nparams, interval);
	double standardDeviationOfCurrentPcpuLoad = computeStandardDeviation(pcpuLoad, pcpuCount);
	printf("Standard Deviation of pCPU Load: %lf\n", standardDeviationOfCurrentPcpuLoad);

	if(standardDeviationOfCurrentPcpuLoad < 5) { prevPerDomainCPUStats = perDomainCPUStats; prevDomainInfo = vcpuInfoPtr; return; }
	
	// Now create a custom struct to store the vcpuInfo with its corresponding load to facilitate sorting of this vcpu list
	VcpuLoadInfo * vcpuLoadInfo = (VcpuLoadInfo*)malloc(vcpuCount * sizeof(VcpuLoadInfo));
	for(int i = 0;i < vcpuCount; i++) {
		vcpuLoadInfo[i].domain = domains[i];
		vcpuLoadInfo[i].vcpuLoad = vcpuLoad[i];
	}

	// Apply greedy multiway partition algorithm
	Partition * partition = (Partition*)malloc(pcpuCount * sizeof(Partition));
	for(int i = 0;i < pcpuCount; i++) {
		partition[i].partitionSum = 0; // Initialise paritionSum to 0 and no domain
		partition[i].domain = (virDomainPtr *)malloc(vcpuCount * sizeof(virDomainPtr));
		memset(partition[i].domain, 0, vcpuCount * sizeof(virDomainPtr)); // Set pointers to NULL
		partition[i].fill_index = 0;
	}

	qsort(vcpuLoadInfo, vcpuCount, sizeof(VcpuLoadInfo), compareVcpuLoad); // Sort VMs with resepect to load
	printf("\n");
	for(int i = 0;i < vcpuCount; i++) {
		int least_index = 0;
		double least_value = partition[0].partitionSum;
		for(int j = 1;j < pcpuCount; j++) {
			if(partition[j].partitionSum < least_value) {least_index = j;least_value = partition[j].partitionSum;}
		}
		(partition[least_index].domain)[partition[least_index].fill_index] = vcpuLoadInfo[i].domain;
		partition[least_index].partitionSum = (partition[least_index].partitionSum + vcpuLoadInfo[i].vcpuLoad);
		(partition[least_index].fill_index)++;
	}

	for(int i = 0;i<pcpuCount; i++) {
		printf("%d ", partition[i].fill_index);
	}
	printf("\n");

	// Now that we have the partition bindings for each pCPU we need to pin the VMs accordingly
	unsigned char cpumap[] = "a"; // random initialise
	for(int i = 0;i < pcpuCount; i++) {
		int j = 0;
		while(j < vcpuCount && (partition[i].domain)[j] != NULL) {
			// pin this VM to pCPU i
			cpumap[0] = 1 << i;
			int ret = virDomainPinVcpu((partition[i].domain)[j], 0, cpumap, 1); // Only 4 CPUs present so 1 byte enough to represent them.
			if(ret < 0) 
				perror("Unable to pin Vcpu configuration");
			j++;
		}
	}

	prevPerDomainCPUStats = perDomainCPUStats;
	prevDomainInfo = vcpuInfoPtr;
}

int comparePartitionSum(const void * p1, const void * p2) {
	return ((Partition*)p1)->partitionSum - ((Partition*)p2)->partitionSum;
}

int compareVcpuLoad(const void * load1, const void * load2) {
	return ((VcpuLoadInfo*)load1)->vcpuLoad - ((VcpuLoadInfo*)load2)->vcpuLoad;
}

double computeVcpuLoad(virVcpuInfo prevDomainInfo, virVcpuInfo currDomainInfo, int interval) {
	double diff = (double)(currDomainInfo.cpuTime - prevDomainInfo.cpuTime)/(double)10000000;
	return diff/(double)interval;
}

void computePcpuLoad(virTypedParameterPtr * perDomainCPUStats, double * pcpuLoad, int pcpuCount, int vcpuCount, int nparams, int interval) {
	for(int i = 1;i < nparams * pcpuCount; i += 2) { 
		double sum = 0;
		for(int j = 0;j < vcpuCount; j++) {
			sum += ((*(*(perDomainCPUStats + i) + j)).value.ul - (*(*(prevPerDomainCPUStats + i) + j)).value.ul);
		}
		pcpuLoad[i/2] = (sum/10000000.0)/interval;
	}
}

double computeStandardDeviation(double * cpuLoad, int cpuCount) {
	double sum = 0;
	for(int i = 0;i < cpuCount; i++)
		sum += cpuLoad[i];
	double mean = sum/cpuCount;
	double sumOfSquares = 0;
	for(int i = 0;i < cpuCount; i++) 
		sumOfSquares += pow(cpuLoad[i] - mean, 2);
	return (100*pow(sumOfSquares/(cpuCount - 1), 0.5))/mean;
}