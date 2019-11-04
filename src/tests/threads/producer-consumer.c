/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "lib/random.h"

#define BUF_SIZE 5
#define STR_SIZE 11

static void producer(void* n);
static void consumer(void* n);
static bool is_empty(void);
static bool is_full(void);
static void producer_consumer(unsigned int num_producer, unsigned int num_consumer);

char *buffer;

/* Variables */
uint32_t write_pos, read_pos;

/* Locks */
struct lock buffer_mutex;

/* Condition variables */
struct condition not_empty;
struct condition not_full;

void test_producer_consumer(void)
{
    producer_consumer(0, 0);
    producer_consumer(1, 0);
    producer_consumer(0, 1);
    producer_consumer(1, 1);
    producer_consumer(2, 1);
    producer_consumer(1, 3);
    producer_consumer(4, 4);
    producer_consumer(7, 2);
    producer_consumer(2, 7);
    producer_consumer(6, 6);
    pass();
}

static void producer(void* n)
{
    char str[STR_SIZE] = "Hello world";
    int str_pos = 0;

    printf("I am producer %d\n", (uint32_t)n);
    do
      {
        lock_acquire(&buffer_mutex);
        while(is_full())
            cond_wait(&not_full, &buffer_mutex);

        buffer[write_pos%(BUF_SIZE)] = str[str_pos];
        write_pos++;
        str_pos++;

        cond_signal(&not_empty, &buffer_mutex);
        lock_release(&buffer_mutex);
      } while(str_pos < STR_SIZE);
}

static void consumer(void* n)
{
    int n_read = 0;

    printf("I am consumer %d\n", (uint32_t)n);
    while(n_read < STR_SIZE)
      {
        lock_acquire(&buffer_mutex);
        while(is_empty())
            cond_wait(&not_empty, &buffer_mutex);
  
        printf("%c\n", buffer[read_pos%(BUF_SIZE)]);
        read_pos++;
        n_read++;

        cond_signal(&not_full, &buffer_mutex);
        lock_release(&buffer_mutex);
      }
}

static bool is_full()
{
    return write_pos - read_pos == BUF_SIZE;
}

static bool is_empty()
{
    return write_pos == read_pos;
}

static void producer_consumer(unsigned int num_producer, unsigned int num_consumer)
{
    uint32_t i;

    buffer = malloc(BUF_SIZE*sizeof(char));

    lock_init(&buffer_mutex);
    cond_init(&not_empty);
    cond_init(&not_full);
    write_pos = read_pos = 0;

    printf("*** Test with %d producers and %d consumers ***\n", num_producer, num_consumer);
    for (i = 0; i < num_consumer; ++i)
        thread_create("Consumer", PRI_DEFAULT, consumer, (void*)i);
    for (i = 0; i < num_producer; ++i)
        thread_create("Producer", PRI_DEFAULT, producer, (void*)i);
}