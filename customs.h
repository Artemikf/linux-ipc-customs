#ifndef CUSTOMS_H
#define CUSTOMS_H

#define QUEUE_NAME "/customs_mq"
#define FIFO_NAME "/tmp/customs_fifo"

// Структура для черги повідомлень (Інспектор багажу -> Начальник)
struct customs_msg {
    int passport_num;
    int baggage_score; // Бали митного контролю від 1 до 6
};

#endif