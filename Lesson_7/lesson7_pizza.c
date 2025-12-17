#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>

// Количество столов (макс. одновременно готовящихся пицц)
static int N_tables;

// Всего пицц, которые будут приготовлены всеми поварами
static int total_pizzas = 0;
static int delivered_total = 0;

// Семафоры
static sem_t sem_tables;      // свободные столы
static sem_t sem_items;       // количество элементов (пицц или "ядов") в очереди
static sem_t sem_slots;       // свободные слоты в очереди
static sem_t sem_queue_mutex; // "мьютекс" для очереди (бинарный семафор)
static sem_t sem_stats_mutex; // "мьютекс" для статистики

// Простейшая кольцевая очередь для "заказов" (ид пицц / служебные метки)
typedef struct {
    int* buf;
    int capacity;
    int head;
    int tail;
} PizzaQueue;

static PizzaQueue queue_pizzas;

static void queue_init(PizzaQueue* q, int capacity) {
    q->buf = malloc(sizeof(int) * capacity);
    if (!q->buf) {
        perror("malloc queue");
        exit(EXIT_FAILURE);
    }
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
}

static void queue_push(PizzaQueue* q, int value) {
    // ждём свободный слот
    sem_wait(&sem_slots);
    // критическая секция очереди
    sem_wait(&sem_queue_mutex);

    q->buf[q->tail] = value;
    q->tail = (q->tail + 1) % q->capacity;

    sem_post(&sem_queue_mutex);
    // в очереди появился новый элемент
    sem_post(&sem_items);
}

static int queue_pop(PizzaQueue* q) {
    int value;
    // ждём появления элемента
    sem_wait(&sem_items);
    // критическая секция очереди
    sem_wait(&sem_queue_mutex);

    value = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;

    sem_post(&sem_queue_mutex);
    // освободили слот
    sem_post(&sem_slots);
    return value;
}

typedef struct {
    int id;
    int pizzas_to_make;
} CookArg;

typedef struct {
    int id;
} CourierArg;

static int rnd_range(int from, int to_inclusive) {
    return from + rand() % (to_inclusive - from + 1);
}

static void* cook_thread(void* arg) {
    CookArg* c = (CookArg*)arg;
    int id = c->id;
    int n = c->pizzas_to_make;

    for (int i = 0; i < n; ++i) {
        // ждём свободный стол
        sem_wait(&sem_tables);

        printf("Cook %d: started pizza %d/%d\n", id, i + 1, n);
        fflush(stdout);

        // готовим пиццу (имитация временем)
        sleep(rnd_range(1, 3));

        // пицца готова: снимаем со стола и кладём в очередь готовых
        int pizza_id = id * 1000 + i; // чисто для логов делаю уникальный id
        printf("Cook %d: finished pizza %d/%d (id=%d)\n", id, i + 1, n, pizza_id);
        fflush(stdout);

        // кладём пиццу в очередь
        queue_push(&queue_pizzas, pizza_id);

        // освобождаем стол
        sem_post(&sem_tables);
    }

    printf("Cook %d: done all %d pizzas\n", id, n);
    fflush(stdout);
    return NULL;
}

static void* courier_thread(void* arg) {
    CourierArg* c = (CourierArg*)arg;
    int id = c->id;

    for (;;) {
        int pizza_id = queue_pop(&queue_pizzas);

        if (pizza_id < 0) {
            // отрицательное значение — "яд", сигнал остановки
            printf("Courier %d: got stop signal, going to rest\n", id);
            fflush(stdout);
            break;
        }

        printf("Courier %d: took pizza id=%d\n", id, pizza_id);
        fflush(stdout);

        // имитация доставки
        sleep(rnd_range(1, 3));

        sem_wait(&sem_stats_mutex);
        delivered_total++;
        int delivered_now = delivered_total;
        int total = total_pizzas;
        sem_post(&sem_stats_mutex);

        printf("Courier %d: delivered pizza id=%d (delivered %d/%d)\n",
            id, pizza_id, delivered_now, total);
        fflush(stdout);
    }

    return NULL;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s N_tables P_cooks K_couriers max_pizzas_per_cook\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    N_tables = atoi(argv[1]);
    int P = atoi(argv[2]); // повара
    int K = atoi(argv[3]); // курьеры
    int max_pizzas_per_cook = atoi(argv[4]);

    if (N_tables <= 0 || P <= 0 || K < 0 || max_pizzas_per_cook <= 0) {
        fprintf(stderr, "All numeric parameters must be positive (K can be 0).\n");
        return EXIT_FAILURE;
    }

    srand((unsigned)time(NULL));

    CookArg* cook_args = malloc(sizeof(CookArg) * P);
    CourierArg* courier_args = malloc(sizeof(CourierArg) * K);
    pthread_t* cook_threads = malloc(sizeof(pthread_t) * P);
    pthread_t* courier_threads = malloc(sizeof(pthread_t) * K);

    if (!cook_args || !courier_args || !cook_threads || !courier_threads) {
        perror("malloc threads");
        return EXIT_FAILURE;
    }

    // Генерируем, сколько пицц сделает каждый повар: random(x) ∈ [1, x]
    total_pizzas = 0;
    for (int i = 0; i < P; ++i) {
        int n = rnd_range(1, max_pizzas_per_cook);
        cook_args[i].id = i;
        cook_args[i].pizzas_to_make = n;
        total_pizzas += n;
    }

    printf("Total pizzas to make: %d\n", total_pizzas);
    for (int i = 0; i < P; ++i) {
        printf("Cook %d will make %d pizzas\n",
            cook_args[i].id, cook_args[i].pizzas_to_make);
    }
    fflush(stdout);

    // Очередь: размер = все пиццы + K "ядов"
    int queue_capacity = total_pizzas + K;
    if (queue_capacity == 0) {
        queue_capacity = 1; // чисто на всякий случай
    }
    queue_init(&queue_pizzas, queue_capacity);

    // Инициализируем семафоры
    if (sem_init(&sem_tables, 0, N_tables) != 0 ||
        sem_init(&sem_items, 0, 0) != 0 ||
        sem_init(&sem_slots, 0, queue_capacity) != 0 ||
        sem_init(&sem_queue_mutex, 0, 1) != 0 ||
        sem_init(&sem_stats_mutex, 0, 1) != 0) {
        perror("sem_init");
        return EXIT_FAILURE;
    }

    // Создаём поваров
    for (int i = 0; i < P; ++i) {
        if (pthread_create(&cook_threads[i], NULL, cook_thread, &cook_args[i]) != 0) {
            perror("pthread_create cook");
            return EXIT_FAILURE;
        }
    }

    // Создаём курьеров
    for (int i = 0; i < K; ++i) {
        courier_args[i].id = i;
        if (pthread_create(&courier_threads[i], NULL, courier_thread,
            &courier_args[i]) != 0) {
            perror("pthread_create courier");
            return EXIT_FAILURE;
        }
    }

    // Ждём всех поваров
    for (int i = 0; i < P; ++i) {
        pthread_join(cook_threads[i], NULL);
    }

    // Все повара закончили — новых пицц не будет.
    // Кладём K отрицательных "ядов", чтобы разбудить и корректно остановить курьеров.
    for (int i = 0; i < K; ++i) {
        queue_push(&queue_pizzas, -1);
    }

    // Ждём всех курьеров
    for (int i = 0; i < K; ++i) {
        pthread_join(courier_threads[i], NULL);
    }

    printf("Simulation finished. Total pizzas=%d, delivered=%d\n",
        total_pizzas, delivered_total);

    // Чистим ресурсы
    sem_destroy(&sem_tables);
    sem_destroy(&sem_items);
    sem_destroy(&sem_slots);
    sem_destroy(&sem_queue_mutex);
    sem_destroy(&sem_stats_mutex);

    free(queue_pizzas.buf);
    free(cook_args);
    free(courier_args);
    free(cook_threads);
    free(courier_threads);

    return EXIT_SUCCESS;
}