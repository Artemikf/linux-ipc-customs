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
#include "customs.h"

// Структура для передачі даних через FIFO (Паспортний інспектор -> Інспектор багажу)
struct passport_check_result {
    int passport_num;
    int is_allowed;
};

// Глобальні змінні для відстеження сигналів готовності від дітей
volatile sig_atomic_t inspector1_ready = 0;
volatile sig_atomic_t inspector2_ready = 0;

void handle_sigusr1(int sig) {
    inspector1_ready = 1;
}

void handle_sigusr2(int sig) {
    inspector2_ready = 1;
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

    // Створюємо іменований канал (FIFO)
    unlink(FIFO_NAME);
    if (mkfifo(FIFO_NAME, 0666) == -1) {
        perror("Помилка створення mkfifo");
        exit(EXIT_FAILURE);
    }

    // Налаштування POSIX черги повідомлень (Message Queue)
    mq_unlink(QUEUE_NAME); // Видаляємо стару чергу, якщо лишилась
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct customs_msg);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("Помилка створення message queue");
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

        // Сигналимо батькові про готовність
        kill(getppid(), SIGUSR1);

        int passport_num;
        while (read(pipe_fd[0], &passport_num, sizeof(int)) > 0) {
            int r = rand() % 100;
            struct passport_check_result res;
            res.passport_num = passport_num;
            res.is_allowed = (r >= 20); // 80% дозволено, 20% заборонено

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

        // Відкриваємо чергу повідомлень на запис
        mqd_t mq_child = mq_open(QUEUE_NAME, O_WRONLY);
        if (mq_child == (mqd_t)-1) {
            perror("Помилка відкриття message queue дитиною");
            close(fifo_fd);
            exit(EXIT_FAILURE);
        }

        // Сигналимо батькові про готовність
        kill(getppid(), SIGUSR2);

        struct passport_check_result res;
        while (read(fifo_fd, &res, sizeof(struct passport_check_result)) > 0) {
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

            // Відправляємо результат у чергу повідомлень для Начальника
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

    // Очікування завершення дочірніх процесів
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    // Начальник зчитує результати з черги повідомлень
    printf("\n--- [Начальник] ОФІЦІЙНИЙ ЗВІТ З ЧЕРГИ ПОВІДОМЛЕНЬ (MESSAGE QUEUE) ---\n");
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

    // Очищення IPC ресурсів
    mq_close(mq);
    mq_unlink(QUEUE_NAME);
    unlink(FIFO_NAME);

    printf("[Начальник] Робота пункту пропуску успішно завершена.\n");
    return 0;
}