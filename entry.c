#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fftw3.h>
#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

const int samplesize = 1500;
int shared_data[samplesize/2];
char spotifybuffer[1024];
char currsonginfo[1024];

pthread_mutex_t fftmutex;
pthread_mutex_t spotifymutex;

int init_curses(){
    initscr(); cbreak(); noecho(); // curses bs 
    curs_set(0); // hide cursor
    timeout(-1); // blocking getch


    //if(LINES <= HEIGHT / 2 || COLS <= WIDTH){
        //printw("Window too small\npress any key to continue");
        //refresh();
        //getch();
        //endwin();
        //return 1;
    //}
    return 0;
}

// use min max normalization to scale magnitudes to emphasize deviations (bad and dumb)
//void min_max_normalization(double arr[], int size) { 
    //double min_val = arr[0];
    //double max_val = arr[0];

    //for (int i = 1; i < size; ++i) {
        //if (arr[i] < min_val) {
            //min_val = arr[i];
        //}
        //if (arr[i] > max_val) {
            //max_val = arr[i];
        //}
    //}

    //for (int i = 0; i < size; ++i) {
        //arr[i] = (arr[i] - min_val) / (max_val - min_val);
    //}
//}

void z_score_normalization(double arr[], int size) {
    double sum = 0.0;
    double sum_sq = 0.0;

    for (int i = 0; i < size; ++i) {
        sum += arr[i];
        sum_sq += arr[i] * arr[i];
    }

    double mean = sum / size;
    double variance = (sum_sq - (sum * sum) / size) / (size - 1);
    double std_deviation = sqrt(variance);

    for (int i = 0; i < size; ++i) {
        arr[i] = (arr[i] - mean) / std_deviation;
    }
}

void writesongartist(char * str){
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if(strlen(str) == 0){
        mvprintw(0, 0, "bad");
    }else{
        size_t offset = strlen(str);
        int start_col = cols - offset;
        mvprintw(0, start_col, "%s", str);
    }
}

void draw_viz(int * mags, int num_frames){
    clear();
    for(int i = 0; i < num_frames; i++){
        if(i >= COLS){
            break;
        }
        for(int j = 0; j < mags[i]; j++){
            int y = LINES - 1 - j;
            mvaddch(y, i, ACS_BLOCK);
        }
    }
    refresh();
}

void * animation_thread(){
    sleep(1);
    while(1){
        int mags[samplesize/2];

        pthread_mutex_lock(&fftmutex);
        for(int i = 0; i < samplesize/2; i++){
            mags[i] = shared_data[i];
        }
        pthread_mutex_unlock(&fftmutex);

        draw_viz(mags, samplesize/2);

        pthread_mutex_lock(&spotifymutex);

        strcpy(currsonginfo, spotifybuffer);

        pthread_mutex_unlock(&spotifymutex);

        writesongartist(currsonginfo);

        refresh();
        usleep(25000);
    }
}
typedef struct {
    FILE * spotifile;
} SpotifyThreadArgs;

void * spotify_read_thread(void *args) {
    SpotifyThreadArgs * threadArgs = (SpotifyThreadArgs *)args;

    int flags = fcntl(fileno(threadArgs->spotifile), F_GETFL, 0);
    fcntl(fileno(threadArgs->spotifile), F_SETFL, flags | O_NONBLOCK);

    char buffer[1024];
    while (1) {
        pthread_mutex_lock(&spotifymutex);

        // read from the py script without blocking
        if (fgets(buffer, sizeof(buffer), threadArgs->spotifile) != NULL) {
            // read new data
            if(strlen(buffer) > 0 && strcmp(buffer, "\n") != 0){
                strcpy(spotifybuffer, buffer);
            }
        } else {
            // no data read; check if end-of-file or just no data available
            if (feof(threadArgs->spotifile)) {
                // EOF reached; handle end-of-file 
                clearerr(threadArgs->spotifile);  // clear the EOF flag
            } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
                // error
                perror("error reading from python");
                pthread_mutex_unlock(&spotifymutex);
                break;
            }
            // here-> no new data; retain buffer
        }

        pthread_mutex_unlock(&spotifymutex);

        usleep(500000);
    }

    return NULL;
}

int main() {
    //initialize
    init_curses();
    pthread_mutex_init(&fftmutex, NULL);
    pthread_mutex_init(&spotifymutex, NULL);

    for(int i = 0; i < samplesize/2; i++){
        shared_data[i] = 0;
    }
    for(int i = 0; i < sizeof(spotifybuffer); i++){
        spotifybuffer[i] = '\0';
    }

    pthread_t tid;
    int pipefd[2];
    int thread_init = pthread_create(&tid, NULL, animation_thread, NULL);

    //pipe to capture the output
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    const char * home = getenv("HOME");

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) { // child
        // not reading
        close(pipefd[0]);

        // redirect stdout 
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // initialize tap from swift
        const char * swiftpath = "/dev/wiretapmk2/main";

        size_t pathlen = strlen(home) + strlen(swiftpath) + 1;
        char * abspath = malloc(pathlen);
        strcpy(abspath, home);
        strcat(abspath, swiftpath);

        execlp(abspath, "main", "-", NULL);

        // If execlp fails
        perror("execlp");
        free(abspath);
        exit(EXIT_FAILURE);
    } else { // parent
        // manage tap pipe
        // not writing
        close(pipefd[1]);
        FILE * tapreadend = fdopen(pipefd[0], "r"); // open pipe read as file interface

        // popen python process for song / artist data
        FILE * spotifile;

        char * venvpath = "/dev/wiretapmk2/python/venv/bin/python";
        char * pypath = "/dev/wiretapmk2/python/spotify_req.py";

        size_t homelen = strlen(home);

        char *absvenv = malloc(homelen + strlen(venvpath) + 1);
        char *abspy = malloc(homelen + strlen(pypath) + 1);

        strcpy(absvenv, home);
        strcat(absvenv, venvpath); // absvenv now contains the absolute path to the virtual py interpreter

        strcpy(abspy, home);
        strcat(abspy, pypath); // abspy now contains the absolute path to the py script

        // final command string
        char *abspythonpath = malloc(strlen(absvenv) + strlen(abspy) + 2);  // +2 for space and null terminator
        sprintf(abspythonpath, "%s %s", absvenv, abspy);

        spotifile = popen(abspythonpath, "r");

        free(absvenv);
        free(abspy);
        free(abspythonpath);

        if (spotifile == NULL) {
            perror("popen failed");
            return 1;
        }

        pthread_t pyhtonthread;
        SpotifyThreadArgs threadArgs = {spotifile};

        if (pthread_create(&pyhtonthread, NULL, spotify_read_thread, &threadArgs) != 0) {
            perror("pthread_create failed");
            pclose(spotifile);
            return 1;
        }

        while(1){
            // read sample from audio tap
            double amplitudes[samplesize]; // init buffer
            int count = 0;

            while (count < samplesize) {
                char buffer[32];
                if(fgets(buffer, sizeof(buffer), tapreadend) != NULL){
                    // convert parsed to double and store
                    double value = atof(buffer);
                    amplitudes[count] = value;
                    //printf("Parsed float: %f\n", value);

                    count++;
                }else{
                    //break;
                    perror("fgets null, audio tap failure/desync");
                    exit(EXIT_FAILURE);
                }
            }

            // fftw initialization
            fftw_complex *in, *out;
            fftw_plan p;

            in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * samplesize);
            out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * samplesize);

            for(int i = 0; i < samplesize; i++){
                in[i][0] = amplitudes[i]; // real part
                in[i][1] = 0.0; // real input, no complex 
            }

            // go go gadget fft
            p = fftw_plan_dft_1d(samplesize, in, out, FFTW_FORWARD, FFTW_ESTIMATE); 
            fftw_execute(p);

            // init magnitudes
            double magnitudes[samplesize/2]; 

            for (int i = 0; i < samplesize/2; i++) {
                magnitudes[i] = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
            }

            // done with fft
            fftw_destroy_plan(p);
            fftw_free(in);
            fftw_free(out);

            z_score_normalization(magnitudes, samplesize/2);

            pthread_mutex_lock(&fftmutex);
            for (int i = 0; i < samplesize/2; i++) {
                shared_data[i] = (int) (sqrt(magnitudes[i]) * 10);
            }
            pthread_mutex_unlock(&fftmutex);
        }

        // Wait for the child process to finish
        waitpid(pid, NULL, 0);
    }

    endwin();
    return 0;
}