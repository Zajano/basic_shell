/*******************************************************************************
** Program:		smallsh
** Author:      Zachary Jaffe-Notier
** Date:        Feb 24, 2020
** Description: simple shell with 3 built-in commands: exit, cd, and status
** Sources:
** 1. fgets		https://stackoverflow.com/questions/46146240
**				/why-does-alarm-cause-fgets-to-stop-waiting
** 2. pid str	https://stackoverflow.com/questions/15262315
**				/how-to-convert-pid-t-to-string
** 3. dev/null	https://stackoverflow.com/questions/14846768
**				/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect
** 4. more, but lost track...
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>		//errno for checking input
#include <fcntl.h>		//file control
#include <signal.h>     //sigactions
#include <unistd.h>     //exec
#include <sys/stat.h>   //stat
#include <sys/types.h>  //pid_t
#include <sys/wait.h>   //waitpid

//globals
int exit_stat = 0;		// exit smallsh on 1
int fg_mode = 0; 		// foreground-only mode == 1
int bg_process = 0;		// if 1, meant to be background process
char user_input[2049];	// max chars + enter
char* user_args[512];	// array of arguments
char input_file[255];	// file name for redirect
char output_file[255];
int status_code = -5;	// track exit status of processess
//int num_procs = 0;	// number of processess
int in_red = 0; 		// 1 = input redirect
int out_red = 0;		// 1 = output redirect

//checks whether to enter or exit foreground-only mode
void check_SIGTSTP()
{
    //currently in bg mode
    if (fg_mode == 0)
    {
        fg_mode = 1; //now in fg-only mode
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(1, message, 50);
		message = ": ";
        write(1, message, 2);
        fflush(stdout); //ensure output
    }
    else //swtich back
    {
        fg_mode = 0;
        char* message = "\nExiting foreground-only mode\n";
        write(1, message, 30);
		message = ": ";
		write(1, message, 2);
        fflush(stdout);
    }
}

//to reset values before new loops
void reset_defaults()
{
	int i;
	bg_process = 0;
	in_red = 0;		//1 = trigger input redirect
	out_red = 0;	//1 = trigger output redirect
	memset(user_input, sizeof(user_input), '\0');
	memset(input_file, sizeof(input_file), '\0');
	memset(output_file, sizeof(output_file), '\0');
	for (i = 0; i < 512; i++)
	{
		user_args[i] = NULL;
	}
}

int main()
{
  //variables for everything
  int i; //for loops
  //int file_result; //for opening file check
  //pid_t current_pid = -5; //for child processes

  /*********** SIG-ACTION CONSTRUCTION ***************/
  //initialize all values in sigactions as empty
  struct sigaction SIGINT_action = {{0}};
  struct sigaction SIGTSTP_action = {{0}};
  struct sigaction SIGTSTP_child = {{0}};
  struct sigaction SIGINT_child = {{0}};

  //assign functions to handlers
  SIGINT_action.sa_handler = SIG_IGN;       //ignore
  SIGTSTP_action.sa_handler = check_SIGTSTP;  //foreground/background mode switch
  SIGTSTP_child.sa_handler = SIG_IGN;		//ignore in child process
  SIGINT_child.sa_handler = SIG_DFL;		//default interrupt

  //fill masks to ignore
  sigfillset(&SIGINT_action.sa_mask);
  sigfillset(&SIGTSTP_action.sa_mask);
  sigfillset(&SIGINT_child.sa_mask);
  sigfillset(&SIGTSTP_child.sa_mask);

  //special flags for actions, should already be 0
  SIGINT_action.sa_flags = 0;
  SIGTSTP_action.sa_flags = 0;
  SIGINT_child.sa_flags = 0;
  SIGTSTP_child.sa_flags = 0;

  //assign signals to catch for forground
  sigaction(SIGINT, &SIGINT_action, NULL);
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
  /****************************************************/

  //start running until meant to exit
  while(exit_stat != 1)
  {
	//reset input and arguments
	reset_defaults();

    //prompt and flush to ensure output
    printf (": ");
    fflush(stdout);

	//get input string
	//from source #1, fgets fix
	//int errno;
	do {
		errno = 0;
		fgets(user_input, sizeof(user_input), stdin);
	} while (EINTR == errno);
	user_input[strlen(user_input)-1] = '\0'; //remove newline

	/***********************GET AND USE ARGUMENTS******************/
	//pid to string, for $$ expansion
	//from source #2
	char pid[10];
	sprintf(pid, "%d", getpid());

	//for fork and background PID check
	pid_t child_pid = -5;

	//check for empty input
	if(strcmp(user_input, "") == 0)
	{
		fflush(stdout); //get PID messages
		continue;//do nothing
		//ignore = 1;
	}
	else //parse input and execute
	{
		//get first arg and prepare for array
		char* argument = strtok(user_input, " ");
		int num_args = 0;

		//check for comment line
		if(argument[0] == '#')
		{
			fflush(stdout); //get PID messages
			continue;//do nothing
		}
		else //get arguments from line
		{
			//fill argument array
			while (argument != NULL && num_args < 512)
			{
				//output flag and save location
				if(strcmp(argument, ">") == 0)
				{
					out_red = 1;
					//next argument
					argument = strtok(NULL, " "); //null to use same string

					//check for valid input and copy
					if (strcmp(argument, "") != 0 && argument != NULL)
					{
						strcpy(output_file, argument);
					}
				}
				//input flag and save location
				else if(strcmp(argument, "<") == 0)
				{
					in_red = 1;
					//next argument
					argument = strtok(NULL, " "); //null to use same string

					//check for valid input and copy
					if (strcmp(argument, "") != 0 && argument != NULL)
					{
						strcpy(input_file, argument);
					}
				}
				//other arguments
				else
				{
					//copy arg in to array
					user_args[num_args] = strdup(argument);

					//expand if $$, from souce 2
					if(user_args[num_args][0] == '$' && user_args[num_args][1] == '$')
					{
						user_args[num_args] = strdup(pid);
					}
					num_args++;
				}

				//next arg for loop
				argument = strtok(NULL, " ");
			} //end argument array building

			/*************************CHECK ARGUMENTS**********************/
			//first check for exit
			if(strcmp(user_args[0], "exit") == 0)
			{
				exit_stat = 1; //escape loop to exit
			}
			else //process stuff
			{
				//check last arg for bg process
				if(strcmp(user_args[num_args-1], "&") == 0)
				{
					//check if in foreground-only mode
					if(fg_mode == 0)
					{
						bg_process = 1;
					}
					//remove from array
					user_args[num_args-1] = NULL;
				}

				//check for 'status'
				if(strcmp(user_args[0], "status") == 0)
				{
					//for exit status
					if(WIFEXITED(status_code) != 0)
					{
						printf("exit value %d\n", WEXITSTATUS(status_code));
						fflush(stdout);
					}
					//see if killed by signal
					else if (WIFSIGNALED(status_code) != 0)
					{
						printf("terminated by signal %d\n", WTERMSIG(status_code));
						fflush(stdout);
					}
				}
				//check for 'cd'
				else if (strcmp(user_args[0], "cd") == 0)
				{
					//check to go home or move up a directory
					if(user_args[1] != NULL)
					{
						//check for valid directory
						if(chdir(user_args[1]) != 0)
						{
							printf("Unable to change to %s directory\n", user_args[1]);
							fflush(stdout);
						}
					}
					else //go home
					{
						chdir(getenv("HOME"));
					}
				}
				/***************ALL OTHER COMMANDS***************/
				else
				{
					child_pid = fork();

					//for in/out redirect checks
					int in_FD, out_FD, dup_status;

					switch(child_pid)
					{
						//failed forking
						case -1:
							printf("unable to fork\n");
							fflush(stdout);

						//child case
						case 0:
							//ignore ctrl+z
							sigaction(SIGTSTP, &SIGTSTP_child, NULL);

							//if background
							if(bg_process == 1)
							{
								//redirect to dev/null for output by default
								//source 3
								out_FD = open("/dev/null", O_WRONLY);
								if(out_FD == -1)
								{
									//print to error out and escape
									fprintf(stderr, "cannot access /dev/null\n");
									fflush(stderr);
									exit(1);
								}
								dup_status = dup2(out_FD, STDIN_FILENO);
								if (dup_status == -1)
								{
									fprintf(stderr, "dup2() failed\n");
									fflush(stderr);
									exit(1);
								}
								// close open files
								fcntl(out_FD, F_SETFD, FD_CLOEXEC);

								//input redirect by user
								if (in_red == 1)
								{
									in_FD = open(input_file, O_RDONLY);
									if (in_FD == -1)
									{
										//print to error out and escape
										fprintf(stderr, "cannot open %s for input\n", input_file);
										fflush(stderr);
										exit(1);
									}
									dup_status = dup2(in_FD, STDIN_FILENO);
									if (dup_status == -1)
									{
										fprintf(stderr, "dup2() failed\n");
										fflush(stderr);
										exit(1);
									}
									// close open files
									fcntl(in_FD, F_SETFD, FD_CLOEXEC);
								}

								//output redirect by user
								if (out_red == 1)
								{
									out_FD = open(output_file,  O_WRONLY | O_CREAT | O_TRUNC, 0644);
									if(out_FD == -1)
									{
										//print to error out and escape
										fprintf(stderr, "cannot open %s for output\n", output_file);
										fflush(stderr);
										exit(1);
									}
									dup_status = dup2(out_FD, STDOUT_FILENO);
									if (dup_status == -1)
									{
										fprintf(stderr, "dup2() failed\n");
										fflush(stderr);
										exit(1);
									}
									// close open files
									fcntl(out_FD, F_SETFD, FD_CLOEXEC);
								} //end out_redirect
							} //end if_background

					/*******************FOREGROUND********************/
							else //foreground
							{
								//allow ctrl+C interrupt
								sigaction(SIGINT, &SIGINT_child, NULL);

								//input and output redirect same as for background
								if (in_red == 1)
								{
									in_FD = open(input_file, O_RDONLY);
									if (in_FD == -1)
									{
										//print to error out and escape
										fprintf(stderr, "cannot open %s for input\n", input_file);
										fflush(stderr);
										exit(1);
									}
									dup_status = dup2(in_FD, STDIN_FILENO);
									if (dup_status == -1)
									{
										fprintf(stderr, "dup2() failed\n");
										fflush(stderr);
										exit(1);
									}
									// close open files
									fcntl(in_FD, F_SETFD, FD_CLOEXEC);
								}

								//output redirect by user
								if (out_red == 1)
								{
									out_FD = open(output_file,  O_WRONLY | O_CREAT | O_TRUNC, 0644);
									if(out_FD == -1)
									{
										//print to error out and escape
										fprintf(stderr, "cannot open %s for output\n", output_file);
										fflush(stderr);
										exit(1);
									}
									dup_status = dup2(out_FD, STDOUT_FILENO);
									if (dup_status == -1)
									{
										fprintf(stderr, "dup2() failed\n");
										fflush(stderr);
										exit(1);
									}
									// close open files
									fcntl(out_FD, F_SETFD, FD_CLOEXEC);
								} //end out_redirect
							}
				/******************EXEC OTHER ARGS*********************/
							int exec_status;
							exec_status = execvp(user_args[0], user_args);
							if (exec_status == -1)
							{
								fprintf(stderr, "%s: no such file or directory\n", user_args[0]);
								fflush(stderr);
								exit(1);
							}
				/******************PARENT PROCESS*********************/
						default:
							//int child_exit_method;
							//background
							if (bg_process == 1)
							{
								//do not wait
								//waitpid(child_pid, &child_exit_method, WNOHANG);
								printf("background pid is %d\n", child_pid);
								fflush(stdout);
								//num_procs++;
							}
							else //forground
							{
								//wait for process finish, save status
								waitpid(child_pid, &status_code, 0);

								//if killed by signal, notify user
								if(WIFSIGNALED(status_code) != 0)
								{
									printf("terminated by signal %d\n", WTERMSIG(status_code));
									fflush(stdout);
								}
							}
					} //end switch check
				} //end all other commands

				// //check background PIDs
				// int child_exit_method;
				// //-1 to wait for any child process
				// while ((child_pid = waitpid(-1, &child_exit_method, WNOHANG)) > 0)
				// {
				// 	printf("background pid %d is done: ", child_pid);
				// 	fflush(stdout);
				// 	if (WIFEXITED(child_exit_method) != 0)
				// 	{
				// 		printf("exit value %d\n", WEXITSTATUS(child_exit_method));
				// 		fflush(stdout);
				// 	}
				// 	else if (WIFSIGNALED(child_exit_method))
				// 	{
				// 		printf("terminated by signal %d\n", WTERMSIG(child_exit_method));
				// 		fflush(stdout);
				// 	}
				// }

			}//end argument array iteration
		}//end array building
	}//end input parsing

	//check background PIDs
	int child_exit_method;
	//-1 to wait for any child process
	while ((child_pid = waitpid(-1, &child_exit_method, WNOHANG)) > 0)
	{
		printf("background pid %d is done: ", child_pid);
		fflush(stdout);
		if (WIFEXITED(child_exit_method) != 0)
		{
			printf("exit value %d\n", WEXITSTATUS(child_exit_method));
			fflush(stdout);
		}
		else if (WIFSIGNALED(child_exit_method))
		{
			printf("terminated by signal %d\n", WTERMSIG(child_exit_method));
			fflush(stdout);
		}
	}
}//end execution loop
  return(0);
};
