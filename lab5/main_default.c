#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

// /proc/self/statm
void print_statm(const char* str) {
    FILE* f = fopen("/proc/self/statm", "r");

    if(f){
        char buffer[256];

        if(fgets(buffer, sizeof(buffer), f)){
            buffer[strcspn(buffer, "\n")] = 0;
            printf("[statm] %s: %s\n", str, buffer);
        }

        fclose(f);
    }
}

// /proc/self/maps
void print_maps_anon(const char* str){
    FILE* f = fopen("/proc/self/maps", "r");

    if(f){
        printf("\n=== MAPS: %s ===\n", str);

        char buffer[512];
        while(fgets(buffer, sizeof(buffer), f)){
            if(strstr(buffer, "anon") || (strchr(buffer, '/') == NULL && strchr(buffer, '[') == NULL)){
                unsigned long start, end;
                if(sscanf(buffer, "%lx-%lx", &start, &end) == 2){
                    size_t size_kb = (end-start) / 1024;
                    printf("[Размер: %6zu KB]  %s", size_kb, buffer);
                }
            }
        }

        fclose(f);
    }
}


int main(){
    printf("PID: %d\n\n", getpid());


    printf("test 1\n");
    print_statm("ДО");

    // 100 блоков памяти
    void* blocks[100];
    for(int i=0; i<100; ++i){
        blocks[i] = malloc(1024);
    }

    print_statm("ПОСЛЕ 100 блоков по 1024 байт");

    // освобождение половины
    for(int i=0; i<50; ++i){
        free(blocks[i]);
    }
    print_statm("ПОСЛЕ освобождения 50 блоков");



    printf("\n\ntest 2\n");
    print_maps_anon("ДО");

    void* huge1 = malloc(2 * 1024 * 1024);
    void* huge2 = malloc(3 * 1024 * 1024);

    print_maps_anon("ПОСЛЕ выделения");

    free(huge1);

    print_maps_anon("ПОСЛЕ освобождения 1");

    free(huge2);

    print_maps_anon("ПОСЛЕ освобождения 2");


    дочистка
    for(int i=50; i<100; ++i){
        free(blocks[i]);
    }

    return 0;
}