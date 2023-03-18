#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h> // for open() and close()

#define MAX_LEN 128
#define MAX_PATHS 10

int main(int argc, char **argv) {
  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  char *paths[MAX_PATHS];
  int path_count = 0;
  FILE *fp;

  // initialize the first element of the paths array to "/bin"
  paths[path_count++] = "/bin";

  if (argc == 2) {
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
      perror("fopen");
      exit(EXIT_FAILURE);
    }
    while ((read = getline(&line, &len, fp)) != -1) {
      if (read == -1) {
        break;
      }

      // Remove the newline character from the end of the line
      line[read - 1] = '\0';

      char *s2, *cmd;
      s2 = (char *) malloc(20);
      if (s2 == NULL) {
        printf("Failed to allocate memory.\n");
        continue;
      }
      strcpy(s2, line);

      cmd = strsep(&s2, " ");

      if (strcmp(cmd, "exit") == 0) {
        exit(0);
      } else if (strcmp(cmd, "cd") == 0) {
        // cd only takes one argument
        char *dir;
        // take cd off of line
        strsep(&line, " ");
        dir = strsep(&line, " ");

        if (dir == NULL) {
          fprintf(stderr, "An error has occurred\n");
          return 1;
        }

        if (chdir(dir) != 0) {
          printf("Could not change directory.\n");
        }

        char s[100];

        // printing current working directory
        printf("%s\n", getcwd(s, 100));

        // printf("cd command has been entered\n");
      } else if (strcmp(cmd, "path") == 0) {
        // take path off of line
        strsep(&line, " ");

        // reset the paths array and path_count
        memset(paths, 0, sizeof(paths));
        path_count = 0;

        // store the paths in the paths array
        char *token;
        char *s2_copy = s2;
        while ((token = strsep(&s2_copy, " ")) != NULL) {
          if (path_count < MAX_PATHS) {
            paths[path_count++] = token;
          } else {
            printf("Maximum number of paths reached.\n");
            break;
          }
        }

        // Handle the case where the user sets path to be empty
        if (path_count == 0) {
          paths[path_count++] = "/bin";
        } else if (path_count == 1) {
          paths[path_count++] = "/usr/bin";
        }

        // Check if both paths are valid
        int valid_paths = 0;
        for (int i = 0; i < path_count; i++) {
          if (access(paths[i], F_OK) == 0) {
            valid_paths++;
          } else {
            printf("Invalid path: %s\n", paths[i]);
          }
        }
        if (valid_paths == 2) {
          printf("Both paths are valid.\n");
        }
      } else {
        // Check if the command line contains ampersand characters
        char *ampersand_pos = strchr(line, '&');
        if (ampersand_pos == NULL) {
          pid_t pid;
          char *token;
          int i = 0;
          char *parmList[MAX_LEN];

          token = strtok(line, " ");
          while (token != NULL) {
            parmList[i++] = token;
            token = strtok(NULL, " ");
          }
          parmList[i] = NULL;

          char *output_file = NULL;
          int fd;
          for (int j = 0; j < i; j++) {
            if (strcmp(parmList[j], ">") == 0) {
              output_file = parmList[j + 1];
              parmList[j] = NULL;
              break;
            }
          }

          if ((pid = fork()) == -1) {
            perror("fork error");
            line = NULL;
            len = 0;
            continue;
          } else if (pid == 0) {
            if (output_file != NULL) {
              fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
              if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
              }
              if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
              }
              close(fd);
            }
            char path[MAX_LEN];
            snprintf(path, sizeof(path), "/bin/%s", parmList[0]);
            if (access(path, F_OK) == -1) {
              printf("access error\n");
              exit(EXIT_FAILURE);
            }
            if (execv(path, parmList) < 0) {
              printf("Could not execute command.\n");
              exit(EXIT_FAILURE);
            }
          }

          int status;
          waitpid(pid, &status, 0);
          if (status != 0) {
            printf("The command returned with a non-zero status.\n");
          }
        } else {
          // The command line contains ampersand characters
          // Split the line into separate commands and execute them in parallel
          char *token;
          int num_commands = 0;
          char *commands[MAX_LEN];

          token = strtok(line, "&");
          while (token != NULL) {
            // Remove leading and trailing spaces from the command
            while (*token == ' ') {
              token++;
            }
            int len = strlen(token);
            while (len > 0 && token[len - 1] == ' ') {
              token[--len] = '\0';
            }

            commands[num_commands++] = token;
            token = strtok(NULL, "&");
          }

          // Execute the commands in parallel
          int i;
          for (i = 0; i < num_commands; i++) {
            pid_t pid = fork();
            if (pid == -1) {
              perror("fork error");
              exit(EXIT_FAILURE);
            } else if (pid == 0) {
              // Child process
              char *cmd = strtok(commands[i], " ");
              int j = 0;
              char *params[MAX_LEN];
              params[j++] = cmd;

              // Parse the parameters for the command
              while ((cmd = strtok(NULL, " ")) != NULL) {
                params[j++] = cmd;
              }
              params[j] = NULL;

              // Execute the command
              char path[MAX_LEN];
              int k;
              for (k = 0; k < path_count; k++) {
                snprintf(path, sizeof(path), "%s/%s", paths[k], params[0]);
                if (access(path, F_OK) == 0) {
                  execv(path, params);
                  break;
                }
              }

              // If we reach here, the command was not found
              printf("Command not found: %s\n", params[0]);
              exit(EXIT_FAILURE);
            }
          }

          // Wait for all child processes to finish
          for (i = 0; i < num_commands; i++) {
            int status;
            wait(&status);
            if (status != 0) {
              printf("Command returned with a non-zero status.\n");
            }
          }
        }
      }

      if (line != NULL) {
//      free(line);
        line = NULL;
        len = 0;
      }
    }
  } else {
    while (1) {
      printf("wish> ");
      read = getline(&line, &len, stdin);

      if (read == -1) {
        break;
      }

      // Remove the newline character from the end of the line
      line[read - 1] = '\0';

      char *s2, *cmd;
      s2 = (char *) malloc(20);
      if (s2 == NULL) {
        printf("Failed to allocate memory.\n");
        continue;
      }
      strcpy(s2, line);

      cmd = strsep(&s2, " ");

      if (strcmp(cmd, "exit") == 0) {
        exit(0);
      } else if (strcmp(cmd, "cd") == 0) {
        // cd only takes one argument
        char *dir;
        // take cd off of line
        strsep(&line, " ");
        dir = strsep(&line, " ");

        if (dir == NULL) {
          printf("Could not find the executable file.\n");
          continue;
        }

        if (chdir(dir) != 0) {
          printf("Could not change directory.\n");
        }

        char s[100];

        // printing current working directory
        printf("%s\n", getcwd(s, 100));

        // printf("cd command has been entered\n");
      } else if (strcmp(cmd, "path") == 0) {
        // take path off of line
        strsep(&line, " ");

        // reset the paths array and path_count
        memset(paths, 0, sizeof(paths));
        path_count = 0;

        // store the paths in the paths array
        char *token;
        char *s2_copy = s2;
        while ((token = strsep(&s2_copy, " ")) != NULL) {
          if (path_count < MAX_PATHS) {
            paths[path_count++] = token;
          } else {
            printf("Maximum number of paths reached.\n");
            break;
          }
        }

        // Handle the case where the user sets path to be empty
        if (path_count == 0) {
          paths[path_count++] = "/bin";
        } else if (path_count == 1) {
          paths[path_count++] = "/usr/bin";
        }

        // Check if both paths are valid
        int valid_paths = 0;
        for (int i = 0; i < path_count; i++) {
          if (access(paths[i], F_OK) == 0) {
            valid_paths++;
          } else {
            printf("Invalid path: %s\n", paths[i]);
          }
        }
        if (valid_paths == 2) {
          printf("Both paths are valid.\n");
        }
      } else {
        // Check if the command line contains ampersand characters
        char *ampersand_pos = strchr(line, '&');
        if (ampersand_pos == NULL) {
          pid_t pid;
          char *token;
          int i = 0;
          char *parmList[MAX_LEN];

          token = strtok(line, " ");
          while (token != NULL) {
            parmList[i++] = token;
            token = strtok(NULL, " ");
          }
          parmList[i] = NULL;

          char *output_file = NULL;
          int fd;
          for (int j = 0; j < i; j++) {
            if (strcmp(parmList[j], ">") == 0) {
              output_file = parmList[j + 1];
              parmList[j] = NULL;
              break;
            }
          }

          if ((pid = fork()) == -1) {
            perror("fork error");
            line = NULL;
            len = 0;
            continue;
          } else if (pid == 0) {
            if (output_file != NULL) {
              fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
              if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
              }
              if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
              }
              close(fd);
            }
            char path[MAX_LEN];
            snprintf(path, sizeof(path), "/bin/%s", parmList[0]);
            if (access(path, F_OK) == -1) {
              printf("access error\n");
              exit(EXIT_FAILURE);
            }
            if (execv(path, parmList) < 0) {
              printf("Could not execute command.\n");
              exit(EXIT_FAILURE);
            }
          }

          int status;
          waitpid(pid, &status, 0);
          if (status != 0) {
            printf("The command returned with a non-zero status.\n");
          }
        } else {
          // The command line contains ampersand characters
          // Split the line into separate commands and execute them in parallel
          char *token;
          int num_commands = 0;
          char *commands[MAX_LEN];

          token = strtok(line, "&");
          while (token != NULL) {
            // Remove leading and trailing spaces from the command
            while (*token == ' ') {
              token++;
            }
            int len = strlen(token);
            while (len > 0 && token[len - 1] == ' ') {
              token[--len] = '\0';
            }

            commands[num_commands++] = token;
            token = strtok(NULL, "&");
          }

          // Execute the commands in parallel
          int i;
          for (i = 0; i < num_commands; i++) {
            pid_t pid = fork();
            if (pid == -1) {
              perror("fork error");
              exit(EXIT_FAILURE);
            } else if (pid == 0) {
              // Child process
              char *cmd = strtok(commands[i], " ");
              int j = 0;
              char *params[MAX_LEN];
              params[j++] = cmd;

              // Parse the parameters for the command
              while ((cmd = strtok(NULL, " ")) != NULL) {
                params[j++] = cmd;
              }
              params[j] = NULL;

              // Execute the command
              char path[MAX_LEN];
              int k;
              for (k = 0; k < path_count; k++) {
                snprintf(path, sizeof(path), "%s/%s", paths[k], params[0]);
                if (access(path, F_OK) == 0) {
                  execv(path, params);
                  break;
                }
              }

              // If we reach here, the command was not found
              printf("Command not found: %s\n", params[0]);
              exit(EXIT_FAILURE);
            }
          }

          // Wait for all child processes to finish
          for (i = 0; i < num_commands; i++) {
            int status;
            wait(&status);
            if (status != 0) {
              printf("Command returned with a non-zero status.\n");
            }
          }
        }
      }

      if (line != NULL) {
//      free(line);
        line = NULL;
        len = 0;
      }
    }
  }


  return 0;
}