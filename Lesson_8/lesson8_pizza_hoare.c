#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// --------- "Монитор Kitchen" ---------

typedef struct {
    int free_tables;     // свободные столы
    int ready_pizzas;    // готовые пиццы
    int active_cooks;    // сколько поваров ещё работают

    pthread_mutex_t mtx;
    pthread_cond_t  cond_table_free;
    pthread_cond_t  cond_pizza_ready;
} Kitchen;

static void kitchen_init(Kitchen* k, int tables, int cooks) {
    k->free_tables = tables;
    k->ready_pizzas = 0;
    k->active_cooks = cooks;
    pthread_mutex_init(&k->mtx, NULL);
    pthread_cond_init(&k->cond_table_free, NULL);
    pthread_cond_init(&k->cond_pizza_ready, NULL);
}

// Повар хочет занять стол
static void kitchen_start_cooking(Kitchen* k) {
    pthread_mutex_lock(&k->mtx);
    while (k->free_tables == 0) {
        pthread_cond_wait(&k->cond_table_free, &k->mtx);
    }
    k->free_tables--;
    pthread_mutex_unlock(&k->mtx);
}

// Повар закончил очередную пиццу
static void kitchen_finish_cooking(Kitchen* k) {
    pthread_mutex_lock(&k->mtx);
    k->ready_pizzas++;
    k->free_tables++;

    // разбудим и повара (если кто ждёт стол), и курьера (если ждёт пиццу)
    pthread_cond_signal(&k->cond_table_free);
    pthread_cond_signal(&k->cond_pizza_ready);
    pthread_mutex_unlock(&k->mtx);
}

// Повар сделал все свои пиццы и уходит
static void kitchen_cook_done(Kitchen* k) {
    pthread_mutex_lock(&k->mtx);
    k->active_cooks--;
    if (k->active_cooks == 0) {
        // новых пицц больше не будет — разбудить всех курьеров,
        // которые, возможно, ждут ready_pizzas > 0
        pthread_cond_broadcast(&k->cond_pizza_ready);
    }
    pthread_mutex_unlock(&k->mtx);
}

// Курьер пытается взять пиццу.
// Возвращает 1, если пицца взята; 0, если работы больше не будет.
static int kitchen_take_pizza(Kitchen* k) {
    pthread_mutex_lock(&k->mtx);

    // Пока нет готовых пицц, но ещё есть живые повара — ждём
    while (k->ready_pizzas == 0 && k->active_cooks > 0) {
        pthread_cond_wait(&k->cond_pizza_ready, &k->mtx);
    }

    // Если нет ни пицц, ни поваров — работёнки больше нет
    if (k->ready_pizzas == 0 && k->active_cooks == 0) {
        pthread_mutex_unlock(&k->mtx);
        return 0;
    }

    // здесь ready_pizzas > 0
    k->ready_pizzas--;
    pthread_mutex_unlock(&k->mtx);
    return 1;
}

static void kitchen_destroy(Kitchen* k) {
    pthread_mutex_destroy(&k->mtx);
    pthread_cond_destroy(&k->cond_table_free);
    pthread_cond_destroy(&k->cond_pizza_ready);
}

// --------- "Клиентская часть": потоки поваров и курьеров ---------

typedef struct {
    int id;
    int pizzas_to_make;
    Kitchen* kitchen;
} CookArg;

typedef struct {
    int id;
    Kitchen* kitchen;
} CourierArg;

static int rnd_range(int from, int to_inclusive) {
    return from + rand() % (to_inclusive - from + 1);
}

void* cook_thread(void* arg) {
    CookArg* c = (CookArg*)arg;
    int id = c->id;
    int n = c->pizzas_to_make;
    Kitchen* k = c->kitchen;

    printf("Cook %d: will make %d pizzas\n", id, n);
    fflush(stdout);

    for (int i = 0; i < n; ++i) {
        kitchen_start_cooking(k);

        printf("Cook %d: started pizza %d/%d\n", id, i + 1, n);
        fflush(stdout);

        // готовим (имитация временем)
        sleep(rnd_range(1, 3));

        printf("Cook %d: finished pizza %d/%d\n", id, i + 1, n);
        fflush(stdout);

        kitchen_finish_cooking(k);
    }

    printf("Cook %d: done all %d pizzas\n", id, n);
    fflush(stdout);

    kitchen_cook_done(k);
    return NULL;
}

void* courier_thread(void* arg) {
    CourierArg* c = (CourierArg*)arg;
    int id = c->id;
    Kitchen* k = c->kitchen;

    for (;;) {
        int got = kitchen_take_pizza(k);
        if (!got) {
            printf("Courier %d: no work left, going to rest\n", id);
            fflush(stdout);
            break;
        }

        printf("Courier %d: took pizza, delivering...\n", id);
        fflush(stdout);

        // доставка (имитация временем)
        sleep(rnd_range(1, 3));

        printf("Courier %d: delivered pizza\n", id);
        fflush(stdout);
    }

    return NULL;
}

// --------- main: создание потоков и запуск "монитора" ---------

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s N_tables P_cooks K_couriers max_pizzas_per_cook\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    int N_tables = atoi(argv[1]);
    int P = atoi(argv[2]); // повара
    int K = atoi(argv[3]); // курьеры
    int max_p = atoi(argv[4]);

    if (N_tables <= 0 || P <= 0 || K <= 0 || max_p <= 0) {
        fprintf(stderr, "All numeric parameters must be positive.\n");
        return EXIT_FAILURE;
    }

    srand((unsigned)time(NULL));

    Kitchen kitchen;
    kitchen_init(&kitchen, N_tables, P);

    pthread_t* cook_threads = malloc(sizeof(pthread_t) * P);
    pthread_t* courier_threads = malloc(sizeof(pthread_t) * K);
    CookArg* cook_args = malloc(sizeof(CookArg) * P);
    CourierArg* courier_args = malloc(sizeof(CourierArg) * K);

    if (!cook_threads || !courier_threads || !cook_args || !courier_args) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    int total_pizzas = 0;

    // создаём поваров
    for (int i = 0; i < P; ++i) {
        int n = rnd_range(1, max_p);
        cook_args[i].id = i;
        cook_args[i].pizzas_to_make = n;
        cook_args[i].kitchen = &kitchen;
        total_pizzas += n;

        if (pthread_create(&cook_threads[i], NULL,
            cook_thread, &cook_args[i]) != 0) {
            perror("pthread_create cook");
            return EXIT_FAILURE;
        }
    }

    printf("Total pizzas to make: %d\n", total_pizzas);
    fflush(stdout);

    // создаём курьеров
    for (int i = 0; i < K; ++i) {
        courier_args[i].id = i;
        courier_args[i].kitchen = &kitchen;
        if (pthread_create(&courier_threads[i], NULL,
            courier_thread, &courier_args[i]) != 0) {
            perror("pthread_create courier");
            return EXIT_FAILURE;
        }
    }

    // ждём поваров
    for (int i = 0; i < P; ++i) {
        pthread_join(cook_threads[i], NULL);
    }

    // ждём курьеров
    for (int i = 0; i < K; ++i) {
        pthread_join(courier_threads[i], NULL);
    }

    kitchen_destroy(&kitchen);
    free(cook_threads);
    free(courier_threads);
    free(cook_args);
    free(courier_args);

    printf("Simulation finished.\n");
    return EXIT_SUCCESS;
}