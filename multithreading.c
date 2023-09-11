/******* HEADERS ********/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

/********* VARIABLES ***********/

#define MAX_LEN 500

// states for student's parameter curr_state

#define WAITING_SLOT 0       // 0 if waiting for the slot to be allocated
#define ALLOCATED_SLOT 1     // 1 if allocated the slot for the tutorial
#define ATTENDING_TUTORIAL 2 // 2 if attending the tutorial
#define COURSE_FINALISED 3   // 3 if likes the course and has finished the course

/******** COLORS ********/

#define ANSI_RED "\033[1;31m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_RESET "\x1b[0m"

/********* STRUCT DEFINATIONS *********/

struct ta
{
    int ta_id;             // TA id
    int lab_id;            // Lab id to which the TA belongs
    int number_of_turns;   // number of tutorials taken by the TA
    int is_taking_tut;     // 1 if tut is ongoing else 0
    pthread_mutex_t mutex; // mutex for TA
};

struct labs
{
    int lab_id;            // id of the lab
    char name[MAX_LEN];    // name of the lab
    int num_tas;           // total TAs in the lab
    struct ta TA[MAX_LEN]; // store the TA objects
    int max_taships;       // max TAs
    int num_tut;           // number of tutorials taken by the lab , used for checking the fact if all the TAs have finished their quota of tutorials
    pthread_mutex_t mutex; // mutex
};

struct course
{
    /*** INPUT VARIABLES FOR THE COURSE ***/

    int id;             // course id
    char name[MAX_LEN]; // course name
    float interest;     // interest quotient of the course
    int num_labs;       // number of labs eligible for giving TAs
    int labs[MAX_LEN];  // store the labs ids
    int max_slots;      // maximum number of slots a TA can take up

    /*** SELF_DEFINED VARIABLES , SHOULD BE INITIALISED ***/

    bool look_ta;          // 1 if looking for a TA else 0
    bool is_active;        // to check if course has no eligible TA , is_active is set to 0 ( or false )
    pthread_t thread_idx;  //  thread to simulate courses
    pthread_cond_t allot;  //  conditional variables , for taking up the tutorial
    pthread_mutex_t mutex; // mutex

    int slots;        // total slots created
    int slots_filled; // available slots
    int curr_ta;      // the current TA idx
    int curr_lab;     // the current lab , from which TA was alloted
};

struct student
{
    int id;             // id of the student
    int curr_pref;      // 0 , 1 , or 2
    int course_pref[3]; // the three preference courses
    int time_to_fill;   // store the time to fill in the preferences
    float callibre;     // to store the current callibre of the student

    int curr_state; // -1 if waiting for the slot
                   // 0 if slot allocated
                   // 1 if attending tutorial
                   // 2 if course_finalised

    pthread_t thread_idx;
    pthread_mutex_t mutex; // mutex lock
    pthread_cond_t std;    // for student to be waiting for a tutorial slot to be assigned
                           // so that the student thread does not end up doing busy waiting
};

/***** LIST OBJECTS ******/

struct labs labs_list[MAX_LEN];
struct course course_list[MAX_LEN];
struct student student_list[MAX_LEN];
bool simulation;
int num_students, num_labs, num_courses;

/**************** HELPER FUNCTIONS ***********************/

int random_int_in_range(int lower_limit, int upper_limit)
{
    int range = upper_limit - lower_limit + 1;
    int random_num = rand() % range + lower_limit;
    return random_num;
}

void check(char *str, int val)
{
    if (!val)
        printf(ANSI_RED "%s can not have value NIL\nEXITING SIMULATION\n" ANSI_RESET, str);
}

int take_input()
{

    // get the number of students , labs , courses

    scanf("%d %d %d", &num_students, &num_labs, &num_courses);

    if (!num_courses || !num_labs || !num_courses)
    {
        check("num_courses", num_courses);
        check("num_labs", num_labs);
        check("num_students", num_students);
        return -1;
    }

    // courses

    for (int i = 0; i < num_courses; i++)
    {
        // 0 based indexing for course_list

        // initialise course object variables

        course_list[i].id = i;
        course_list[i].is_active = true;
        course_list[i].slots = 0;
        course_list[i].slots_filled = -1;
        course_list[i].curr_ta = -1;
        course_list[i].curr_lab = -1;
        course_list[i].look_ta = true;
        int ret = pthread_mutex_init(&(course_list[i].mutex), NULL);
        assert(!ret);
        ret = pthread_cond_init(&(course_list[i].allot), NULL);
        assert(!ret);

        scanf("%s %f %d %d", course_list[i].name, &course_list[i].interest, &course_list[i].max_slots, &course_list[i].num_labs);

        for (int j = 0; j < course_list[i].num_labs; j++)
        {
            scanf("%d", &course_list[i].labs[j]);
        }
    }

    // students

    for (int i = 0; i < num_students; i++)
    {
        // 0 based indexing for student_list
        // initialise the student object variables
        student_list[i].id = i;
        student_list[i].curr_pref = 0;
        student_list[i].curr_state = WAITING_SLOT;

        int ret = pthread_cond_init(&(student_list[i].std), NULL);
        assert(!ret);
        ret = pthread_mutex_init(&(student_list[i].mutex), NULL);
        assert(!ret);
        scanf("%f %d %d %d %d", &student_list[i].callibre, &student_list[i].course_pref[0], &student_list[i].course_pref[1], &student_list[i].course_pref[2], &student_list[i].time_to_fill);
    }

    // labs

    for (int i = 0; i < num_labs; i++)
    {
        labs_list[i].lab_id = i;
        labs_list[i].num_tut = 0;

        int ret = pthread_mutex_init(&(labs_list[i].mutex), NULL);
        assert(!ret);

        scanf("%s %d %d", labs_list[i].name, &labs_list[i].num_tas, &labs_list[i].max_taships);

        for (int j = 0; j < labs_list[i].num_tas; j++)
        {
            labs_list[i].TA[j].ta_id = j;
            labs_list[i].TA[j].number_of_turns = 0;
            labs_list[i].TA[j].lab_id = i;
            labs_list[i].TA[j].is_taking_tut = 0;
            ret = pthread_mutex_init(&(labs_list[i].TA[j].mutex), NULL);
            assert(!ret);
        }
    }
    return 0;
}

float probability(float interest, float callibre)
{
    return interest * callibre;
}

/************** THREAD FUNCTIONS ************************/

int assign_tas(int course_idx)
{
    int lab_count = course_list[course_idx].num_labs;
    int flag_cnt = 0;
    for (int i = 0; i < lab_count; i++)
    {
        int curr_lab = course_list[course_idx].labs[i];
        pthread_mutex_lock(&labs_list[curr_lab].mutex);
        int max_TAships = labs_list[curr_lab].max_taships;
        int num_TAs = labs_list[curr_lab].num_tas;

        if (labs_list[curr_lab].num_tut == num_TAs * max_TAships)
        {
            flag_cnt++;
            pthread_mutex_unlock(&labs_list[curr_lab].mutex);
            continue;
        }
        // there is some lab that has TAs that could be assigned
        pthread_mutex_unlock(&labs_list[curr_lab].mutex);
        for (int j = 0; j < num_TAs; j++)
        {
            pthread_mutex_lock(&labs_list[curr_lab].TA[j].mutex);
            if (labs_list[curr_lab].TA[j].number_of_turns < max_TAships && labs_list[curr_lab].TA[j].is_taking_tut == 0)
            {
                // got the TA that can be assigned
                // update the variables
                labs_list[curr_lab].TA[j].is_taking_tut = 1;
                labs_list[curr_lab].TA[j].number_of_turns++;

                // update course details
                course_list[course_idx].curr_lab = curr_lab;
                course_list[course_idx].curr_ta = j;
                course_list[course_idx].look_ta = false;

                // update the number of tutorial of the lab , ( have to keep a lock as , it is accessed by multiple courses at once , to avoid race condition )
                pthread_mutex_lock(&labs_list[curr_lab].mutex);
                labs_list[curr_lab].num_tut++;
                pthread_mutex_unlock(&labs_list[curr_lab].mutex);
                printf("TA %d from lab %s has been allocated to course %s for %d TA ship\n", j, labs_list[curr_lab].name, course_list[course_idx].name, labs_list[curr_lab].TA[j].number_of_turns);
                pthread_mutex_unlock(&labs_list[curr_lab].TA[j].mutex);
                return 1;
            }
            pthread_mutex_unlock(&labs_list[curr_lab].TA[j].mutex);
        }
    }
    if (flag_cnt == lab_count)
        return -1; // no more TAs could be assigner to the course , withdraw it
    return 0;      // Tas could be assigned  , currentlt TAs might be busy taking up some tutorial
}

int assign_slots(int course_idx)
{
    //  printf(ANSI_RED "IN assign_slots %s ######################\n" ANSI_RESET, course_list[course_idx].name);
    int slots_filled = 0;
    int max_slots = course_list[course_idx].slots;
    // while (slots_filled == 0 && slots_filled < max_slots) // loop until atleast one slot is filled
    {
        // loop over the list of students and look for any student that could be allocated to the course
        for (int i = 0; i < num_students; i++)
        {
            if (student_list[i].course_pref[student_list[i].curr_pref] == course_idx && student_list[i].curr_state == WAITING_SLOT)
            {
                if (slots_filled == max_slots)
                {
                    pthread_mutex_unlock(&student_list[i].mutex);
                    break;
                }
                student_list[i].curr_state = ALLOCATED_SLOT;

                int x = pthread_cond_signal(&(student_list[i].std));
                assert(!x);

                slots_filled++;
                // tutorial slot been assigned to the students
            }
        }
    }

    pthread_mutex_lock(&course_list[course_idx].mutex);
    course_list[course_idx].slots_filled = slots_filled;
    pthread_mutex_unlock(&course_list[course_idx].mutex);
    // print message
    return 0;
}

int conduct_tutorial(int course_idx)
{
    // printf(ANSI_RED "IN conduct_tutorial %s #######################\n" ANSI_RESET, course_list[course_idx].name);

    int curr_lab_idx = course_list[course_idx].curr_lab;
    int curr_ta_idx = course_list[course_idx].curr_ta;
    printf(ANSI_YELLOW "Tutorial has started for Course %s with %d seats filled out of %d\n" ANSI_RESET, course_list[course_idx].name, course_list[course_idx].slots_filled, course_list[course_idx].slots);

    for (int i = 0; i < num_students; i++)
    {

        if (student_list[i].course_pref[student_list[i].curr_pref] == course_idx && student_list[i].curr_state == ALLOCATED_SLOT)
        {
            pthread_mutex_lock(&student_list[i].mutex);
            student_list[i].curr_state = ATTENDING_TUTORIAL;
            pthread_mutex_unlock(&student_list[i].mutex);
        }

        // done the tutorial now , reset the variables
    }

    sleep(1);
    printf(ANSI_YELLOW "TA %d from lab %s has completed the tutorial for course %s\n" ANSI_RESET, curr_ta_idx, labs_list[curr_lab_idx].name, course_list[course_idx].name);

    int br = pthread_cond_broadcast(&(course_list[course_idx].allot));
    assert(!br);

    // changed the tutorial session
    // all the students have been notified

    pthread_mutex_lock(&course_list[course_idx].mutex);
    course_list[course_idx].slots_filled = 0;
    course_list[course_idx].curr_lab = -1;
    course_list[course_idx].curr_ta = -1;
    course_list[course_idx].look_ta = true;
    course_list[course_idx].slots = 0;
    pthread_mutex_unlock(&course_list[course_idx].mutex);

    pthread_mutex_lock(&labs_list[curr_lab_idx].TA[curr_ta_idx].mutex);
    labs_list[curr_lab_idx].TA[curr_ta_idx].is_taking_tut = false;
    pthread_mutex_unlock(&labs_list[curr_lab_idx].TA[curr_ta_idx].mutex);

    return 0;
}

void *course_thread(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    printf(ANSI_YELLOW "Course %s is active\n" ANSI_RESET, course_list[idx].name);

    while (simulation)
    {
        // first look for the TAs
        // secondly assign the slots
        // third conduct the tutorial
        if (course_list[idx].look_ta)
        {
            int flag = assign_tas(idx);
            while (flag == 0)
            {
                // keep looking for the TAs until u find one
                flag = assign_tas(idx);
            }
            // done allocating the TA ~ the first part
            if (flag == 1)
            {
                // got the TA allocate the slots
                course_list[idx].slots_filled = 0;
                course_list[idx].slots = random_int_in_range(1, course_list[idx].max_slots);
                // ~ allocated the slots for the course , done the second part
                // it is a blocking function , waits until there is atleast one student avaialble for the tut to be conducted
                assign_slots(idx);
                // got atleast one student , now conduct the tutorial
                conduct_tutorial(idx);
            }
            else
            {
                // remove the course and signal the waiting students if any
                pthread_mutex_lock(&course_list[idx].mutex);
                course_list[idx].is_active = false;
                pthread_mutex_unlock(&course_list[idx].mutex);
                for (int i = 0; i < num_students; i++)
                {
                    if (student_list[i].course_pref[student_list[i].curr_pref] == idx)
                    {
                        int br = pthread_cond_signal(&student_list[i].std);
                        assert(!br);
                    }
                }

                break;
            }
        }
    }

    if (!course_list[idx].is_active)
        printf(ANSI_RED "Course %s doesn’t have any TA’s eligible and is removed from course offerings\n" ANSI_RESET, course_list[idx].name);

    return NULL;
}

void *student_thread(void *arg)
{
    // get the student id as the argument
    int idx = *(int *)arg;
    free(arg);
    // sleep until students fills in the preferences
    sleep(student_list[idx].time_to_fill);
    // print statement
    printf(ANSI_MAGENTA "Student %d has filled in preferences for course registration\n" ANSI_RESET, idx);

    // variable to keep track of current choice of coourse [ 0 , 1 , 2 ]
    // 0 based indexing
    int choice = 0;

    while (choice < 3)
    {
        pthread_mutex_lock(&student_list[idx].mutex);
        // initialise the variables , current choice of course
        int curr_course_idx = student_list[idx].course_pref[choice];
        student_list[idx].curr_pref = choice;
        student_list[idx].curr_state = WAITING_SLOT; // initially student would be waiting for a slot to be assigned
        pthread_mutex_unlock(&student_list[idx].mutex);

        pthread_mutex_lock(&student_list[idx].mutex);
        while (course_list[curr_course_idx].is_active && student_list[idx].curr_state == WAITING_SLOT) // to avoid busy waiting use conditional variable std to sleep , this
        {                                                                                             // will be signalled by the course thread which also changes the status from waiting to alloted slot

            pthread_cond_wait(&student_list[idx].std, &student_list[idx].mutex);
        }
        // two cases possible , either the course is not active or the student has been assigned a slot
        pthread_mutex_unlock(&student_list[idx].mutex);
        // if the course is not active
        if (!course_list[curr_course_idx].is_active)
        {
            if (choice == 2)
                break;

            printf(ANSI_BLUE "Student %d has changed current preference from %s (priority %d) to %s (priority %d)\n" ANSI_RESET, idx, course_list[student_list[idx].course_pref[choice]].name, choice + 1, course_list[student_list[idx].course_pref[choice + 1]].name, choice + 2);
            choice++;
            continue;
        }
        else if (student_list[idx].curr_state == ALLOCATED_SLOT)
        {
            // if slot has been allocated
            printf(ANSI_GREEN "Student %d has been allocated a seat in course %s\n", idx, course_list[curr_course_idx].name);

            pthread_mutex_lock(&student_list[idx].mutex);
            while (student_list[idx].curr_state == ALLOCATED_SLOT) // slot allocated , wait for tutorial to be conducted and the student status to change to ATTENDING TUT
            {
                pthread_cond_wait(&course_list[curr_course_idx].allot, &student_list[idx].mutex);
            }
            // attended the tutorial
            // abhi status hoga ATTENDING TUTORIAL
            pthread_mutex_unlock(&student_list[idx].mutex);

            printf("Student %d attended Tutorial for course id %d : %s\n", idx, curr_course_idx, course_list[curr_course_idx].name);

            float prob_liking = probability(student_list[idx].callibre, course_list[curr_course_idx].interest); // probability of him liking the course
            float random_prob = (float)rand() / RAND_MAX;                                                       // get random probability for comparison
            if (random_prob <= prob_liking)
            {
                pthread_mutex_lock(&student_list[idx].mutex);
                student_list[idx].curr_state = COURSE_FINALISED;
                pthread_mutex_unlock(&student_list[idx].mutex);
                printf("Student %d has selected course %s permanently\n", idx, course_list[student_list[idx].course_pref[choice]].name);
                break; // exit the simualtion
            }
            else
            { // print message
                printf(ANSI_CYAN "Student %d has withdrawn from course %s\n" ANSI_RESET, idx, course_list[curr_course_idx].name);
            }
        }
        if (choice < 2)
        {
            printf(ANSI_BLUE "Student %d has changed current preference from %s (priority %d) to %s (priority %d)\n" ANSI_RESET, idx, course_list[student_list[idx].course_pref[choice]].name, choice + 1, course_list[student_list[idx].course_pref[choice + 1]].name, choice + 2);
        }
        choice++;
    }

    if (student_list[idx].curr_state != COURSE_FINALISED)
    {
        printf(ANSI_RED "Student %d exited simulation , could not be allocated any course of it's preference\n" ANSI_RESET, idx);
    }

    return NULL;
}

int main()
{
    srand(time(0));
    int ret = take_input();
    if (ret == -1)
        return 1;

    simulation = true;

    for (int i = 0; i < num_students; i++)
    {
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        pthread_create(&student_list[i].thread_idx, NULL, student_thread, id);
    }

    for (int i = 0; i < num_courses; i++)
    {
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        pthread_create(&course_list[i].thread_idx, NULL, course_thread, id);
    }

    for (int i = 0; i < num_students; i++)
    {
        pthread_join(student_list[i].thread_idx, NULL);
    }

    simulation = false;
    return 0;
}