#ifndef CUSTOMS_H
#define CUSTOMS_H

#define QUEUE_NAME "/customs_queue"
#define FIFO_NAME "/tmp/customs_fifo"

// Структура для черги повідомлень (Інспектор багажу -> Начальник)
struct customs_msg {
    int passport_num;
    int is_allowed;     // 1 - проїзд дозволено, 0 - заборонено
    int baggage_score;  // бали від 1 до 6 (0, якщо заборонено)
};

#endif