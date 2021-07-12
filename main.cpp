//compiled with VS2019 and command "cl main.cpp -Z7"
//documentation says _WIN32_WINNT must at least be 0x0400. I didn't need to define it, maybe you do, idk.

#include "Windows.h"
#include "assert.h"
#include "time.h"
#include "stdio.h"
#include "math.h"

const int number_of_jobs = 20;
const int number_of_workers = 4;

struct Job_Data{
	void* fiber;
	int id;
	double time_when_network_is_done;
	bool has_job_started = false;
	bool is_job_done = false;
	bool can_be_taken_by_a_worker = true;
	HANDLE access_mutex;
};

struct Worker_Data{
	void* base_fiber;
	double random_state;
	int id;
	HANDLE thread_handle;
};

thread_local Worker_Data* worker_data;

Job_Data global_job_pool[number_of_jobs] = {};

double get_double_between_0_and_1(){
	//the shittiest rng I have ever written, but its predictable so you can reproduce things.
	worker_data->random_state = (worker_data->random_state * 31.4 + 0.12) / 1.7;
	worker_data->random_state = fmod(worker_data->random_state, 1);
	return worker_data->random_state;
}


void job_yield(Job_Data* job_data){
	// printf("job %d yields\n", job_data->id);
	SwitchToFiber(worker_data->base_fiber);
	// printf("job %d resumed\n", job_data->id);
}

void schedule_job(Job_Data* job_data){
	SwitchToFiber(job_data->fiber); //apparently stepping in the debugger won't let you enter the fiber execution. You have to actually set a breakpoint
}

bool is_network_call_done(Job_Data* job_data){
	return (double)clock()/(double)CLOCKS_PER_SEC > job_data->time_when_network_is_done;
}

void start_network_call_and_yield(Job_Data* job_data){
	//a common use case for co-routines and fibers and the likes is that you do asynchronous network operations, but structure your code like they were synchronous. Instead of an expensive network call, we just say that the network call will take between 1 and 5 seconds.

	double time_until_network_call_is_done = get_double_between_0_and_1() * 4. + 1.;
	job_data->time_when_network_is_done = (double)clock()/(double)CLOCKS_PER_SEC + time_until_network_call_is_done;

	job_yield(job_data);
}

void compute_some_important_value(unsigned* value){
	for(int i = 0; i < 1000 * 1000 * 30; i++)
		*value += i * 2 - i / 3;
}

void job_main(void* parameter){
	auto job_data = (Job_Data*) parameter;

	job_data->has_job_started = true;

	printf("worker %d: job %d started\n", worker_data->id, job_data->id);

	unsigned value = job_data->id;

	compute_some_important_value(&value);
	start_network_call_and_yield(job_data);
	printf("worker %d: job %d finished network call 1\n", worker_data->id, job_data->id);

	compute_some_important_value(&value);
	start_network_call_and_yield(job_data);
	printf("worker %d: job %d finished network call 2\n", worker_data->id, job_data->id);

	compute_some_important_value(&value);
	start_network_call_and_yield(job_data);
	printf("worker %d: job %d finished network call 3\n", worker_data->id, job_data->id);

	compute_some_important_value(&value);

	printf("worker %d: job %d finished with value %u\n", worker_data->id, job_data->id, value % 100);
	job_data->is_job_done = true;

	job_yield(job_data);
}

DWORD WINAPI worker_main(void* parameter){
	worker_data = (Worker_Data*) parameter;

	worker_data->base_fiber = ConvertThreadToFiber(nullptr);
	assert(worker_data->base_fiber != nullptr /*ConvertThreadToFiber failed. We need to convert the base thread to a fiber first, because only a fiber can schedule other fibers.*/);

	int value;

	printf("worker %d: starting jobs\n", worker_data->id);
	while(true){
		for(int i = 0; i < number_of_jobs; i++){
			auto job = &global_job_pool[i];
			DWORD mutex_access_result = WaitForSingleObject(job->access_mutex, 0);

			if(mutex_access_result == WAIT_OBJECT_0){ //we got the mutex
				if(!job->is_job_done && (!job->has_job_started || is_network_call_done(job)))
					schedule_job(job);

				ReleaseMutex(job->access_mutex);
			}
		}

		bool all_jobs_done = true;

		for(int i = 0; i < number_of_jobs; i++)
			all_jobs_done = all_jobs_done && global_job_pool[i].is_job_done;

		if(all_jobs_done)
			break;
	}

	printf("worker %d: all jobs finished\n", worker_data->id);

	return 0;
}

void main(){
	for(int i = 0; i < number_of_jobs; i++){
		global_job_pool[i].id = i;
		global_job_pool[i].fiber = CreateFiber(2000000 /*stack size*/, &job_main /*function to start fiber in*/, &global_job_pool[i] /*parameter for fiber start function*/);
		global_job_pool[i].access_mutex = CreateMutex(NULL, 0, NULL);
	}

	HANDLE handles_to_wait_on[number_of_workers];

	Worker_Data workers[number_of_workers];

	for(int i = 0; i < number_of_workers; i++){
		workers[i].id = i;
		workers[i].thread_handle = CreateThread(NULL, 2000000, &worker_main, &workers[i], 0, NULL);
		workers[i].random_state = fmod(0.28 * (double) i, 1);
		handles_to_wait_on[i] = workers[i].thread_handle;
	}

	WaitForMultipleObjects(number_of_workers, &handles_to_wait_on[0], 1, INFINITE);

	for(int i = 0; i < number_of_jobs; i++)
		DeleteFiber(global_job_pool[i].fiber);

	printf("all jobs and workers finished\n");
}


