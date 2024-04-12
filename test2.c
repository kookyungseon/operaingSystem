#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>
#include <sys/time.h>

#define MAX_FILENAME_LEN 1024
#define MAX_CMD_LEN 2048
#define MAX_PATH_LEN 1024
#define MAX_LINE_LEN 4096

// 함수 선언
void compile_and_execute(char *pathname, char *input_dir, char *answer_dir, char *target_src, int time_limit);
void print_usage();
int is_valid_input_file(char *filename);

// 변수 선언
int total_compile_error = 0;
int total_runtime_error = 0;
int total_correct = 0;
int total_timeout = 0;
int total_wrong = 0;
int total_runtime = 0;

// 메인 함수
int main(int argc, char *argv[]) {
    char *input_dir = NULL;
    char *answer_dir = NULL;
    int time_limit = 10; // 기본값은 10초

    int opt;
    while ((opt = getopt(argc, argv, "i:a:t:")) != -1) {
        switch (opt) {
            case 'i':
                input_dir = optarg;
                break;
            case 'a':
                answer_dir = optarg;
                break;
            case 't':
                time_limit = atoi(optarg);
                if (time_limit <= 0) {
                    fprintf(stderr, "Error: Invalid time limit\n");
                    return EXIT_FAILURE;
                }
                break;
            default:
                print_usage();
                return EXIT_FAILURE;
        }
    }

    if (input_dir == NULL || answer_dir == NULL || optind >= argc) {
        print_usage();
        return EXIT_FAILURE;
    }

    for (int i = optind; i < argc; i++) {
        compile_and_execute(argv[i], input_dir, answer_dir, argv[i], time_limit);
    }

    // 결과 출력
    printf("Compile Error: %d\n", total_compile_error);
    printf("Timeout: %d\n", total_timeout);
    printf("Runtime Error: %d\n", total_runtime_error);
    printf("Wrong Answer: %d/%d\n", total_wrong, total_correct + total_wrong);
    printf("Correct: %d milliseconds\n", total_runtime);

    return EXIT_SUCCESS;
}

// compile_and_execute 함수 정의
void compile_and_execute(char *pathname, char *input_dir, char *answer_dir, char *target_src, int time_limit) {
    // 파일 실행 시작 시간 기록
    struct timeval start, end;
    gettimeofday(&start, NULL);

    char compile_command[MAX_CMD_LEN];
    char execute_command[MAX_CMD_LEN];
    char filename[MAX_FILENAME_LEN];
    char input_path[MAX_PATH_LEN];
    char output_path[MAX_PATH_LEN];
    char answer_path[MAX_PATH_LEN];
    FILE *input_file;
    FILE *output_file;
    FILE *answer_file;
    int compile_status;

    snprintf(compile_command, sizeof(compile_command), "gcc %s -o %s.out", target_src, pathname);
    compile_status = system(compile_command);

    if (compile_status != 0) {
        fprintf(stderr, "%s: Compile Error\n", pathname);
        total_compile_error++;
        return;
    }

    DIR *dir;
    struct dirent *entry;
    if ((dir = opendir(input_dir)) != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG && is_valid_input_file(entry->d_name)) {
                snprintf(filename, sizeof(filename), "%s", entry->d_name);

                snprintf(input_path, sizeof(input_path), "%s/%s", input_dir, filename);
                snprintf(output_path, sizeof(output_path), "output%s", filename);
                snprintf(answer_path, sizeof(answer_path), "%s/%s", answer_dir, filename);

                int fd[2];
                if (pipe(fd) == -1) {
                    fprintf(stderr, "Error: Pipe creation failed\n");
                    return;
                }

                pid_t pid = fork();

                if (pid == -1) {
                    fprintf(stderr, "Error: Fork failed\n");
                    return;
                } else if (pid == 0) { 
                    close(fd[0]); 
                    dup2(fd[1], STDOUT_FILENO); 
                    close(fd[1]); 

                    snprintf(execute_command, sizeof(execute_command), "./%s.out < %s", pathname, input_path);
                    system(execute_command);

                    exit(EXIT_SUCCESS);
                } else { 
                    close(fd[1]); 

                    struct timeval timeout;
                    timeout.tv_sec = time_limit;
                    timeout.tv_usec = 0;

                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(fd[0], &fds);

                    int ret = select(fd[0] + 1, &fds, NULL, NULL, &timeout);
                    if (ret == -1) {
                        fprintf(stderr, "Error: Select error\n");
                        return;
                    } else if (ret == 0) { 
                        fprintf(stderr, "%s: Error Execution timed out\n", filename);
                        total_timeout++;
                        close(fd[0]); 
                        kill(pid, SIGKILL); 
                        waitpid(pid, NULL, 0); 
                        continue; 
                    } 

                    output_file = fopen(output_path, "w");
                    if (output_file == NULL) {
                        fprintf(stderr, "Error: Unable to open output file\n");
                        return;
                    }

                    char buffer[MAX_LINE_LEN];
                    ssize_t bytes_read;
                    int isEmpty = 1;
                    while ((bytes_read = read(fd[0], buffer, sizeof(buffer))) > 0) {
                        isEmpty = 0;
                        fwrite(buffer, 1, bytes_read, output_file);
                    }
                    fclose(output_file);
                    close(fd[0]); 

                    if (isEmpty) {
                        fprintf(stderr, "%s: Error Empty input file\n", filename);
                        kill(pid, SIGKILL);
                        waitpid(pid, NULL, 0);
                        continue;
                    }

                    if ((input_file = fopen(output_path, "r")) == NULL || (answer_file = fopen(answer_path, "r")) == NULL) {
                        fprintf(stderr, "Error: Unable to open file for comparison\n");
                        return;
                    }

                    int correct = 1;
                    int ch1, ch2;
                    while ((ch1 = fgetc(input_file)) != EOF && (ch2 = fgetc(answer_file)) != EOF) {
                        if (ch1 != ch2) {
                            correct = 0;
                            break;
                        }
                    }

                    fclose(input_file);
                    fclose(answer_file);

                    if (correct)
                        total_correct++;
                    else
                        total_wrong++;
                }
            }
        }
        closedir(dir);
    } else {
        fprintf(stderr, "Error: Unable to open directory\n");
    }

    // 파일 실행 종료 시간 기록 및 총 실행 시간에 추가
    gettimeofday(&end, NULL);
    int runtime = ((end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000);
    total_runtime += runtime;
}

// print_usage 함수 정의
void print_usage() {
    fprintf(stderr, "Usage: ./a.out -i input_dir -a answer_dir -t time target_src1\n");
}

// is_valid_input_file 함수 정의
int is_valid_input_file(char *filename) {
    if (strcmp(filename, ".DS_Store") == 0) {
        return 0; 
    }
    return 1; 
}

