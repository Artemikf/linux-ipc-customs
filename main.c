#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <mqueue.h>
#include <semaphore.h>
#include "customs.h"

#define SEM_NAME "/customs_sem"
#define LOG_FILE "breaks.log"

struct passport_check_result {
    int passport_num;
    int is_allowed;
};

volatile sig_atomic_t inspector1_ready = 0;
volatile sig_atomic_t inspector2_ready = 0;

void handle_sigusr1(int sig) {
    inspector1_ready = 1;
}

void handle_sigusr2(int sig) {
    inspector2_ready = 1;
}

// Функція для симуляції перерви інспектора з використанням семафора та логуванням
void take_a_break(int inspector_id, sem_t *sem) {
    // Шанс піти на перерву (наприклад, випадково під час роботи)
    if (rand() % 2 == 0) {
        printf("[Інспектор %d] Запит на перерву...\n", inspector_id);
        
        // Захоплюємо семафор (не більше 1 інспектора на перерві водночас)
        sem_wait(sem);

        time_t start_time = time(NULL);
        char start_str[64];
        strftime(start_str, sizeof(start_str), "%Y-%m-%d %H:%M:%S", localtime(&start_time));

        printf("[Інспектор %d] ПІШОВ НА ПЕРЕРВУ о %s\n", inspector_id, start_str);

        // Симулюємо час відпочинку (наприклад, 0.2 секунди)
        usleep(200000);

        time_t end_time = time(NULL);
        char end_str[64];
        strftime(end_str, sizeof(end_str), "%Y-%m-%d %H:%M:%S", localtime(&end_time));

        // Записуємо до файлу логів
        FILE *log = fopen(LOG_FILE, "a");
        if (log != NULL) {
            fprintf(log, "Інспектор %d | Початок: %s | Кінець: %s\n", inspector_id, start_str, end_str);
            fclose(log);
        }

        printf("[Інспектор %d] ПОВЕРНУВСЯ З ПЕРЕРВИ о %s\n", inspector_id, end_str);

        // Звільняємо семафор
        sem_post(sem);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Використання: %s <кількість_мандрівників>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int travelers_count = atoi(argv[1]);
    if (travelers_count <= 0) {
        fprintf(stderr, "Кількість мандрівників має бути більшою за 0.\n");
        exit(EXIT_FAILURE);
    }

    // Очищення старих файлів логів перед запусків
    unlink(LOG_FILE);

    // Створюємо іменований канал (FIFO)
    unlink(FIFO_NAME);
    if (mkfifo(FIFO_NAME, 0666) == -1) {
        perror("Помилка створення mkfifo");
        exit(EXIT_FAILURE);
    }

    // Створюємо семафор (початкове значення = 1)
    sem_unlink(SEM_NAME);
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_RDWR, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("Помилка створення семафора");
        unlink(FIFO_NAME);
        exit(EXIT_FAILURE);
    }

    // Налаштування POSIX черги повідомлень
    mq_unlink(QUEUE_NAME);
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct customs_msg);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("Помилка створення message queue");
        sem_close(sem);
        sem_unlink(SEM_NAME);
        unlink(FIFO_NAME);
        exit(EXIT_FAILURE);
    }

    // Налаштування обробників сигналів
    struct sigaction sa1, sa2;
    sa1.sa_handler = handle_sigusr1;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0;
    sigaction(SIGUSR1, &sa1, NULL);

    sa2.sa_handler = handle_sigusr2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);

    // Створення неіменованого каналу (pipe)
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Помилка створення pipe");
        mq_close(mq);
        mq_unlink(QUEUE_NAME);
        sem_close(sem);
        sem_unlink(SEM_NAME);
        unlink(FIFO_NAME);
        exit(EXIT_FAILURE);
    }

    pid_t pid1, pid2;

    // --- 1-й ДОЧІРНІЙ ПРОЦЕС (Паспортний інспектор) ---
    pid1 = fork();
    if (pid1 < 0) {
        perror("Помилка fork для інспектора 1");
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        close(pipe_fd[1]);
        srand(time(NULL) ^ getpid());

        int fifo_fd = open(FIFO_NAME, O_WRONLY);
        if (fifo_fd == -1) {
            perror("Помилка відкриття FIFO на запис");
            exit(EXIT_FAILURE);
        }

        kill(getppid(), SIGUSR1);

        int passport_num;
        while (read(pipe_fd[0], &passport_num, sizeof(int)) > 0) {
            // Можливість піти на перерву під час роботи
            take_a_break(1, sem);

            int r = rand() % 100;
            struct passport_check_result res;
            res.passport_num = passport_num;
            res.is_allowed = (r >= 20);

            if (res.is_allowed) {
                printf("[Паспортний інспектор] Паспорт №%d: ПРОЇЗД ДОЗВОЛЕНО\n", passport_num);
            } else {
                printf("[Паспортний інспектор] Паспорт №%d: ПРОЇЗД ЗАБОРОНЕНО\n", passport_num);
            }

            write(fifo_fd, &res, sizeof(struct passport_check_result));
            usleep(100000);
        }

        close(pipe_fd[0]);
        close(fifo_fd);
        exit(EXIT_SUCCESS);
    }

    // --- 2-й ДОЧІРНІЙ ПРОЦЕС (Інспектор багажу) ---
    pid2 = fork();
    if (pid2 < 0) {
        perror("Помилка fork для інспектора 2");
        exit(EXIT_FAILURE);
    } else if (pid2 == 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        srand(time(NULL) ^ (getpid() + 1));

        int fifo_fd = open(FIFO_NAME, O_RDONLY);
        if (fifo_fd == -1) {
            perror("Помилка відкриття FIFO на читання");
            exit(EXIT_FAILURE);
        }

        mqd_t mq_child = mq_open(QUEUE_NAME, O_WRONLY);
        if (mq_child == (mqd_t)-1) {
            perror("Помилка відкриття message queue дитиною");
            close(fifo_fd);
            exit(EXIT_FAILURE);
        }

        kill(getppid(), SIGUSR2);

        struct passport_check_result res;
        while (read(fifo_fd, &res, sizeof(struct passport_check_result)) > 0) {
            // Можливість піти на перерву під час огляду багажу
            take_a_break(2, sem);

            struct customs_msg msg;
            msg.passport_num = res.passport_num;
            msg.is_allowed = res.is_allowed;

            if (res.is_allowed) {
                msg.baggage_score = 1 + rand() % 6;
                printf("[Інспектор багажу] Паспорт №%d пройшов контроль. Багаж: %d балів\n", 
                       res.passport_num, msg.baggage_score);
            } else {
                msg.baggage_score = 0;
                printf("[Інспектор багажу] Паспорт №%d затримано. Багаж не оглядається.\n", 
                       res.passport_num);
            }

            if (mq_send(mq_child, (char *)&msg, sizeof(struct customs_msg), 0) == -1) {
                perror("Помилка mq_send");
            }

            usleep(100000);
        }

        close(fifo_fd);
        mq_close(mq_child);
        exit(EXIT_SUCCESS);
    }

    // --- БАТЬКІВСЬКИЙ ПРОЦЕС (Начальник зміни) ---
    close(pipe_fd[0]);

    printf("[Начальник] Очікування готовності інспекторів...\n");
    while (!inspector1_ready || !inspector2_ready) {
        pause();
    }
    printf("[Начальник] Обидва інспектори готові до роботи!\n");

    srand(time(NULL));
    printf("[Начальник] Генерація та відправка %d мандрівників у pipe...\n", travelers_count);

    for (int i = 0; i < travelers_count; i++) {
        int passport_num = 100000 + rand() % 900000;
        write(pipe_fd[1], &passport_num, sizeof(int));
        usleep(100000);
    }

    close(pipe_fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    // Звіт з черги повідомлень
    printf("\n--- [Начальник] ОФІЦІЙНИЙ ЗВІТ З ЧЕРГИ ПОВІДОМЛЕНЬ ( MESSAGE QUEUE ) ---\n");
    for (int i = 0; i < travelers_count; i++) {
        struct customs_msg msg;
        if (mq_receive(mq, (char *)&msg, sizeof(struct customs_msg), NULL) != -1) {
            if (msg.is_allowed) {
                printf("[Звіт Начальника] Мандрівник №%d: Проїзд ДОЗВОЛЕНО | Митний бал: %d/6\n", 
                       msg.passport_num, msg.baggage_score);
            } else {
                printf("[Звіт Начальника] Мандрівник №%d: Проїзд ЗАБОРОНЕНО | Затримано\n", 
                       msg.passport_num);
            }
        }
    }
    printf("--------------------------------------------------------------------\n");

    // Вивід вмісту файлу логів перерв (breaks.log)
    printf("\n--- [Лог-файл] ВМІСТ ФАЙЛУ `breaks.log` (Пункт D - Семафор) ---\n");
    FILE *log_read = fopen(LOG_FILE, "r");
    if (log_read != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), log_read) != NULL) {
            printf("%s", line);
        }
        fclose(log_read);
    } else {
        printf("Логи перерв відсутні або файл не створено.\n");
    }
    printf("---------------------------------------------------------------\n");

    // Очищення всіх IPC ресурсів та семафорів
    mq_close(mq);
    mq_unlink(QUEUE_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);
    unlink(FIFO_NAME);

    printf("[Начальник] Робота пункту пропуску успішно завершена.\n");
    return 0;
}