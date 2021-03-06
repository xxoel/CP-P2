#include <stddef.h>
#include <sys/types.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#define PASS_LEN 6
#define NUM_THREADS 8
#define NUM_ITER 5

struct count {
    long count; // total iterations at the moment
    pthread_mutex_t mutex;
};

struct n_hashes {
    long n_hashes; // total iterations at the moment
    pthread_mutex_t mutex;
};

struct args {
    struct count *count;
    struct n_hashes *n_hashes;
    char **md5;
};

long ipow(long base, int exp) {
    long res = 1;
    for (;;)
    {
        if (exp & 1)
            res *= base;
        exp >>= 1; if (!exp) break; base *= base;
    }

    return res;
}

long pass_to_long(char *str) {
    long res = 0;

    for(int i=0; i < PASS_LEN; i++)
        res = res * 26 + str[i]-'a';

    return res;
};

void long_to_pass(long n, unsigned char *str) {  // str should have size PASS_SIZE+1
    for(int i=PASS_LEN-1; i >= 0; i--) {
        str[i] = n % 26 + 'a';
        n /= 26;
    }
    str[PASS_LEN] = '\0';
}

int hex_value(char c) {
    if (c>='0' && c <='9')
        return c - '0';
    else if (c>= 'A' && c <='F')
        return c-'A'+10;
    else if (c>= 'a' && c <='f')
        return c-'a'+10;
    else return 0;
}

void hex_to_num(char *str, unsigned char *hex) {
    for(int i=0; i < MD5_DIGEST_LENGTH; i++)
        hex[i] = (hex_value(str[i*2]) << 4) + hex_value(str[i*2 + 1]);
}

void *break_pass(void *ptr) {
    struct args *args = ptr;
    unsigned char res[MD5_DIGEST_LENGTH];
    unsigned char md5_num[MD5_DIGEST_LENGTH];
    unsigned char *pass = malloc((PASS_LEN + 1) * sizeof(char));
    double bound = ipow(26, PASS_LEN);
    int localCount;
    char* aux;

    while(args->n_hashes->n_hashes != 0 && args->count->count < bound){

        pthread_mutex_lock(&args->count->mutex);
        localCount = args->count->count;
        args->count->count += NUM_ITER;
        pthread_mutex_unlock(&args->count->mutex);

        for(int i=0; i<NUM_ITER; i++){
            long_to_pass(localCount+i, pass);
            MD5(pass, PASS_LEN, res);
            for(int i=0;i<args->n_hashes->n_hashes;i++){
                hex_to_num(args->md5[i], md5_num);
                if(0 == memcmp(res, md5_num, MD5_DIGEST_LENGTH)){
                    printf("%s: %-50s\n", args->md5[i], (char *) pass); //print result
                    usleep(250000); //wait a quarter of a second

                    // delete hash from list 
                    for (int j = i; j < (args->n_hashes->n_hashes - 1); j++) {
                        aux = args->md5[j];
                        args->md5[j] = args->md5[j + 1];
                        args->md5[j + 1] = aux;
                    }

                    pthread_mutex_lock(&args->n_hashes->mutex);
                    args->n_hashes->n_hashes--;
                    pthread_mutex_unlock(&args->n_hashes->mutex);

                    break; // Found it!
                }
            }
        }
    }

    free(pass);
    return NULL;
}

void op_speed(struct count *count) {
    int j;
    j = count->count;
    usleep(250000); //wait a quarter of a second
    if(j<count->count)
        printf("\r\033[61C  %ld op/seg  ",(count->count-j)*4);
}

void *progress_bar(void *ptr) {
    double bound = ipow(26, PASS_LEN);
    struct args *args = ptr;
    double percent = 0;
    int i;

    while(args->n_hashes->n_hashes!=0){
        percent = (args->count->count / bound)*100;

        printf("\r%4.2f%%\t [",percent); //print percent
        printf("\033[49C ]"); //close bracket
        printf("\r\033[10C"); //move cursor
        for(i=2;i<=percent;i+=2)
            printf("\x1b[32m#\x1b[0m"); //fill bar
        printf("\r");

        op_speed(args->count); //operations per second

        fflush(stdout);
    }

    //print end message
    printf("\r\033[78C \x1b[32mFOUND\x1b[0m\n");

    return NULL;
}

pthread_t start_progress(struct args *args) {
    pthread_t thread;
    if (0 != pthread_create(&thread, NULL, progress_bar, args)) {
        printf("Could not create thread");
        exit(1);
    }

    return thread;
}

pthread_t *start_threads(struct args *args){
    pthread_t *threads = malloc(sizeof(pthread_t) * (NUM_THREADS));
    int i;

    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    // Create NUM_THREAD threads running break_pass
    for (i = 0; i < NUM_THREADS; i++) {

        if (0 != pthread_create(&threads[i], NULL, break_pass, args)) {
            printf("Could not create thread #%d of %d", i, NUM_THREADS);
            exit(1);
        }
    }

    return threads;
}

void set_values(int argc, char *argv[], struct args *args){
    
    args->count = malloc(sizeof(struct count));
    pthread_mutex_init(&args->count->mutex,NULL);
    args->count->count = 0;

    args->n_hashes = malloc(sizeof(struct n_hashes));
    pthread_mutex_init(&args->n_hashes->mutex,NULL);
    args->n_hashes->n_hashes = --argc;

    args->md5 = malloc((sizeof(char*)*MD5_DIGEST_LENGTH) * argc);

    for(int i=0;i<argc;i++)
        args->md5[i] = argv[i+1];

}

void free_values(struct args *args, pthread_t *thrs,pthread_t thr){

    for(int i=0;i<NUM_THREADS;i++){
        pthread_join(thrs[i], NULL);
    }
    pthread_join(thr, NULL);

    pthread_mutex_destroy(&args->count->mutex);
    pthread_mutex_destroy(&args->n_hashes->mutex);

    free(args->count);
    free(args->n_hashes);
    free(args->md5);
    free(args);
    free(thrs);
}

int main(int argc, char *argv[]) {
    struct args *args = malloc(sizeof(struct args));

    if(argc < 2) {
        printf("Use: %s string\n", argv[0]);
        exit(0);
    }

    set_values(argc, argv, args);

    //start progress bar
    pthread_t thr = start_progress(args);
    //start threads with break_pass
    pthread_t *thrs = start_threads(args);

    free_values(args,thrs,thr);

    return 0;
}
