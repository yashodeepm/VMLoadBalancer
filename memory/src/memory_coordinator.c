#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

void MemoryScheduler(virConnectPtr conn,int interval);

const int DOMAIN_UNUSED_THRESHOLD = 100 * 1024;
const int HOST_UNUSED_THRESHOLD = 200 * 1024;
const int MAX_BALLOON_INFLATE_PERMITTED = 40 * 1024;
const int MAX_BALLOON_DEFLATE_PERMITTED = 100 * 1024;
const int MAX_RECLAIM_PERCENTAGE = 30;
int ITERATION = 0;

typedef struct memoryStat {
	virDomainPtr domain;
	unsigned long long int actual;
	unsigned long long int unused;
} memoryStat;

unsigned long long int min(unsigned long long int a, unsigned long long int b) {
	return (a<b)?a:b;
}
unsigned long long int max(unsigned long long int a, unsigned long long int b) {
	return (a>b)?a:b;
}

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

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	ITERATION++;
	printf("Iteration Number: %d\n", ITERATION);
	virDomainPtr * domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE; // filter out only the active guestOS

	int vcpuCount = virConnectListAllDomains(conn, &domains, flags);
	if (vcpuCount < 0)
		perror("Issue returning active domains\n");

	for(int i = 0;i < vcpuCount;i++)
		virDomainSetMemoryStatsPeriod(domains[i], interval, 0); // Set domain stats period to be equal to the interval
	
	// Store maximum domains of each domain
	unsigned long long int * maxBalloonSize = (unsigned long long int *)malloc(vcpuCount * sizeof(unsigned long long int));
	for(int i = 0;i < vcpuCount;i++) 
		maxBalloonSize[i] = virDomainGetMaxMemory(domains[i]);

	// Get the host memory stats for setting limits on balloon driver allocation
	unsigned long long int hostAvailableMemory = virNodeGetFreeMemory(conn)/1024;

	/**
	 * 1. Get memory information of all domains.
	 * 2. Loop over each domain getting all memory stats.
	 * 3. Apply either expand balloon or deflate balloon within constraint.
	 */

	virDomainMemoryStatPtr * domainsMemoryStats = (virDomainMemoryStatPtr *)malloc(vcpuCount * sizeof(virDomainMemoryStatPtr));
	for(int i = 0;i < vcpuCount; i++) { domainsMemoryStats[i] = (virDomainMemoryStatPtr)malloc(VIR_DOMAIN_MEMORY_STAT_NR * sizeof(virDomainMemoryStatStruct)); }
	for(int i = 0;i < vcpuCount; i++)
		virDomainMemoryStats(domains[i], domainsMemoryStats[i], VIR_DOMAIN_MEMORY_STAT_NR, 0);

	memoryStat * memoryStats = (memoryStat*)malloc(vcpuCount * sizeof(memoryStat));
	for(int i = 0;i < vcpuCount; i++) {
		memoryStat temp;
		temp.domain = domains[i];
		for(int j = 0;j < VIR_DOMAIN_MEMORY_STAT_NR; j++) {
			if (domainsMemoryStats[i][j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) { temp.actual = domainsMemoryStats[i][j].val; }
			if (domainsMemoryStats[i][j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) { temp.unused = domainsMemoryStats[i][j].val; }
		}
		memoryStats[i] = temp;
	}

	for(int i = 0;i < vcpuCount; i++) {
		long long int balloonChange = 0;
		if (hostAvailableMemory > HOST_UNUSED_THRESHOLD) {
			if(memoryStats[i].actual < maxBalloonSize[i] && memoryStats[i].unused < DOMAIN_UNUSED_THRESHOLD) {
				balloonChange = min(maxBalloonSize[i] - memoryStats[i].actual, min(DOMAIN_UNUSED_THRESHOLD - memoryStats[i].unused, MAX_BALLOON_INFLATE_PERMITTED));
			} else if (memoryStats[i].unused > DOMAIN_UNUSED_THRESHOLD) {
				balloonChange = -1 * min(MAX_RECLAIM_PERCENTAGE * (memoryStats[i].unused - DOMAIN_UNUSED_THRESHOLD)/100, MAX_BALLOON_DEFLATE_PERMITTED);
			}
			// printf("Balloon change on domain: %d by amount:%lld. Actual size: %lld\n", i, balloonChange, memoryStats[i].actual);
		} else {
			if (memoryStats[i].unused > DOMAIN_UNUSED_THRESHOLD) {
				balloonChange = -1 * min(MAX_RECLAIM_PERCENTAGE * (memoryStats[i].unused - DOMAIN_UNUSED_THRESHOLD)/100, MAX_BALLOON_DEFLATE_PERMITTED);
			}
		}
		virDomainSetMemory(domains[i], memoryStats[i].actual + balloonChange);
		hostAvailableMemory = virNodeGetFreeMemory(conn)/1024;
	}
}
