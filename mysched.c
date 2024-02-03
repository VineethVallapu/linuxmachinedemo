#include<stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define COMMAND 1024
#define MAXJOBS 1024
#define MAXLEN 128
#define TERMINAL '\0'

typedef struct job
{
    int jobid;
    int estatus;
    char fnout[10];
    char fnerr[10];
    char *command;
    char *status;
    char *start;
    char *end;
    pthread_t threadid;
} job;

typedef struct queue
{
    int size;
    int count;
    int start;
    int end;
    job **buffer;
} queue;

int QUEUE;
int CURRENT;
job ALLJOBS[MAXJOBS];
queue *JOBQUEUE;

char *copy_str(char *str)
{
    char *copy, c;
    int i = -1;

    copy = malloc(strlen(str));
    while ((c = str[++i]) != TERMINAL)
        copy[i] = c;
    copy[i] = TERMINAL;

    return copy;
}

job create_job(char *command, int jobid)
{
    job j;
    j.jobid = jobid;
    j.start = NULL;
    j.end = NULL;
    j.command = copy_str(command);
    j.status = "Waiting";
    j.estatus = -1;
    sprintf(j.fnout, "%d.out", j.jobid);
    sprintf(j.fnerr, "%d.err", j.jobid);
    return j;
}

int enqueue(queue *q, job *jp)
{
    if ((q != NULL) && (q->count != q->size))
    {
        q->buffer[q->end % q->size] = jp;
        q->end = (q->end + 1) % q->size;
        q->count++;
        return q->count;
    }

    return -1;
}

job *dequeue(queue *q)
{
    if ((q != NULL) && (q->count != 0))
    {
        job *j = q->buffer[q->start];
        q->start = (q->start + 1) % q->size;
        q->count--;
        return j;
    }

    return (job *)-1;
}

void print_jobs(job *jobs, int n, char *mode)
{
    if (jobs == NULL || n == 0)
    {
        return;
    }
    if (strcmp(mode, "submithistory") == 0)
    {
        printf("Job ID\tCommand\t\tstarttime\tendtime\tstatus\n");
        for (int i = 0; i < n; i++)
        {
            if (strcmp(jobs[i].status, "Success") == 0)
                printf("%d\t%s\t%s\t%s\t%s\n", jobs[i].jobid, jobs[i].command, jobs[i].start, jobs[i].end, jobs[i].status);
        }
        return;
    }
    if (strcmp(mode, "showjobs") == 0)
    {
        printf("jobid\tcommand\t\tstatus\n");
        for (int i = 0; i < n; i++)
        {
            if (strcmp(jobs[i].status, "Success") != 0)
                printf("%d\t%s\t%s\n", jobs[i].jobid, jobs[i].command, jobs[i].status);
        }
        return;
    }
}

int get_line(char *s, int n)
{
    int i = 0;
    char c;
    while (i < n - 1 && (c = getchar()) != '\n')
    {
        if (c == EOF)
            return -1;
        s[i++] = c;
    }
    s[i] = TERMINAL;
    return i;
}

char *left_strip(char *s)
{
    int i;

    i = -1;
    while (s[++i] == ' ');

    return s + i;
}

char *copy_line(char *s)
{
    int i;
    char c;
    char *copy;

    i = 0;
    copy = malloc(strlen(s));
    c = s[i];
    while (c != TERMINAL && c != '\n')
    {
        copy[i] = c;
        c = s[++i];
    }
    copy[i] = TERMINAL;

    return copy;
}

char **parse_arguments(char *line)
{
    char *copy = malloc(strlen(line) + 1);
    strcpy(copy, line);

    char *temp, **args = malloc(sizeof(char *));

    int i = 0;
    for (; (temp = strtok(copy, " ")) != NULL; i++)
    {
        args[i] = malloc(strlen(temp) + 1);
        strcpy(args[i], temp);
        args = realloc(args, sizeof(char *) * (i + 1));
        copy = NULL;
    }

    args[i] = NULL;
    
    return args;
}


void parse_input()
{
    int i = 0;
    char *kw, *command;
    char line[COMMAND];

    printf("Enter Command> ");
    while (get_line(line, COMMAND) != -1)
    {
        if ((kw = strtok(copy_str(line), " ")) != NULL)
        {
            if (!strcmp(kw, "exit"))
            {
                break;
            }
            else if (!strcmp(kw, "showjobs") || !strcmp(kw, "submithistory"))
            {
                print_jobs(ALLJOBS, i, kw);
            }
            else if (!strcmp(kw, "submit"))
            {
                command = left_strip(strstr(line, "submit") + strlen("submit"));
                ALLJOBS[i] = create_job(command, i);
                enqueue(JOBQUEUE, &ALLJOBS[i]);
                printf("Added job %d to the job queue\n", i);
                i++;
            }
        }
        printf("Enter Command> ");
    }
    kill(0, SIGINT);
}

int output_files(char *fn)
{
    int fd;
    if ((fd = open(fn, O_CREAT | O_WRONLY | O_APPEND, 0777)) == -1)
    {
        return -1;
    }
    else
    {
        return fd;
    }
}

void *execute_job(void *arg)
{
    job *jp;
    char **args;
    pid_t pid;

    time_t tim = time(NULL);
    jp = (job *)arg;

    CURRENT++;
    jp->status = "Working";
    jp->start = copy_line(ctime(&tim));

    pid = fork();
    if (pid > 0)
    {
        waitpid(pid, &jp->estatus, WUNTRACED);
        jp->status = "Success";
        tim = time(NULL);
        jp->end = copy_line(ctime(&tim));
    }
    else if (pid == 0)
    {
        dup2(output_files(jp->fnerr), 2);
        dup2(output_files(jp->fnout), 1);
        args = parse_arguments(jp->command);
        execvp(args[0], args);
        exit(0);
    }

    CURRENT--;
    return 0;
}

void *execute_jobs(void *arg)
{
    job *jp;

    while (1)
    {
        if (JOBQUEUE->count > 0)
        {
            if (CURRENT < QUEUE)
            {
                jp = dequeue(JOBQUEUE);

                pthread_create(&jp->threadid, NULL, execute_job, jp);

                pthread_detach(jp->threadid);
            }
        }
        sleep(1);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t threadid;
    JOBQUEUE = malloc(sizeof(queue));
    JOBQUEUE->start = 0;
    JOBQUEUE->end = 0;
    JOBQUEUE->count = 0;
    JOBQUEUE->size = MAXLEN;
    JOBQUEUE->buffer = malloc(sizeof(job *) * MAXLEN);

    if (argc != 2)
    {
        printf("Usage: %s [queue size]\n", argv[0]);
        return 0;
    }
    QUEUE = atoi(argv[1]);

    pthread_create(&threadid, NULL, execute_jobs, NULL);
    parse_input();

    return 0;
}