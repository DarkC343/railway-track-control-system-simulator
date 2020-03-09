// Code author: DarkC343
// Date: March, 2020

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define ACQUIRE_LOCK(mutex) if(pthread_mutex_lock(mutex) != 0) perror("ACQUIRE_LOCK_ERR");
#define RELEASE_LOCK(mutex) if(pthread_mutex_unlock(mutex) != 0) perror("RELEASE_LOCK_ERR");
#define WAIT_ON_CONDITION(condition, mutex) if(pthread_cond_wait(condition, mutex) != 0) perror("WAIT_ON_CONDITION_ERR");
#define SIGNAL_CONDITION(condition) if(pthread_cond_signal(condition) != 0) perror("SIGNAL_CONDITION_ERR");
#define BROADCAST_CONDITION(condition) if(pthread_cond_broadcast(condition) != 0) perror("BROADCAST_CONDITION_ERR");

enum Direction
{
    East,
    West
};

enum Priority
{
    High,
    Low
};

enum TrainStatus
{
    Undefined,
    Loading,
    Waiting,
    Crossing,
    Arrived
};

pthread_mutex_t allowed_trains_lock;
pthread_cond_t allowed_trains_cond;
unsigned int *allowed_trains_queue;

pthread_mutex_t waiting_train_count_lock;
unsigned int waiting_train_count = 0;

pthread_mutex_t last_direction_lock;
enum Direction last_direction = -1;

enum Permission
{
    Permitted,
    Prohibited
};
pthread_mutex_t permission_lock;
pthread_cond_t permission_cond;
enum Permission permission = Permitted;

struct Train
{
    unsigned int number;
    enum Direction direction;
    enum Priority priority;
    unsigned int loading_time;
    unsigned int crossing_time;
};
struct Train *train = NULL;

pthread_mutex_t railway_lock;
pthread_mutex_t railway_lock;
pthread_cond_t railway_cond;
enum RailwayStatus
{
    Free,
    Blocked
};
enum RailwayStatus railway_status = Free;

pthread_mutex_t global_time_lock;
pthread_cond_t global_time_cond;
unsigned int global_time = 0;

pthread_mutex_t crossing_time_lock;
pthread_cond_t crossing_time_cond;
unsigned int crossing_time = -1;

pthread_mutex_t trains_status_lock;
enum TrainStatus *trains_status;

void *train_func(void *);
void *global_timer_func(void *);
void *crossing_timer_func(void *);

void scheduler_func(unsigned int);
char *timestamp(void);

int main(int argc, char *argv[], char **envp)
{
    FILE *f = fopen(argv[1], "r");
    if(!f) 
    {
        perror("FILE_ERR");
        exit(EXIT_FAILURE);
    }
    
    char t_dir[2];
    unsigned int t_lt = 0, t_ct = 0, t_i = 0;
    
    while(fscanf(f, "%s %d %d", t_dir, &t_lt, &t_ct) != EOF)
    {
        train = (struct Train *) realloc(train, (t_i + 1) * sizeof(struct Train));
        if(!train)
        {
            perror("ALLOCATION_ERR");
            exit(EXIT_FAILURE);
        }
        train[t_i].number = t_i;
        if(t_dir[0] == 'e')
        {
            train[t_i].direction = East;
            train[t_i].priority = Low;
        }
        else if(t_dir[0] == 'E')
        {
            train[t_i].direction = East;
            train[t_i].priority = High;
        }
        else if(t_dir[0] == 'w')
        {
            train[t_i].direction = West;
            train[t_i].priority = Low;
        }
        else if(t_dir[0] == 'W')
        {
            train[t_i].direction = West;
            train[t_i].priority = High;
        }
        train[t_i].loading_time = t_lt;
        train[t_i].crossing_time = t_ct;
        t_i++;
    }
        
    allowed_trains_queue = (unsigned int *) malloc(t_i * sizeof(unsigned int));
    trains_status = (enum TrainStatus *) malloc(t_i * sizeof(enum TrainStatus));
    
    for(int i = 0; i < t_i; ++i)
    {
        allowed_trains_queue[i] = -1;
        trains_status[i] = Undefined;
    }
    
    pthread_mutex_init(&railway_lock, NULL);
    pthread_cond_init(&railway_cond, NULL);

    pthread_mutex_init(&global_time_lock, NULL);
    pthread_cond_init(&global_time_cond, NULL);
    
    pthread_mutex_init(&crossing_time_lock, NULL);
    pthread_cond_init(&crossing_time_cond, NULL);
        
    pthread_mutex_init(&allowed_trains_lock, NULL);
    pthread_cond_init(&allowed_trains_cond, NULL);
    
    pthread_mutex_init(&permission_lock, NULL);
    pthread_cond_init(&permission_cond, NULL);
    
    pthread_mutex_init(&trains_status_lock, NULL);
    pthread_mutex_init(&waiting_train_count_lock, NULL);
    pthread_mutex_init(&last_direction_lock, NULL);    
    
    pthread_t tr[t_i];
    pthread_t timert;
    pthread_t crossing_timert;
    for(int i = 0; i < t_i; ++i)
        pthread_create(&tr[i], NULL, train_func, &train[i]);
    pthread_create(&timert, NULL, global_timer_func, NULL);
    pthread_create(&crossing_timert, NULL, crossing_timer_func, NULL);    
    
    for(int i = 0; i < t_i; ++i)
        pthread_join(tr[i], NULL);
    
    return EXIT_SUCCESS;
}

void *train_func(void *arg)
{
    struct Train *trainPtr = (struct Train *) arg;
    trains_status[trainPtr->number] = Loading;
    
    ACQUIRE_LOCK(&global_time_lock);
    while(global_time < trainPtr->loading_time)
        WAIT_ON_CONDITION(&global_time_cond, &global_time_lock);
    printf("%s Train %2d is ready to go %4s\n", timestamp(), trainPtr->number, (trainPtr->direction == West) ? "West" : "East");
    RELEASE_LOCK(&global_time_lock);
    
    ACQUIRE_LOCK(&trains_status_lock);
    trains_status[trainPtr->number] = Waiting;
    RELEASE_LOCK(&trains_status_lock);

    scheduler_func(trainPtr->number);
    
    ACQUIRE_LOCK(&permission_lock);
    if(permission == Prohibited)
        WAIT_ON_CONDITION(&permission_cond, &permission_lock);
    RELEASE_LOCK(&permission_lock);
    
    ACQUIRE_LOCK(&allowed_trains_lock);
    while(trainPtr->number != allowed_trains_queue[0])
        WAIT_ON_CONDITION(&allowed_trains_cond, &allowed_trains_lock);
    RELEASE_LOCK(&allowed_trains_lock);
    
    ACQUIRE_LOCK(&permission_lock);
    permission = Prohibited;
    RELEASE_LOCK(&permission_lock);
    
    ACQUIRE_LOCK(&railway_lock);
    while(railway_status == Blocked)
    {
        WAIT_ON_CONDITION(&railway_cond, &railway_lock);
    }
    RELEASE_LOCK(&railway_lock);
    
    ACQUIRE_LOCK(&last_direction_lock);
    last_direction = trainPtr->direction;
    RELEASE_LOCK(&last_direction_lock);
    
    ACQUIRE_LOCK(&trains_status_lock);
    trains_status[trainPtr->number] = Crossing;
    RELEASE_LOCK(&trains_status_lock);
    printf("%s Train %2d is ON the main track going %4s\n", timestamp(), trainPtr->number, (trainPtr->direction == West) ? "West" : "East");

    ACQUIRE_LOCK(&railway_lock);
    railway_status = Blocked;
    RELEASE_LOCK(&railway_lock);
    
    ACQUIRE_LOCK(&waiting_train_count_lock);
    for(int i = 1; i < waiting_train_count; ++i)
            allowed_trains_queue[i-1] = allowed_trains_queue[i];
    waiting_train_count--;
    RELEASE_LOCK(&waiting_train_count_lock);

    ACQUIRE_LOCK(&crossing_time_lock);
    crossing_time++; // set to 0
    SIGNAL_CONDITION(&crossing_time_cond);
    while(crossing_time < trainPtr->crossing_time)
    {
        WAIT_ON_CONDITION(&crossing_time_cond, &crossing_time_lock);
    }
    crossing_time = -1;
    printf("%s Train %2d is OFF the main track after going %4s\n", timestamp(), trainPtr->number, (trainPtr->direction == West) ? "West" : "East");
    RELEASE_LOCK(&crossing_time_lock);
    
    ACQUIRE_LOCK(&railway_lock);
    railway_status = Free;
    SIGNAL_CONDITION(&railway_cond);
    RELEASE_LOCK(&railway_lock);
    
    ACQUIRE_LOCK(&allowed_trains_lock);
    BROADCAST_CONDITION(&allowed_trains_cond);
    RELEASE_LOCK(&allowed_trains_lock);
    
    ACQUIRE_LOCK(&permission_lock);
    permission = Permitted;
    BROADCAST_CONDITION(&permission_cond);
    RELEASE_LOCK(&permission_lock);
    
    pthread_exit(0);
}

void *global_timer_func(void *arg)
{
    while(1)
    {
        ACQUIRE_LOCK(&global_time_lock);
        usleep(1e5);
        global_time++;
        //printf("- timer: %.1f\n", (float) global_time / 10);
        BROADCAST_CONDITION(&global_time_cond);
        RELEASE_LOCK(&global_time_lock);
        usleep(1);
    }
}

void *crossing_timer_func(void *arg)
{
    while(1)
    {
        ACQUIRE_LOCK(&crossing_time_lock);
        if(crossing_time == -1) WAIT_ON_CONDITION(&crossing_time_cond, &crossing_time_lock);
        usleep(1e5);
        crossing_time++;
        //printf("+ crossing timer: %.1f\n", (float) crossing_time / 10);
        BROADCAST_CONDITION(&crossing_time_cond);
        RELEASE_LOCK(&crossing_time_lock);
        usleep(1);
    }
}

void scheduler_func(unsigned int train_number)
{
    int k = 0, l = 0;
    ACQUIRE_LOCK(&waiting_train_count_lock);
    waiting_train_count++;
    if(waiting_train_count == 1)
    {
        ACQUIRE_LOCK(&allowed_trains_lock);
        allowed_trains_queue[0] = train_number;
        RELEASE_LOCK(&allowed_trains_lock);
    }
    else
    {
        ACQUIRE_LOCK(&allowed_trains_lock);
        k = 0, l = 0;
        for(k = 0; k < waiting_train_count; ++k)
        {
            if(k == waiting_train_count - 1)
            {
                allowed_trains_queue[k] = train_number;
                break;
            }
            if(train[allowed_trains_queue[k]].priority == train[train_number].priority)
            {
                if(train[allowed_trains_queue[k]].direction == train[train_number].direction)
                {
                    if(train[allowed_trains_queue[k]].loading_time > train[train_number].loading_time)
                    {
                        for(l = waiting_train_count; l > 0; --l)
                            allowed_trains_queue[l] = allowed_trains_queue[l-1];
                        allowed_trains_queue[0] = train_number;
                        break;
                    }
                    else if(train[allowed_trains_queue[k]].loading_time < train[train_number].loading_time)
                        continue;
                    else
                    {
                        if(train[allowed_trains_queue[k]].number > train[train_number].number)
                        {
                            for(l = waiting_train_count; l > 0; --l)
                                allowed_trains_queue[l] = allowed_trains_queue[l-1];
                            allowed_trains_queue[0] = train_number;
                            break;
                        }
                        else continue;
                    }
                }
                else
                {
                    if(last_direction == train[allowed_trains_queue[k]].direction)
                    {
                        for(l = waiting_train_count; l > 0; --l)
                            allowed_trains_queue[l] = allowed_trains_queue[l-1];
                        allowed_trains_queue[0] = train_number;
                        break;
                    }
                    else continue;
                }
            }
            else
            {
                if(train[train_number].priority == High)
                {
                    for(l = waiting_train_count; l > 0; --l)
                        allowed_trains_queue[l] = allowed_trains_queue[l-1];
                    allowed_trains_queue[0] = train_number;
                    break;
                }
                else continue;
            }
        }
        RELEASE_LOCK(&allowed_trains_lock);
    }
    RELEASE_LOCK(&waiting_train_count_lock);
}

char *timestamp(void)
{
    char *ts = (char *) malloc(sizeof(char) * 11);
    sprintf(ts, "%02d:%02d:%02d.%01d", ((global_time / 10) / 3600) % 60, ((global_time / 10) / 60) % 60, (global_time / 10) % 60, global_time % 10);
    return ts;
}
