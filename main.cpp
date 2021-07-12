//compiled with VS2019 and command "cl main.cpp -Z7"
//documentation says _WIN32_WINNT must at least be 0x0400. I didn't need to define it, maybe you do, idk.

#include "Windows.h"
#include "assert.h"
#include "time.h"
#include "stdio.h"
#include "math.h"

const int number_of_workers = 4;

void* base_fiber;

double random_state = 0.4;

double get_double_between_0_and_1(){
	//the shittiest rng I have ever written, but its predictable so you can reproduce things.
	random_state = (random_state * 31.4 + 0.12) / 1.7;
	random_state = fmod(random_state, 1);
	return random_state;
}

struct Worker_Data{
	void* fiber;
	int id;
	double time_when_network_is_done;
	bool has_worker_started = false;
	bool is_worker_done = false;
};

void worker_yield(Worker_Data* worker_data){
	printf("worker %d yields\n", worker_data->id);
	SwitchToFiber(base_fiber);
	printf("worker %d resumed\n", worker_data->id);
}

void schedule_worker(Worker_Data* worker_data){
	SwitchToFiber(worker_data->fiber); //apparently stepping in the debugger won't let you enter the fiber execution. You have to actually set a breakpoint
}

bool is_network_call_done(Worker_Data* worker_data){
	return (double)clock()/(double)CLOCKS_PER_SEC > worker_data->time_when_network_is_done;
}

void start_network_call_and_yield(Worker_Data* worker_data){
	//a common use case for co-routines and fibers and the likes is that you do asynchronous network operations, but structure your code like they were synchronous. Instead of an expensive network call, we just say that the network call will take between 1 and 5 seconds.

	double time_until_network_call_is_done = get_double_between_0_and_1() * 4. + 1.;
	worker_data->time_when_network_is_done = (double)clock()/(double)CLOCKS_PER_SEC + time_until_network_call_is_done;

	worker_yield(worker_data);
}

void compute_some_important_value(int* value){
	for(int i = 0; i < 1000 * 1000 * 10; i++)
		*value += i * 2 - i / 3;
}

void worker_main(void* parameter){
	auto worker_data = (Worker_Data*) parameter;

	worker_data->has_worker_started = true;

	printf("worker %d started\n", worker_data->id);

	int value = worker_data->id;

	compute_some_important_value(&value);
	start_network_call_and_yield(worker_data);
	printf("worker %d finished network call 1\n", worker_data->id);

	compute_some_important_value(&value);
	start_network_call_and_yield(worker_data);
	printf("worker %d finished network call 2\n", worker_data->id);

	compute_some_important_value(&value);
	start_network_call_and_yield(worker_data);
	printf("worker %d finished network call 3\n", worker_data->id);

	compute_some_important_value(&value);

	printf("worker %d finished with value %d\n", worker_data->id, value % 100);
	worker_data->is_worker_done = true;

	worker_yield(worker_data);
}

int main(){
	base_fiber = ConvertThreadToFiber(nullptr);
	assert(base_fiber != nullptr /*ConvertThreadToFiber failed. We need to convert the base thread to a fiber first, because only a fiber can schedule other fibers.*/);

	Worker_Data workers[number_of_workers] = {};

	for(int i = 0; i < number_of_workers; i++){
		workers[i].id = i;
		workers[i].fiber = CreateFiber(2000000 /*stack size*/, &worker_main /*function to start fiber in*/, &workers[i] /*parameter for fiber start function*/);
		assert(base_fiber != nullptr /*Creating a fiber for a worker failed.*/);
	}

	int value;

	printf("starting workers\n");
	while(true){
		for(int i = 0; i < number_of_workers; i++){
			if(!workers[i].is_worker_done && (!workers[i].has_worker_started || is_network_call_done(&workers[i])))
				schedule_worker(&workers[i]);

			compute_some_important_value(&value); //just here so that there is a visible gap between fiber schedules in the profiler
		}

		bool all_workers_done = true;

		for(int i = 0; i < number_of_workers; i++)
			all_workers_done = all_workers_done && workers[i].is_worker_done;

		if(all_workers_done)
			break;
	}

	for(int i = 0; i < number_of_workers; i++)
		DeleteFiber(workers[i].fiber);

	printf("all workers finished\n");
}


