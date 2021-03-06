#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/queue.h>
#include <pthread.h>
#include <limits.h>

#include "uthash.h"
#include "tokenizer.h"
#include "util.h"
#include "queue.h"

#define FALSE 0
#define TRUE 1

// Will be used for writing to output stream and database
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t database_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct BookOrder {
    char* title;
    int   customer_id;
    char* category;
    float price;
} BookOrder;

struct Customer {
    char* name;
    int   customer_id; // key in hashtable
    char* address;
    char* state;
    char* zipcode;
    float balance;
    Queue *successful_orders; // queue of struct SuccessfulOrder
    Queue *rejected_orders; // queue of struct RejectedOrder
    UT_hash_handle hh;
};

struct SuccessfulOrder {
    char* title;
    float price;
    float balance;
};

struct RejectedOrder {
    char* title;
    float price;
};

struct CategoryQueue {
    char* category; // key
    Queue *queue;
    UT_hash_handle hh;
};

struct CategoryThread {
    pthread_t thread;
    struct CategoryThread *next;
};

struct Customer* customers = NULL; // hash table for customers
struct CategoryQueue* category_queues = NULL; // hash table for category queues

void add_customer(struct Customer *s)
{
    HASH_ADD_INT(customers,customer_id,s);
}

struct Customer* find_customer(int customer_id)
{
    struct Customer* s;
    HASH_FIND_INT(customers,&customer_id,s);
    return s;
}

Queue *find_category_queue(char *category)
{
    struct CategoryQueue* q;
    HASH_FIND_STR(category_queues,category,q);
    if (q == NULL) {
        return NULL;
    }
    return q->queue;
}

void add_category_queue(char *category)
{
    char *category_copy = malloc(strlen(category) + 1);
    strcpy(category_copy, category);
    struct CategoryQueue *c = malloc(sizeof(struct CategoryQueue));
    c->category = category_copy;
    c->queue = Q_create_queue();
    if (find_category_queue(category) == NULL) {
        HASH_ADD_KEYPTR( hh, category_queues, c->category, strlen(c->category), c );
    }
}

void* orderFunc(void* arg)
{
    char* order_file = (char*)arg;

    FILE* fp = fopen(order_file, "r");

    char line[LINE_MAX];
    while (fgets(line,LINE_MAX,fp) != NULL) {
        TokenizerT *tokenizer = TKCreate("|\n\r",line);

        struct BookOrder *order = malloc(sizeof(struct BookOrder));
        order->title = TKGetNextToken(tokenizer);
        char* temp = TKGetNextToken(tokenizer);
        order->price = atof(temp);
        free(temp);
        temp = TKGetNextToken(tokenizer);
        order->customer_id = atoi(temp);
        free(temp);
        order->category = TKGetNextToken(tokenizer);

        Queue *q = find_category_queue(order->category);

        if (!q) {
            free(order->title);
            free(order->category);
            free(order);
            TKDestroy(tokenizer);
            continue;
        }

        Q_enqueue(q, (void *)order);
        TKDestroy(tokenizer);
    }

    // Notify threads about end of stream
    struct CategoryQueue *q;
    for (q = category_queues; q != NULL; q = q->hh.next) {
        q->queue->isopen = FALSE;
    }

    fclose(fp);

    return NULL;
}

void* categoryFunc(void* arg)
{
    char* category = (char*)arg;
    Queue* q = find_category_queue(category);
    free(category);

    if (!q) {
      return NULL;
    }

    while (q->isopen || q->length > 0) {
        if (q->length == 0) {
            continue;
        }

        pthread_mutex_lock(&database_lock);
        struct BookOrder *order = (struct BookOrder*)Q_dequeue(q);
        struct Customer* cust = find_customer(order->customer_id);
        cust->balance = cust->balance - order->price;

        pthread_mutex_lock(&file_lock);
        fprintf(stdout,"-----------------------------------\n");
        fprintf(stdout,"### Order Summary ###\n");
        fprintf(stdout,"Customer name: %s\n",cust->name);
        fprintf(stdout,"Customer id: %d\n",cust->customer_id);

        if (cust->balance > 0) {
            fprintf(stdout,"Order accepted, remaining balance: %.2f\n\n",cust->balance);
        } else {
            fprintf(stdout,"Order rejected, remaining balance: %.2f\n\n",cust->balance+order->price);
        }

        if (cust->balance < 0) {
            fprintf(stdout,"### Rejected Order ###\n");
            fprintf(stdout, "Title of book: %s\n",order->title);
            fprintf(stdout, "Cost of book: %.2f\n",order->price);
            cust->balance = cust->balance+order->price;

            struct RejectedOrder *r = malloc(sizeof(struct RejectedOrder));
            r->title = malloc(strlen(order->title) + 1);
            strcpy(r->title, order->title);
            r->price = order->price;

            Q_enqueue(cust->rejected_orders, r);
        } else {
            fprintf(stdout,"### Accepted Order ###\n");
            fprintf(stdout,"Title of book: %s\n",order->title);
            fprintf(stdout,"Cost of book: %.2f\n",order->price);

            struct SuccessfulOrder *s = malloc(sizeof(struct SuccessfulOrder));
            s->title = malloc(strlen(order->title) + 1);
            strcpy(s->title, order->title);
            s->price = order->price;
            s->balance = cust->balance;

            Q_enqueue(cust->successful_orders, s);
        }

        free(order->title);
        free(order->category);
        free(order);
        pthread_mutex_unlock(&file_lock);
        pthread_mutex_unlock(&database_lock);
    }

    return NULL;
}

void create_customers(char* databasefile)
{
    FILE* fp = fopen(databasefile,"r");
    char line[LINE_MAX];

    while (fgets(line,LINE_MAX,fp) != NULL) {

        // We tokenize by pipe | and quotation " character
        TokenizerT* tokenizer  = TKCreate("|\"",line);
        struct Customer *customer = malloc(sizeof(struct Customer));

        customer->name = TKGetNextToken(tokenizer);
        char* temp = TKGetNextToken(tokenizer);
        customer->customer_id = atoi(temp);
        free(temp);
        temp = TKGetNextToken(tokenizer);
        customer->balance = atof(temp);
        free(temp);
        customer->address = TKGetNextToken(tokenizer);
        customer->state = TKGetNextToken(tokenizer);
        customer->zipcode = TKGetNextToken(tokenizer);
        customer->successful_orders = Q_create_queue();
        customer->rejected_orders = Q_create_queue();

        add_customer(customer);

        TKDestroy(tokenizer);
    }

    fclose(fp);
}

void destroy_customers(void)
{
    struct Customer *c, *tmp;
    HASH_ITER(hh,customers,c,tmp) {
        HASH_DEL(customers,c);
        Q_destroy(c->successful_orders);
        Q_destroy(c->rejected_orders);
        free(c->successful_orders);
        free(c->rejected_orders);
        free(c->name);
        free(c->address);
        free(c->zipcode);
        free(c->state);
        free(c);
    }
}

void destroy_category_queues(void)
{
    struct CategoryQueue *q, *tmp2;
    HASH_ITER(hh,category_queues,q,tmp2) {
        HASH_DEL(category_queues,q);
        Q_destroy(q->queue);
        free(q->category);
        free(q->queue);
        free(q);
    }
}
int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Error: incorrect input format.\n");
        fprintf(stderr, "Correct format: ./bookorder <database file> <book order file> <categories>\n");
        return -1;
    }

    char* databasefile = argv[1];
    char* bookorderfile = argv[2];

    if (!file_exists(databasefile)) {
        fprintf(stderr, "Error: Could not file database file.\n");
        return -1;
    }

    if (!file_exists(bookorderfile)) {
        fprintf(stderr, "Error: Could not file book order file.\n");
        return -1;
    }

    create_customers(databasefile);

    char* categories = argv[3];
    TokenizerT* tokenizer = TKCreate(" ", categories);
    char* tok = TKGetNextToken(tokenizer);

    struct CategoryThread *category_threads = NULL;

    if (file_exists(tok)) {
        FILE* fp = fopen(tok,"r");
        char line[LINE_MAX];

        while (fgets(line,LINE_MAX,fp) != NULL) {
            removeNewline(line);
            add_category_queue(line);
            // Gonna cause memory leak, need to fix.
            char* category = malloc(strlen(line)+1);
            strcpy(category, line);

            // create category_threads
            pthread_t category_thread;
            pthread_create(&category_thread, NULL, categoryFunc, category);

            struct CategoryThread *cat_thread = malloc(sizeof(struct CategoryThread));
            cat_thread->thread = category_thread;
            if (category_threads == NULL) {
                category_threads = cat_thread;
                cat_thread->next = NULL;
            } else {
                cat_thread->next = category_threads;
                category_threads = cat_thread;
            }

        }

        fclose(fp);
    } else {
        do {
            char* category = malloc(strlen(tok)+1);
            strcpy(category, tok);

            add_category_queue(category);

            // create category_thread
            pthread_t category_thread;
            pthread_create(&category_thread,NULL,categoryFunc,category);

            struct CategoryThread *cat_thread = malloc(sizeof(struct CategoryThread));
            cat_thread->thread = category_thread;
            if (category_threads == NULL) {
                category_threads = cat_thread;
                cat_thread->next = NULL;
            } else {
                cat_thread->next = category_threads;
                category_threads = cat_thread;
            }

            free(tok);
       } while ((tok = TKGetNextToken(tokenizer)) != NULL);
    }

    TKDestroy(tokenizer);
    free(tok);

    // End of main thread of execution

    // Multi-threading begins

    pthread_t producer_thread;
    pthread_create(&producer_thread,NULL,orderFunc,bookorderfile);
    pthread_join(producer_thread,NULL); // Join producer thread

    // Join category threads
    struct CategoryThread *ptr = NULL;
    for (ptr = category_threads; ptr != NULL; ptr=ptr->next) {
        pthread_join(ptr->thread,NULL);
    }

    // Write customer history to stdout.
    struct Customer *c, *tmp;
    HASH_ITER(hh,customers,c,tmp) {
      fprintf(stdout, "=== BEGIN CUSTOMER INFO ===\n");
      fprintf(stdout, "### BALANCE ###\n");
      fprintf(stdout, "Customer name: %s\n", c->name);
      fprintf(stdout, "Customer ID number: %d\n", c->customer_id);
      fprintf(stdout, "Remaining credit balance after all purchases (a dollar amount): %.2f\n", c->balance);

      fprintf(stdout, "### SUCCESSFUL ORDERS ###\n");
      struct SuccessfulOrder *s;
      while ((s = Q_dequeue(c->successful_orders))) {
        fprintf(stdout, "%s|%.2f|%.2f\n", s->title, s->price, s->balance);
        free(s->title);
        free(s);
      }

      fprintf(stdout, "### REJECTED ORDERS ###\n");
      struct RejectedOrder*r;
      while ((r = Q_dequeue(c->rejected_orders))) {
        fprintf(stdout, "%s|%.2f\n", r->title, r->price);
        free(r->title);
        free(r);
      }
      fprintf(stdout, "=== END CUSTOMER INFO ===\n\n");
    }

    ptr = category_threads;
    while (ptr != NULL) {
        struct CategoryThread *temp = ptr;
        ptr = ptr->next;
        free(temp);
    }
    category_threads = NULL;

    destroy_customers();
    destroy_category_queues();

    pthread_mutex_destroy(&file_lock);
    pthread_mutex_destroy(&database_lock);

    return 0;
}
