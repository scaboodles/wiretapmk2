#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fftw3.h>
#include <math.h>
#include <ncurses.h>

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
void min_max_normalization(double arr[], int size) { 
    double min_val = arr[0];
    double max_val = arr[0];

    for (int i = 1; i < size; ++i) {
        if (arr[i] < min_val) {
            min_val = arr[i];
        }
        if (arr[i] > max_val) {
            max_val = arr[i];
        }
    }

    for (int i = 0; i < size; ++i) {
        arr[i] = (arr[i] - min_val) / (max_val - min_val);
    }
}

void draw_viz(long int * mags, int num_frames){
    clear();
    for(int i = 0; i < num_frames; i++){
        for(int j = 0; j < mags[i]; j++){
            int y = LINES - 1 - j;
            mvaddch(y, i, ACS_BLOCK);
        }
    }
    refresh();
}

int main() {
    //initialize
    init_curses();
    // pipe to capture the output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

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
        execlp("./main", "main", "-", NULL);

        // If execlp fails
        perror("execlp");
        exit(EXIT_FAILURE);
    } else { // parent

        // not writing
        close(pipefd[1]);
        ssize_t bytesRead;
        char buffer[1024];
        // continuously read output from audio tap
        while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            // tokenize and read
            double amplitudes[128]; // init buffer
            char *token = strtok(buffer, "\n");
            int count = 0;
            while (token != NULL) {
                if(strcmp(token, "end" ) == 0){
                    break;
                }else{
                    // convert parsed to float
                    double value = atof(token);
                    amplitudes[count] = value;
                    //printf("Parsed float: %f\n", value);

                    // get next line
                    count++;
                    token = strtok(NULL, "\n");
                }
            }

            // fftw initialization
            fftw_complex *in, *out;
            fftw_plan p;

            in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * count);
            out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * count);

            for(int i = 0; i < count; i++){
                in[i][0] = amplitudes[i]; // real part
                in[i][1] = 0.0; // real input, no complex 
            }

            // go go gadget fft
            p = fftw_plan_dft_1d(count, in, out, FFTW_FORWARD, FFTW_ESTIMATE); 
            fftw_execute(p);

            // init magnitudes
            double * magnitudes = malloc(sizeof(double) * count);

            for (int i = 0; i < count; i++) {
                magnitudes[i] = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
            }

            // normalize magnitudes

            for (int i = 0; i < count; i++) {
                magnitudes[i] = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
            }

            // done with fft
            fftw_destroy_plan(p);
            fftw_free(in);
            fftw_free(out);

            //min_max_normalization(magnitudes, count);

            long int * scaled_mags = malloc(sizeof(long int) * count);
            int scale_factor = LINES - 1;

            for (int i = 0; i < count; i++) {
                scaled_mags[i] = round(scale_factor * magnitudes[i]);
            }

            free(magnitudes);

            draw_viz(scaled_mags, count);
            free(scaled_mags);
        }

        if (bytesRead == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        // Wait for the child process to finish
        waitpid(pid, NULL, 0);
    }

    return 0;
}