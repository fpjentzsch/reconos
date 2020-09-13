/* Combination of the sortdemo and matrixmuliplication using partial reconfiguration */
#define  _GNU_SOURCE

#include "reconos.h"
#include "reconos_app.h"
#include "timer.h"

#include "utils.h"
#include "lib/runtime/arch/arch.h"
#include "lib/runtime/private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "mmp.h"
#include "common.h"
#include "main.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define BLOCK_SIZE 2048

#define log(...) printf(__VA_ARGS__); fflush(stdout)


void print_help() {
	printf("\n"
	       "ReconOS v4 sort/matrixmul dynamic reconfiguration demo application\n"
	       "--------------------------------\n"
	       "\n"
	       "Sorts a buffer full of data with a variable number of sw and hw threads, reconfigures the sortdemo hw threads to matrixmul threads, and runs the matrixmul demo.\n"
	       "\n"
	       "Usage:\n"
	       "    reconfsortmatrixmul <num_hw_threads> <num_sw_threads> <num_of_blocks> <num_of_blocks_matrix>\n"
	       "\n"
	       "    <num_hw_threads> - Number of hardware threads to create. The maximum number is\n"
	       "                       limited by the hardware design.\n"
	       "    <num_sw_threads> - Number of software threads to create.\n"
	       "    <num_of_blocks_sort>  - Number of blocks to create and sort. This must be a power of 2.\n"
	       "	<num_of_blocks_matrix>  - Size of matrices to multiply. Must be one of 256,512,1024 or 2048\n"
	       "\n"
	);
}

/* functions for the reconfiguration */
int program_fpga(char* bitstream_file, unsigned partial)
{
  printf ("Programming FPGA with bitstream %s..\n", bitstream_file);

  char cmd[128];
  if(partial)
	sprintf(cmd, "fpgautil -b %s -f Partial", bitstream_file);
  else
    sprintf(cmd, "fpgautil -b %s -f Full", bitstream_file);

  FILE *fd = popen(cmd, "r");

  char buf[256];
  if (fread (buf, 1, sizeof (buf), fd) > 0)
  {
    printf ("Result:\n%s", buf);
  }
  else
  {
    fprintf(stderr, "ERROR programming FPGA\n");
    return -1;
  }

  fclose(fd);
  return 0;
}

//no longer required if fpgautil is installed
int reconfigure_fpga_manager(char* filename, unsigned partial){

	//Check if bitstream file exists. For the FPGA Manager, it needs to be located at /lib/firmware/
	char path [114] = "/lib/firmware/";
	struct stat buffer;
  	if (stat(strcat(path, filename), &buffer) != 0){
		log("Bitstream file %s could not be found at /lib/firmware/\n",filename);
		return -1;
	}

	int fd_partial = open("/sys/class/fpga_manager/fpga0/flags", O_RDWR);
	if(fd_partial < 0){
		log("Failed to open FPGA Manager attribute 'flags' when configuring %s\n",filename);
		return -1;
	}

	char partial_flag[2];
	if(!partial){
		strcpy(partial_flag,"0");
	}
	else {
		strcpy(partial_flag,"1");
	}

	write(fd_partial, partial_flag, 2);
	close(fd_partial);

	fd_partial = open("/sys/class/fpga_manager/fpga0/firmware", O_WRONLY);
	if(fd_partial < 0){
		log("Failed to open FPGA Manager attribute 'firmware' when configuring %s\n",filename);
		return -1;
	}
	log("Opened FPGA Manager. Initiating bitstream reconfiguration\n");
	write(fd_partial, filename, strlen(filename));

	int fd_finish_flag = open("/sys/class/fpga_manager/fpga0/state", O_RDONLY);
	char finish_flag[32];

	/* wait until reconfiguration is finished */
	while(strncmp(finish_flag, "operating", 9)){
		read(fd_finish_flag,finish_flag,32);
	}
	log("Reconfiguration with bitfile %s finished\n",filename);
	close(fd_partial);
	close(fd_finish_flag);

	return 0;
}

int reconfigure(char* filename, unsigned partial){
	return program_fpga(filename, partial);
}

/* functions for sortdemo */

int cmp_uint64t(const void *a, const void *b) {
	return *(uint64_t *)a - *(uint64_t *)b;
}

void _merge(uint64_t *data, uint64_t *tmp,
           int l_count, int r_count) {
	int i;
	uint64_t *l = data, *r = data + l_count;
	int li = 0, ri = 0;

	for (i = 0; i < l_count; i++) {
		tmp[i] = l[i];
	}

	for (i = 0; i < l_count + r_count; i++) {
		if (ri >= r_count || (li < l_count && tmp[li] < r[ri])) {
			data[i] = tmp[li];
			li++;
		} else {
			data[i] = r[ri];
			ri++;
		}
	}
}

void merge(uint64_t *data, int data_count) {
	int bs, bi;
	uint64_t *tmp;

	tmp = (uint64_t *)malloc(data_count * sizeof(uint64_t));

	for (bs = BLOCK_SIZE; bs < data_count; bs += bs) {
		for (bi = 0; bi < data_count; bi += bs + bs) {
			if (bi + bs + bs > data_count) {
				_merge(data + bi, tmp, bs, data_count - bi - bs);
			} else {
				_merge(data + bi, tmp, bs, bs);
			}
		}
	}

	free(tmp);
}

int main(int argc, char **argv) {
	int i;
	int num_hwts, num_swts, num_blocks, str_matrix_size;
	uint64_t *data, *copy;
	int data_count;

	if (argc != 5) {
		print_help();
		return 0;
	}

	num_hwts = atoi(argv[1]);
	num_swts = atoi(argv[2]);
	num_blocks = atoi(argv[3]);
	str_matrix_size = atoi(argv[4]);

	unsigned int t_start, t_gen, t_sort, t_merge, t_check;
	unsigned int t_reconfiguration_full = 0;
	unsigned int *t_suspend = (unsigned int *) malloc(num_hwts * sizeof(unsigned int));
	unsigned int *t_reconfiguration = (unsigned int *) malloc(num_hwts * sizeof(unsigned int));
	unsigned int *t_resume = (unsigned int *) malloc(num_hwts * sizeof(unsigned int));

	//timer_init(); 

	//check if num_blocks (sortdemo) is a power of 2. Multiple of 2 also works, but leads to segmentation faults for some values.
	if (((num_blocks & (num_blocks-1)) != 0) || (num_blocks == 0))
	{
		print_help();
		return 0;
	}

	if(!((str_matrix_size == 256) || (str_matrix_size == 512) || (str_matrix_size == 1024) || (str_matrix_size == 2048)))
	{
		print_help();
		return 0;
	}

	//t_reconfiguration_full = timer_get();
	if(reconfigure("Config_sortdemo_0_sortdemo_1_implement_full.bin",0) < 0){
		printf("Failed to load static bitfile of sortdemo\n");
		return -1;
	}
	//t_reconfiguration_full = timer_get() - t_reconfiguration_full;

	reconos_init();
	reconos_app_init();
	timer_init(); 

	reconos_clock_threads_set(100000); //set dynamic clock "threads" to 100MHz

	log("creating %d hw-threads:", num_hwts);
	struct reconos_thread **reconos_hwts = (struct reconos_thread **) malloc(num_hwts * sizeof(struct reconos_thread));
	if(!reconos_hwts){
		log("Could not allocate memory for %d hardware threads\n",num_hwts);
	}
	for (i = 0; i < num_hwts; i++) {
		log(" %d", i);
		reconos_hwts[i]=reconos_thread_create_hwt_reconf();
	}
	log("\n");

	struct reconos_thread **reconos_swts = (struct reconos_thread **) malloc(num_swts * sizeof(struct reconos_thread));
	if(!reconos_swts){
		log("Could not allocate memory for %d software threads\n",num_swts);
	}
	log("creating %d sw-thread:", num_swts);
	for (i = 0; i < num_swts; i++) {
		log(" %d", i);
		reconos_swts[i]=reconos_thread_create_swt_sortdemo();
	}
	log("\n");

	t_start = timer_get();
	log("generating data ...\n");
	data_count = num_blocks * BLOCK_SIZE;
	data = (uint64_t *)malloc(data_count * sizeof(uint64_t));
	copy = (uint64_t *)malloc(data_count * sizeof(uint64_t));
	for (i = 0; i < data_count; i++) {
		data[i] = data_count - i - 1;
	}
	memcpy(copy, data, data_count * sizeof(uint64_t));
	t_gen = timer_get() - t_start;

	log("putting %d blocks into job queue: ", num_blocks);
	for (i = 0; i < num_blocks; i++) {
		mbox_put(resources_address, (uint64_t)(data + i * BLOCK_SIZE));
		log(".");
	}
	log("\n");

	t_start = timer_get();
	log("waiting for %d acknowledgements: ", num_blocks);
	for (i = 0; i < num_blocks; i++) {
		mbox_get(resources_acknowledge);
		log(".");
	}

	log("\n");
	t_sort = timer_get() - t_start;

#if 1
	t_start = timer_get();
	log("merging sorted data slices ...\n");
	merge(data, data_count);
	t_merge = timer_get() - t_start;
#endif

	t_start = timer_get();
	log("checking sorted data ...\n");
	qsort(copy, data_count, sizeof(uint64_t), cmp_uint64t);
	for (i = 0; i < data_count; i++) {
		if (data[i] != copy[i]) {
			log("expected %#.16lx but found %#.16lx at %d\n", copy[i], data[i], i);
		}
	}
	t_check = timer_get() - t_start;

	log("sending terminate message to sortdemo HWTs and SWTs:");
	for (i = 0; i < num_hwts + num_swts; i++) {
		log(" %d", i);
		mbox_put(resources_address, 0xffffffffffffffff);
	}
	log("\n");

	/* reconfiguration */

	for(i = 0; i < num_hwts; i++){
		/*construct name of bitfile*/
		char filename[100] = "pblock_slot_";
		char filename_2[100] = "_matrixmul_";
		char id[10];
		sprintf(id, "%d",i);
		strcat(filename,id);
		strcat(filename,filename_2);
		strcat(filename,id);
		strcat(filename,"_partial.bin");

		/*suspend each thread and reconfigure */
		log("Suspending HWT %d\n",i);
		t_suspend[i] = timer_get();
		reconos_thread_suspend_block(reconos_hwts[i]);
		t_suspend[i] = timer_get() - t_suspend[i];

		t_reconfiguration[i] = timer_get();
		if(reconfigure(filename,1) < 0){
			return -1;
		}
		t_reconfiguration[i] = timer_get() - t_reconfiguration[i];
	}

	/* terminate sortdemo software threads */
	//for(i = 0; i < num_swts;i++){
	//	mbox_put(resources_address,0xffffffffffffffff);
	//}

	for(i=0; i< num_swts; i++){
		pthread_join(reconos_swts[i]->swslot,NULL);
		log("SWT %d was terminated\n",i);
	}

	/* resume hardware threads */
	for(i = 0; i < num_hwts; i++){
		log("Resume HWT %d\n",i);
		t_resume[i] = timer_get();
		reconos_thread_resume(reconos_hwts[i],i);
		t_resume[i] = timer_get() - t_resume[i];
	}

	/* create matrixmul software threads */
	for(i = 0; i< num_swts; i++){
		log("Creating MatrixMul SWT %d\n",i);
		reconos_swts[i] = reconos_thread_create_swt_matrixmul();
	}


	/* matrixmul */

	unsigned generate_data_time;
	unsigned generate_check_result_time;
	unsigned str_mmp_split;
	unsigned std_mmp_time;
	unsigned str_mmp_combine;
	unsigned comparision_time;
	unsigned calculation_time_std;
	unsigned calculation_time_str;
	
	printf("matrixmul build: %s %s\n", __DATE__, __TIME__);

	int std_matrix_size	= STD_MMP_MATRIX_SIZE; // Fixed by hardware thread


	int mbox_size = (int) pow(7, ((int)log2(str_matrix_size)) - ((int)log2(STD_MMP_MATRIX_SIZE)));
	printf("Size of mailboxes: %i\n", mbox_size);

	int64_t *i_matrixes[2]	= {NULL, NULL};
	int64_t *o_matrix		= NULL;
	int64_t *compare		= NULL;

	MATRIXES* std_mmp_matrixes = NULL;

	log("Generating input data.\n");
	generate_data_time = timer_get();
	generate_data(i_matrixes, &o_matrix, str_matrix_size);
	generate_data_time = timer_get() - generate_data_time;

	log("Generating check results.\n");
	generate_check_result_time = timer_get();
	generate_result(i_matrixes, &compare, str_matrix_size);
	generate_check_result_time = timer_get() - generate_check_result_time;

	// split input matrixes recursively (strassen algorithm part 1)
	log("Running Strassen algorithm part 1 - split.\n");
	str_mmp_split = timer_get();
	str_matrix_split(i_matrixes[0], i_matrixes[1], &std_mmp_matrixes, str_matrix_size);
	str_mmp_split = timer_get() - str_mmp_split;

	// calculate matrixes with standard mmp algorithm (in hw and/or sw)
	log("Putting matrix pointers in mbox.\n");
	std_mmp_time = timer_get();
	MATRIXES *ptr = std_mmp_matrixes;

	for (i=0; i<mbox_size; ++i) {
		printf("Putting pointer to matrixes into mbox: %p, %p, %p\n", ptr->matrixes[0],ptr->matrixes[1],ptr->matrixes[2]);
		mbox_put(resources_address,(int64_t)(ptr->matrixes));
		ptr = ptr->next;
	}
	log("Waiting for acknowledgements...\n");
	for (i=0; i<mbox_size; ++i) {
		printf("Getting pointer to matrixes from mbox: %p\n", (void*)mbox_get(resources_acknowledge));
	}
	std_mmp_time = timer_get() - std_mmp_time;
	log("Got acknowledgments.\n");

	// combine results (strassen algorithm part 2)
	log("Running Strassen algorithm part 2 - combine.\n");
	str_mmp_combine = timer_get();
	o_matrix = str_matrix_combine(&std_mmp_matrixes, std_matrix_size, str_matrix_size);
	str_mmp_combine = timer_get() - str_mmp_combine;

	// check, if results are correct
	comparision_time = timer_get();
	int correct_result =  compare_result(o_matrix, compare, str_matrix_size);
	comparision_time = timer_get() - comparision_time;

	if (correct_result == -1) {
		log("\nResult is correct.\n\n");
	} else {
		log("\nBad result.\n");
		printf("Comparison failed at index %i.Correct: %li, Actual result: %li.\n", correct_result, compare[correct_result], o_matrix[correct_result] );
#if 1
		print_matrix(i_matrixes[0], 'A', str_matrix_size);
		print_matrix(i_matrixes[1], 'B', str_matrix_size);
		print_matrix(o_matrix    , 'C', str_matrix_size);
		print_matrix(compare          , 'Z', str_matrix_size);
#endif
		log("\n");
		exit(EXIT_FAULTY_RESULT);
	}

	calculation_time_std = generate_check_result_time;
	calculation_time_str = str_mmp_split + std_mmp_time + str_mmp_combine;

	log("Sortdemo timing information\n");
	log("===========================\n");
	log("Size: %d words, %d hw-threads, %d sw-threads):\n"
    "  Generate data: %f ms\n"
    "  Sort data    : %f ms\n"
    "  Merge data   : %f ms\n"
    "  Check data   : %f ms\n"
    "Total computation time (sort & merge): %f ms\n\n",
    data_count, num_hwts, num_swts,
    timer_toms(t_gen), timer_toms(t_sort), timer_toms(t_merge),
    timer_toms(t_check), timer_toms(t_sort + t_merge));

    log("Reconfiguration timing information\n");
    log("==================================\n");
    log("Loading full bitfile: %f ms\n", timer_toms(t_reconfiguration_full));
    for(i = 0; i < num_hwts; i++){
    log("Suspending HWT %d:     %f ms\n", i, timer_toms(t_suspend[i]));
    log("Reconfiguring HWT %d:  %f ms\n", i, timer_toms(t_reconfiguration[i]));
    log("Resuming HWT %d:       %f ms\n", i, timer_toms(t_resume[i]));
    }
    log("\n");


	log("Matrixmul timing information\n");
	log("============================\n");
	log("Generate input data:   %f ms\n", timer_toms(generate_data_time));
	log("Generate check result: %f ms\n", timer_toms(generate_check_result_time));
	log("Str. split (part 1):   %f ms\n", timer_toms(str_mmp_split));
	log("Std. MMP:              %f ms\n", timer_toms(std_mmp_time));
	log("Str. combine (part 2): %f ms\n", timer_toms(str_mmp_combine));
	log("Check HWT result:      %f ms\n\n", timer_toms(comparision_time));
	log("Matrixmul important timing results\n");
	log("==================================\n");
	log("Runtime Std. MMP:      %f ms\n", timer_toms(calculation_time_std));
	log("Runtime Str. MMP:      %f ms\n", timer_toms(calculation_time_str));



	timer_cleanup();
	reconos_app_cleanup();
	reconos_cleanup();

	return 0;
}