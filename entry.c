#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fftw3.h>

int main() {
    // Create a pipe to capture the output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Fork to create a child process
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) { // Child process
        // Close the read end of the pipe since we are going to write to it
        close(pipefd[0]);

        // Redirect stdout 
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Execute the Swift program
        execlp("./main", "main", "-", NULL);

        // If execlp fails
        perror("execlp");
        exit(EXIT_FAILURE);
    } else { // Parent process
        close(pipefd[1]);
        //Continuously print and read from pipe  ~MISSING
        char buffer[1024];
        ssize_t bytesRead;

        while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            // tokenize and read
            double amplitudes[128];
            char *token = strtok(buffer, "\n");
            int count = 0;
            while (token != NULL) {
                if(strcmp(token, "end" ) == 0){
                    printf("%d frames", count);
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
            fftw_complex *in, *out;
            fftw_plan p;

            in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * count);
            out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * count);

            for(int i = 0; i < count; i++){
                in[i][0] = amplitudes[i]; // real part
                in[i][1] = 0.0; // real input, no complex 
            }

            p = fftw_plan_dft_1d(count, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
            fftw_execute(p);

            printf("FFT result:\n");
            for (int i = 0; i < count; i++) {
                printf("%f + %fi\n", out[i][0], out[i][1]);
            }

            fftw_destroy_plan(p);
            fftw_free(in);
            fftw_free(out);
            exit(0);
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