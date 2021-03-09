### Job-Synchronization

Job scheduler which uses a Multi-Level Feedback Queue algorithm to schedule jobs.

Jobs are continuously accepted and each receive a priority of execution.  Each thread (queue in scheduler) is synchronized to run in parallel, with jobs at the front executing.
