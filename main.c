#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include "customs.h"

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

    // Налаштування обробників сигналів від дочірніх процесів
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
        exit(EXIT_FAILURE);
    }

    pid_t pid1, pid2;

    // Створення 1-го дочірнього процесу (Паспортний інспектор)
    pid1 = fork();
    if (pid1 < 0) {
        perror("Помилка fork для інспектора 1");
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        // --- КОД 1-ГО ДОЧІРНЬОГО ПРОЦЕСУ (Паспортний інспектор) ---
        // Закриваємо зайвий кінець пайпа на запис
        close(pipe_fd[1]);

        // Сигналимо батькові про готовність
        kill(getppid(), SIGUSR1);

        // Тут напарник напише логіку читання з pipe_fd[0] та роботи з FIFO
        // Наразі просто симулюємо очікування
        exit(EXIT_SUCCESS);
    }

    // Створення 2-го дочірнього процесу (Інспектор багажу / Семафори)
    pid2 = fork();
    if (pid2 < 0) {
        perror("Помилка fork для інспектора 2");
        exit(EXIT_FAILURE);
    } else if (pid2 == 0) {
        // --- КОД 2-ГО ДОЧІРНЬОГО ПРОЦЕСУ (Інспектор багажу) ---
        // Закриваємо весь pipe, бо він тут не потрібен
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        // Сигналимо батькові про готовність
        kill(getppid(), SIGUSR2);

        // Тут напарник напише логіку FIFO, митного огляду, семафорів та логування
        exit(EXIT_SUCCESS);
    }

    // --- КОД БАТЬКІВСЬКОГО ПРОЦЕСУ (Начальник зміни) ---
    // Закриваємо кінець пайпа на читання (начальник тільки пише в нього)
    close(pipe_fd[0]);

    printf("[Начальник] Очікування готовності інспекторів...\n");
    // Чекаємо, поки обидва інспектори надішлють сигнали готовності
    while (!inspector1_ready || !inspector2_ready) {
        pause();
    }
    printf("[Начальник] Обидва інспектори готові до роботи!\n");

    // Генерація випадкових номерів паспортів та запис у pipe
    srand(time(NULL));
    printf("[Начальник] Генерація та відправка %d мандрівників у pipe...\n", travelers_count);

    for (int i = 0; i < travelers_count; i++) {
        int passport_num = 100000 + rand() % 900000; // 6-значний номер паспорта
        write(pipe_fd[1], &passport_num, sizeof(int));
        printf("[Начальник] Згенеровано паспорт: %d\n", passport_num);
        usleep(100000); // невелика пауза для наочності (0.1с)
    }

    // Закриваємо записуючий кінець пайпа, щоб діти знали, що дані скінчилися
    close(pipe_fd[1]);

    // Очікування завершення дочірніх процесів
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    // Згодом сюди додадим зчитування з черги повідомлень (Message Queue)

    printf("[Начальник] Робота пункту пропуску завершена.\n");
    return 0;
}