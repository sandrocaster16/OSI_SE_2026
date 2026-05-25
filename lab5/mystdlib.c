#include <sys/mman.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define MEGABITE (1024*1024)

typedef struct Header{
    size_t size;
    bool is_free;
    struct Header* next;
} Header;

struct Header* head = NULL;


void* custom_malloc(size_t size){
    // <При невозможности выделения возвращает NULL>
    if(size == 0) return NULL;

    // ищу блок в моих блоках выделенных (меньше MEGABITE)
    Header* qwe = head;
    while(qwe != NULL){
        if(qwe->is_free && qwe->size >= size){
            qwe->is_free=false;
            return (void*)(qwe+1);
        }
        qwe = qwe->next;
    }

    // если нету, то занимаю у ОСи mmap'ом
    size_t size_full = sizeof(Header)+size;
    Header* test = mmap(NULL, size_full, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(test == MAP_FAILED) return NULL;

    // заполняю очев
    test->size = size;
    test->is_free = false;
    test->next = NULL;

    // отдаю в конец очев
    if(head == NULL){
        head = test;
    }
    else{
        qwe = head;
        while(qwe->next != NULL) qwe = qwe->next;
        qwe->next = test;
    }

    // и отдаю поинтер после хедера
    return (void*)(test + 1);
}


void custom_free(void* ptr){
    // <Принимает NULL без ошибок>
    if(ptr == NULL) return;

    Header* start = (Header*)ptr - 1;

    // <Большие блоки (> 1 МБ) должны возвращаться системе через munmap>
    if(start->size > MEGABITE){
        if(head == start){
            head = start->next;
        }
        else{
            Header* qwe = head;
            while(qwe != NULL && qwe->next != start) qwe = qwe->next;
            if(qwe != NULL) qwe->next = start->next;
        }
        munmap(start, start->size + sizeof(Header));
        return;
    }

    // если нет, то я просто помечаю как свободный
    start->is_free = true;

    // <Объединяет соседние свободные блоки>
    // я как понял этот и следующий блок должны быть пустыми
    Header* qwe = head;
    while(qwe != NULL && qwe->next != NULL){
        if(qwe->is_free && qwe->next->is_free){
            if((char*)qwe+sizeof(Header)+qwe->size == (char*)qwe->next){
                qwe->size += sizeof(Header)+qwe->next->size;
                qwe->next = qwe->next->next;
                continue;
            }
        }

        qwe = qwe->next;
    }
}


void* custom_calloc(size_t nmemb, size_t size){
    if(nmemb == 0 || size == 0) return NULL;

    // <Выделяет память для массива из nmemb элементов по size байт>
    size_t size_full = nmemb*size;
    // <Обрабатывает переполнение при умножении nmemb * size>
    if(size_full/nmemb != size) return NULL;

    void* ptr = custom_malloc(size_full);
    if(ptr != NULL){
        // <Инициализирует всю выделенную память нулями>
        memset(ptr, 0 ,size_full);
    }
    return ptr;
}


void* custom_realloc(void* ptr, size_t size){
    // <При ptr == NULL работает как custom_malloc>
    if(ptr == NULL){
        return custom_malloc(size);
    }

    // <При size == 0 работает как custom_free (возвращает NULL )>
    if(size == 0){
        custom_free(ptr);
        return NULL;
    }

    Header* start = (Header*)ptr - 1;

    // если в минус или так же
    if (start->size >= size){
        return ptr;
    }

    // <По возможности расширяет текущий блок>
    if (start->next != NULL &&
        start->next->is_free &&
        start->size + sizeof(Header) + start->next->size >= size &&
        (char*)start + sizeof(Header) + start->next->size == (char*)start->next
    ){
        start->size += sizeof(Header)+start->next->size;
        start->next = start->next->next;

        return ptr;
    }

    // <При невозможности расширения выделяет новый блок и копирует данные>
    void* new_ptr = custom_malloc(size);
    if(new_ptr != NULL){
        memcpy(new_ptr, ptr, start->size);
        custom_free(ptr);
    }
    return new_ptr;
}