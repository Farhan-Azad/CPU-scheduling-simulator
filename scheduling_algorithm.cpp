/*
 * Farhan Azad
 * Implements three scheduling algorithms using POSIX Threads (pthreads):
 *   1. First-Come, First-Served (FCFS)
 *   2. Shortest-Job-First (SJF)
 *   3. Preemptive Priority Scheduling (PS)
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <random>
#include <pthread.h>
using namespace std;

// Data Types

struct Task {
    string name;
    int priority; 
    int burst;      // CPU burst time
    int arrival{0}; // used by PS
};

// making sure that task ran from start to end
struct Segment {
    string task_name;
    int start;
    int end;
};

struct Result {
    vector<Segment> segments;
    double avg_wait{0.0};
    double avg_turnaround{0.0};
    vector<int> arrivals; // storing the PS arrival times
};

vector<Task> tasks;   // loaded once by main, read-only in FCFS/SJF

Result fcfs_result;
Result sjf_result;
Result ps_result;
// basically just used the structure of result to store the results of all 3 algorithms

// just a simple file reader to use for reading input.txt
void read_tasks(const string &filename) {
    ifstream f(filename);
    if (!f.is_open()) {
        cerr << "Cannot open " << filename << "\n";
        exit(EXIT_FAILURE);
    }
    Task t;
    while (f >> t.name >> t.priority >> t.burst) { //adding them in the order of name, priority and burst time
        tasks.push_back(t);
    }
    if (tasks.empty()) {
        cerr << "No tasks found in " << filename << "\n";
        exit(EXIT_FAILURE);
    }
}

// making this so that I don't have to keep using push_back with the parameters every time

static void add_segment(Result &r, const string &name, int start, int end) {
    r.segments.push_back({name, start, end});
}

/* FCFS algorithm:
        everything starts at t=0 and are served according the order from the input file.
 */
void *fcfs(void *arg) {
    Result &r = *static_cast<Result *>(arg);

    int time = 0;
    double total_wait = 0.0, total_turnaround = 0.0;

    for (const Task &task : tasks) {
        // Waiting time = time CPU assigned - arrival time (fyi everything arrive at 0)
        total_wait += time;
        total_turnaround = total_turnaround + time + task.burst;

        add_segment(r, task.name, time, time + task.burst);
        time += task.burst;
    }

    int n = static_cast<int>(tasks.size());
    r.avg_wait = total_wait / n;
    r.avg_turnaround = total_turnaround / n;
    return nullptr; // since we got a void* function, wwe need return
}

/* SJF:
        Sorts by burst time
        Ties in burst are broken randomly
        shuffle before sorting so that equal-burst tasks land in a random order
 */
void *sjf(void *arg) {
    Result &r = *static_cast<Result *>(arg);

    // making a local copy for this algo because i don't want to mess with the shared vector
    vector<Task> sorted = tasks;

    // Shuffle first to randomize all the ties
    mt19937 rng(static_cast<unsigned>(time(nullptr)));
    shuffle(sorted.begin(), sorted.end(), rng);

    // sort by burst
    stable_sort(sorted.begin(), sorted.end(), [](const Task &a, const Task &b) {
        return a.burst < b.burst; 
    }

);

    int time = 0;
    double total_wait = 0.0, total_turnaround = 0.0;

    for (const Task &task : sorted) {
        total_wait += time;
        total_turnaround = total_turnaround + time + task.burst;

        add_segment(r, task.name, time, time + task.burst);
        time += task.burst;
    }

    int n = static_cast<int>(sorted.size());
    r.avg_wait = total_wait / n;
    r.avg_turnaround = total_turnaround / n;
    return nullptr;
}

/* PS:
        Each task gets a random arrival time in [0, 100] ms.
        The simulation runs 1 ms at a time. At each tick the highest-priority task that has arrived (and has remaining burst) is selected. Ties in priority are broken randomly.
 */
void *PS(void *arg) {
    Result &r = *static_cast<Result *>(arg);

    //making sur that PS have different sequence than SJF
    srand(static_cast<unsigned>(time(nullptr)) ^
          static_cast<unsigned long>(pthread_self()));

    int n = static_cast<int>(tasks.size());

    // once again, making a local copy with random arrival times assigned
    vector<Task> local = tasks;
    r.arrivals.resize(n);

    for (int i = 0; i < n; i++) {
        local[i].arrival = rand() % 101;  // keeping between [0, 100] ms
        r.arrivals[i] = local[i].arrival;
    }

    // stroing the remaining burst and completion time per task
    vector<int> remaining(n);
    vector<int> completion(n, -1);
    for (int i = 0; i < n; i++) remaining[i] = local[i].burst;

    int completed = 0;
    int cur_task  = -1;   // index of task on CPU, -1 = idle
    int seg_start = 0;

    // we gurntee finish by doing max arrival + total burst
    int total_burst = accumulate(local.begin(), local.end(), 0, [](int sum, const Task &t) { 
        return sum + t.burst; 
    });
    int sim_end = 100 + total_burst + 1;

    for (int t = 0; t < sim_end && completed < n; t++) {

        // helps me find the best ready task at every tick
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (local[i].arrival > t || remaining[i] <= 0) continue;

            if (best == -1) {
                best = i;
            } 
            else if (local[i].priority < local[best].priority) {
                best = i;
            } 
            else if (local[i].priority == local[best].priority) {
                //if they have the same priority, we do ennie minnie minie moe and choose one randomly
                if (rand() % 2 == 0) best = i;
            }
        }

        //to detect context switch
        if (best != cur_task) {
            if (cur_task != -1) {
                //close the outgoing segment
                add_segment(r, local[cur_task].name, seg_start, t);
            }
            seg_start = t;
            cur_task  = best;
        }

        if (best == -1) continue; // CPU idle this tick

        //run the chosen task for 1 ms 
        remaining[best]--;

        if (remaining[best] == 0) {
            //close segment right away after task is finished
            completion[best] = t + 1;
            completed++;
            add_segment(r, local[best].name, seg_start, t + 1);
            cur_task = -1; //this will force new segment evaluation next tick
        }
    }

    //thi will compute per-task wait and turnaround, and then average
    double total_wait = 0.0, total_turnaround = 0.0;
    for (int i = 0; i < n; i++) {
        int turnaround = completion[i] - local[i].arrival;
        int wait = turnaround - local[i].burst;
        total_wait += wait;
        total_turnaround += turnaround;
    }
    r.avg_wait = total_wait / n;
    r.avg_turnaround = total_turnaround / n;
    return nullptr;
}

//gotta print the results so i will create a function for that

void print_result(const string &algo_name, const Result &r, bool show_arrivals) {
    cout << "\n" << algo_name << ":\n";

    if (show_arrivals) {
        cout << "Arrival Times: ";
        for (int i = 0; i < static_cast<int>(tasks.size()); i++) {
            cout << tasks[i].name << " = " << r.arrivals[i];
            if (i < static_cast<int>(tasks.size()) - 1) cout << ", ";
        }
        cout << endl;
    }

    // Gantt chart
    for (int i = 0; i < static_cast<int>(r.segments.size()); i++) {
        const Segment &s = r.segments[i];
        cout << s.task_name << " [" << s.start << " - " << s.end << "]";
        if (i < static_cast<int>(r.segments.size()) - 1) cout << ", ";
    }
    cout << endl;

    cout << fixed << setprecision(2);
    cout << "Avg. waiting time:    " << r.avg_wait << "\n";
    cout << "Avg. turnaround time: " << r.avg_turnaround << "\n";
}


int main() {
    //random for SJF tie-breaking)
    srand(static_cast<unsigned>(time(nullptr)));

    read_tasks("input.txt");

    cout << "Task Set:\n";
    cout << left
              << setw(8)  << "Task"
              << setw(10) << "Priority"
              << setw(10) << "Burst(ms)" << "\n";
    for (const Task &t : tasks) {
        cout << setw(8)  << t.name
                  << setw(10) << t.priority
                  << setw(10) << t.burst << "\n";
    }

    //spawn one thread per algorithm
    pthread_t t_fcfs, t_sjf, t_ps;

    pthread_create(&t_fcfs, nullptr, fcfs, &fcfs_result);
    pthread_create(&t_sjf, nullptr, sjf, &sjf_result);
    pthread_create(&t_ps, nullptr, PS, &ps_result);

    //obviously have to wait for all three to finish
    pthread_join(t_fcfs, nullptr);
    pthread_join(t_sjf,  nullptr);
    pthread_join(t_ps,   nullptr);

    // finally using the function i made to print all results
    print_result("FCFS", fcfs_result, false);
    print_result("SJF",  sjf_result,  false);
    print_result("PS",   ps_result,   true);

    return 0;
}

 /* 
        Compile:  g++ -Wall -std=c++17 -o scheduling_algorithm scheduling_algorithm.cpp -lpthread
        Run:     ./scheduling_algorithm
 */