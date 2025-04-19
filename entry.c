#define _XOPEN_SOURCE_EXTENDED

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <locale.h>
#include <errno.h>

#include <fcntl.h>
#include <pthread.h>

#include <fftw3.h>
#include <ncurses.h>
#include <wchar.h>


volatile sig_atomic_t resize = 0;
const int samplesize = 1500;
float shared_data[samplesize/2];
char spotifybuffer[1024];
char currsonginfo[1024];

wchar_t * lowerQ = L"▂";
wchar_t * lowerH = L"▄";
wchar_t * lower3Q = L"▆";

cchar_t lowerQcc;
cchar_t lowerHcc;
cchar_t lower3Qcc;

cchar_t upperQcc;
cchar_t upperHcc;
cchar_t upper3Qcc;

float * vizState;
int prevCols;

WINDOW *buffer_win = NULL;

void resizeflag(int sig){
    resize = 1;
}

pthread_mutex_t fftmutex;
pthread_mutex_t spotifymutex;

int init_curses(){
    initscr(); cbreak(); noecho(); // curses bs 
    curs_set(0); // hide cursor
    timeout(-1); // blocking getch
    setlocale(LC_ALL, ""); // w i d e  characters1
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE);

    setcchar(&lowerQcc, lowerQ, A_NORMAL, 0, NULL);
    setcchar(&lowerHcc, lowerH, A_NORMAL, 0, NULL);
    setcchar(&lower3Qcc, lower3Q, A_NORMAL, 0, NULL);

    setcchar(&upperQcc, lowerQ, A_NORMAL, 1, NULL);
    setcchar(&upperHcc, lowerH, A_NORMAL, 1, NULL);
    setcchar(&upper3Qcc, lower3Q, A_NORMAL, 1, NULL);

    //if(LINES <= HEIGHT / 2 || COLS <= WIDTH){
        //printw("Window too small\npress any key to continue");
        //refresh();
        //getch();
        //endwin();
        //return 1;
    //}

    buffer_win = newwin(LINES, COLS, 0, 0);
    leaveok(buffer_win, TRUE);
    keypad(buffer_win, TRUE);

    vizState = calloc(COLS, sizeof(float));
    prevCols = COLS;
    return 0;
}
void resizeVizState(){
    float * tempState = calloc(COLS, sizeof(float));
    if(prevCols < COLS){
        for(int i = 0; i < prevCols; i++){
            tempState[i] = vizState[i];
        }
    }else{
        for(int i = 0; i < COLS; i++){
            tempState[i] = vizState[i];
        }
    }

    free(vizState);
    vizState = tempState;
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

void dymanic_max_norm(double magnitudes[], int size){
    double max_val = 0;
    for (int i = 0; i < size; ++i) {
        if (magnitudes[i] > max_val) max_val = magnitudes[i];
    }
    if (max_val > 0) {
        for (int i = 0; i < size; ++i) {
            magnitudes[i] /= max_val;
        }
    }
}

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

void logscale(double arr[], int size){
    for (int i = 0; i < size; ++i) {
        arr[i] = logf(arr[i] + 1);
    }
}

void balanced_audio_normalization(double magnitudes[], int size) {
    for (int i = 0; i < size; ++i) {
        magnitudes[i] = log10(magnitudes[i] + 1);
    }
    
    // reduce bass bias
    for (int i = 0; i < size; ++i) {
        double freq_factor = (double)i / size;  
        
        double weight = 0.5 + freq_factor;
        magnitudes[i] *= weight;
    }

    dymanic_max_norm(magnitudes, size);
    
    double contribution_scale = LINES * 0.1; // 5% of window height per frame
    
    for (int i = 0; i < size; ++i) {
        magnitudes[i] *= contribution_scale;
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

void dampState(){
    for(int i = 0; i < COLS; i++){
        if(vizState[i] != 0.0f){
            vizState[i] *= 0.9f;
        }
    }
}

void updateState(float * mags, int num_frames){
    for(int i = 0; i < num_frames; i++){
        if(i >= COLS){
            break;
        }
        //vizState[i] = 0.8f * vizState[i] + 0.8f * mags[i];
        vizState[i] += mags[i];
    }
}

void draw_viz(float * mags, int num_frames){
    if(resize){
        endwin();
        refresh();
        resize = 0;

        delwin(buffer_win);
        buffer_win = newwin(LINES, COLS, 0, 0);
        leaveok(buffer_win, TRUE);
        keypad(buffer_win, TRUE);

        resizeVizState();
    }

    werase(buffer_win);

    int midline = (LINES - 1) / 2;

    dampState();
    updateState(mags, num_frames);

    for(int i = 0; i < num_frames; i++){
        if(i >= COLS){
            break;
        }
        float j = vizState[i]/2;
        int iter = 0;
        for(; j > 1; j--){
            int y = midline - iter;
            mvwaddch(buffer_win, y, i, ACS_BLOCK);
            iter++;
        }

        int y = midline - iter;
        if(j > .75){
            mvwadd_wch(buffer_win, y, i, &lower3Qcc);
        }else if(j > .5){
            mvwadd_wch(buffer_win, y, i, &lowerHcc);
        }else if(j > .25){
            mvwadd_wch(buffer_win, y, i, &lowerQcc);
        }

        j = vizState[i]/2;
        iter = 0;
        for(; j > 1; j--){
            int y = midline + iter + 1;
            mvwaddch(buffer_win, y, i, ACS_BLOCK);
            iter++;
        }

        y = midline + iter + 1;
        if(j > .75){
            mvwadd_wch(buffer_win, y, i, &upperQcc);
        }else if(j > .5){
            mvwadd_wch(buffer_win, y, i, &upperHcc);
        }else if(j > .25){
            mvwadd_wch(buffer_win, y, i, &upper3Qcc);
        }
    }
    wnoutrefresh(buffer_win);
    doupdate();
}

void * animation_thread(){
    sleep(1);
    while(1){
        float mags[samplesize/2];

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
        usleep(33000);
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
    signal(SIGWINCH, resizeflag);

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

            //z_score_normalization(magnitudes, samplesize/2);
            //dymanic_max_norm(magnitudes, samplesize/2);
            //logscale(magnitudes, samplesize/2);
            balanced_audio_normalization(magnitudes, samplesize/2);

            pthread_mutex_lock(&fftmutex);
            for (int i = 0; i < samplesize/2; i++) {
                shared_data[i] = (float) fabs(magnitudes[i]);
            }
            pthread_mutex_unlock(&fftmutex);
        }

        waitpid(pid, NULL, 0);
    }

    endwin();
    return 0;
}