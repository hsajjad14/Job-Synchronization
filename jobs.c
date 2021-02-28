// ------------
// This code is provided solely for the personal and private use of
// students taking the CSC369H5 course at the University of Toronto.
// Copying for purposes other than this use is expressly prohibited.
// All forms of distribution of this code, whether as given or with
// any changes, are expressly prohibited.
//
// Authors: Bogdan Simion
//
// All of the files in this directory and all subdirectories are:
// Copyright (c) 2019 Bogdan Simion
// -------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "executor.h"

extern struct executor tassadar;


/**
 * Populate the job lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <type> <num_resources> <resource_id_0> <resource_id_1> ...
 *
 * Each job is added to the queue that corresponds with its job type.
 */
void parse_jobs(char *file_name) {
    int id;
    struct job *cur_job;
    struct admission_queue *cur_queue;
    enum job_type jtype;
    int num_resources, i;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*) &jtype, (int*) &num_resources) == 3) {

        /* construct job */
        cur_job = malloc(sizeof(struct job));
        cur_job->id = id;
        cur_job->type = jtype;
        cur_job->num_resources = num_resources;
        cur_job->resources = malloc(num_resources * sizeof(int));

        int resource_id;
		for(i = 0; i < num_resources; i++) {
		    fscanf(f, "%d ", &resource_id);
		    cur_job->resources[i] = resource_id;
		    tassadar.resource_utilization_check[resource_id]++;
		}

		assign_processor(cur_job);

        /* append new job to head of corresponding list */
        cur_queue = &tassadar.admission_queues[jtype];
        cur_job->next = cur_queue->pending_jobs;
        cur_queue->pending_jobs = cur_job;
        cur_queue->pending_admission++;
    }

    fclose(f);
}

/*
 * Magic algorithm to assign a processor to a job.
 */
void assign_processor(struct job* job) {
    int i, proc = job->resources[0];
    for(i = 1; i < job->num_resources; i++) {
        if(proc < job->resources[i]) {
            proc = job->resources[i];
        }
    }
    job->processor = proc % NUM_PROCESSORS;
}


void do_stuff(struct job *job) {
    /* Job prints its id, its type, and its assigned processor */
    printf("%d %d %d\n", job->id, job->type, job->processor);
}



/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the executor
 * before any jobs start coming
 *
 */
void init_executor() {

    int i;
    for(i = 0; i < NUM_RESOURCES; i++){
        pthread_mutex_t resource_lock = PTHREAD_MUTEX_INITIALIZER;
        tassadar.resource_locks[i] = resource_lock;
        tassadar.resource_utilization_check[i] = 0;
    }

    for(i = 0; i < NUM_QUEUES; i++){
        pthread_mutex_t admission_queue_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t admission_queue_admission_cv = PTHREAD_COND_INITIALIZER;
        pthread_cond_t admission_queue_execution_cv = PTHREAD_COND_INITIALIZER;
        tassadar.admission_queues[i].lock = admission_queue_lock;
        tassadar.admission_queues[i].admission_cv = admission_queue_admission_cv;
        tassadar.admission_queues[i].execution_cv = admission_queue_execution_cv;
        // not sure if we need to malloc pending_jobs b/c parse_jobs does it
        //tassadar.admission_queues[i].pending_jobs = malloc(sizeof(struct job));
        tassadar.admission_queues[i].pending_admission = 0;
        tassadar.admission_queues[i].admitted_jobs = malloc(QUEUE_LENGTH * sizeof(struct job *));
        tassadar.admission_queues[i].capacity = QUEUE_LENGTH;
        tassadar.admission_queues[i].num_admitted = 0;
        tassadar.admission_queues[i].head = 0;
        tassadar.admission_queues[i].tail = 0;
    }

    for(i = 0; i < NUM_PROCESSORS; i++){
        pthread_mutex_t processor_record_lock = PTHREAD_MUTEX_INITIALIZER;
        tassadar.processor_records[i].lock = processor_record_lock;
        tassadar.processor_records[i].completed_jobs = NULL;
        tassadar.processor_records[i].num_completed = 0;
    }
}


/**
 * TODO: Fill in this function
 *
 * Handles an admission queue passed in through the arg (see the executor.c file).
 * Bring jobs into this admission queue as room becomes available in it.
 * As new jobs are added to this admission queue (and are therefore ready to be taken
 * for execution), the corresponding execute thread must become aware of this.
 *
 */
void *admit_jobs(void *arg) {
    struct admission_queue *q = arg;

    // This is just so that the code compiles without warnings
    // Remove this line once you implement this function
    //q = q;

    //go through linked list of pending_jobs and add it to admit_jobs
    while(1){

        // lock so single admission_queue is being filled at once
        pthread_mutex_lock(&(q->lock));
        if (q->pending_admission == 0){
            pthread_mutex_unlock(&(q->lock));
            break;
        }
        // check if admission_queue has space
        // if it doesn't unlock and wait for a job to be executed
        while(q->num_admitted == q->capacity){
            pthread_cond_wait(&(q->execution_cv), &(q->lock));
        }

        // space available to add to admit_jobs queue, add it
        // struct job *to_admit, *temp;
        // if (q->pending_admission == 1){
        //     to_admit = q->pending_jobs;
        // } else {
        //     temp = q->pending_jobs;
        //     for (int i = 1; i < q->pending_admission-1; i++) {
        //         temp = temp->next;
        //     }
        //     to_admit = temp->next;
        //     temp->next=NULL;
        // }
        q->pending_admission--;

        q->admitted_jobs[q->tail] = q->pending_jobs;
        q->tail = (q->tail + 1) % q->capacity;
        q->num_admitted++;
        q->pending_jobs = q->pending_jobs->next;

        // signal that a new job was admitted
        pthread_cond_signal(&(q->admission_cv));
        pthread_mutex_unlock(&(q->lock));
    }
    return NULL;
}


/**
 * TODO: Fill in this function
 *
 * Moves jobs from a single admission queue of the executor.
 * Jobs must acquire the required resource locks before being able to execute.
 * A job will be assigned a processor id equal to its last required resource
 * from the jobs file.
 *
 * Note: You do not need to spawn any new threads in here to simulate the processors.
 * When a job acquires all its required resources, it will execute do_stuff.
 * When do_stuff is finished, the job is considered to have completed.
 *
 * Once a job has completed, the admission thread must be notified since room
 * just became available in the queue. Be careful to record the job's completion
 * on its assigned processor and keep track of resources utilized.
 *
 * Note: No printf statements are allowed in your final jobs.c code,
 * other than the one from do_stuff!
 */
void *execute_jobs(void *arg) {
    struct admission_queue *q = arg;

    // This is just so that the code compiles without warnings
    // Remove this line once you implement this function
    q = q;
    while(1){
        pthread_mutex_lock(&(q->lock));
        //If the admission queue is done executing, stop the thread
        if (q->num_admitted + q->pending_admission == 0)
        {
            pthread_mutex_unlock(&(q->lock));
            break;
        }

        // if admission_queue is empty, wait for admit_jobs
        while(q->num_admitted == 0){
            pthread_cond_wait(&(q->admission_cv), &(q->lock));
        }

        //get first job in admission_queue
        struct job *to_exec = q->admitted_jobs[q->head];
        int i;
        int resource_id;
        for(i = 0; i < to_exec->num_resources; i++){
            //if resources is needed and available get it and lock
            // else wait
            resource_id = to_exec->resources[i];
            pthread_mutex_lock(&(tassadar.resource_locks[resource_id]));
            //printf("Using resource %d for job %d \n",i,to_exec->id);
            tassadar.resource_utilization_check[resource_id]--;
        }
        // now all resources are acquired
        do_stuff(to_exec);

        //free resources so other admission queues can use them
        for(i = 0; i < to_exec->num_resources; i++){
            resource_id = to_exec->resources[i];
            pthread_mutex_unlock(&(tassadar.resource_locks[resource_id]));
        }

        // add to process record
        pthread_mutex_lock(&(tassadar.processor_records[to_exec->processor].lock));


        if (tassadar.processor_records[to_exec->processor].num_completed == 0){
            tassadar.processor_records[to_exec->processor].completed_jobs = to_exec;
        } else {
           struct job *cur = tassadar.processor_records[to_exec->processor].completed_jobs;
           for(i = 1; i < tassadar.processor_records[to_exec->processor].num_completed; i++){
               cur = cur->next;
           }
           cur->next = to_exec;
           cur->next->next = NULL;
        }
        tassadar.processor_records[to_exec->processor].num_completed++;

        pthread_mutex_unlock(&(tassadar.processor_records[to_exec->processor].lock));

        // signal admission queue, ready to add more jobs
        pthread_cond_signal(&(q->execution_cv));
        q->num_admitted--;
        q->head = (q->head + 1) % q->capacity;
        // done, unlock all resources
        pthread_mutex_unlock(&(q->lock));
    }
    free(q->admitted_jobs);
    return NULL;
}
