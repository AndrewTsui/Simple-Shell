#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include "../include/shell.h"
#include "../include/helpers.h"

static char buffer[BUFFER_SIZE];
extern int errno;
int bg = 0, pFlag = 0, rFlag = 0, outflag = 0, inflag = 0, errflag = 0, successiveOp = 0, firstOp = 0;
char *outfile = NULL, *infile = NULL, *errfile = NULL, *appendfile = NULL, *outerrfile = NULL;

// Create a linked list struct
typedef struct node
{
	char* command;
	pid_t value;
	struct node* next;
} node_t;

typedef struct list
{
	node_t* head;
} List_t;

List_t* list;

// Add background process to linked list
void addNode( pid_t pid, char* comm )
{
	node_t** head = &(list->head);
	node_t* new_node = malloc(sizeof(node_t));
	new_node->value = pid;
	new_node->command = strdup(comm);
	new_node->next = *head;
	*head = new_node;
}
// Print background processes
void printList()
{
	node_t* head = list->head;
	node_t* curr = head;
	if(curr == NULL)
		return;
	printf("PID\tCommand\n");
	while( curr != NULL )
	{
		printf("%d\t%s\n", curr->value, curr->command);
		curr = curr->next;
	}
}
// Delete background process from linked list
void deleteNode( pid_t pid )
{
	node_t** head = &(list->head);
	node_t* current = *head;
	node_t* prev = NULL;
	if( current->value == pid )
	{
		*head = current->next;
		free(current->command);
		free(current);
		return;
	}
	while( current != NULL  && current->value != pid )
	{
		prev = current;
		current = current->next;
	}
	if( current == NULL )
		return;
	prev->next = current->next;
	free(current->command);
	free(current);
}
// Helper function to print errors
void error(const char* msg)
{
	printf("%s\n", msg);
	exit(EXIT_FAILURE);
}
// Helper function to reset all global variables after each iteration
void reset_vars()
{
	pFlag= 0;
	outflag = 0;
	inflag = 0;
	errflag = 0;
	successiveOp = 0;
	rFlag = 0;
	outfile = NULL;
	infile = NULL;
	errfile = NULL;
	appendfile = NULL;
	outerrfile = NULL;
}
// Fork wrapper function
pid_t Fork(void)
{
	pid_t pid;
	if((pid = fork()) < 0)
		error("Fork error.");
	return pid;
}
// SIGUSR2 handler
void sigusr2_handler(int sig)
{
	printf("Hi User!\n");
}
// SIGCHLD handler
void sigchld_handler(int sig)
{
	sigset_t mask, prev_mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &prev_mask);
	int wait_result, exit_status, old_errno = errno;
	while((wait_result = waitpid(-1, &exit_status, WNOHANG)) > 0)
		deleteNode(wait_result);
	errno = old_errno;
	sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}
// Looks through the arguments for the pipe operator while also looking for redirection operator 
void checkPipe( char* args[] )
{
	int i = 0;
	while(args[i])
	{
		if(strcmp(args[i],">") == 0 || strcmp(args[i],"<") == 0 || strcmp(args[i],">>") == 0 ||
			strcmp(args[i], "2>") == 0 || strcmp(args[i], "&>") == 0)
			rFlag = 1;
		else if(strcmp(args[i],"|") == 0)
			pFlag = 1;
		++i;
	}
}
// Traverses backwards to ensure that redirection operators come after arguments
int checkRedirection( char* args[], size_t numTokens )
{
	int i, argFlag = 0, wasOp = 0;
	for(i = numTokens-1; i >= 0; --i)
	{
		if(strcmp(args[i], "&") == 0)
			continue;
		else if(strcmp(args[i],">") == 0)
		{
			if(i == numTokens-1)
				firstOp = 1;
			if(wasOp)
				successiveOp = 1;
			wasOp = 1;
			rFlag = 2;
			++outflag;
			argFlag = 0;
			outfile = args[i+1];
		}
		else if(strcmp(args[i],"<") == 0)
		{
			if(i == numTokens-1)
				firstOp = 1;
			if(wasOp)
				successiveOp = 1;
			wasOp = 1;
			rFlag = 2;
			++inflag;
			argFlag = 0;
			infile = args[i+1];
		}
		else if(strcmp(args[i],">>") == 0)
		{
			if(i == numTokens-1)
				firstOp = 1;
			if(wasOp)
				successiveOp = 1;
			wasOp = 1;
			rFlag = 2;
			++outflag;
			argFlag = 0;
			appendfile = args[i+1];
		}
		else if(strcmp(args[i],"&>") == 0)
		{
			if(i == numTokens-1)
				firstOp = 1;
			if(wasOp)
				successiveOp = 1;
			wasOp = 1;
			rFlag = 2;
			++outflag;
			++errflag;
			argFlag = 0;
			outerrfile = args[i+1];
		}
		else if(strcmp(args[i],"2>") == 0)
		{
			if(i == numTokens-1)
				firstOp = 1;
			if(wasOp)
				successiveOp = 1;
			wasOp = 1;
			rFlag = 2;
			++errflag;
			argFlag = 0;
			errfile = args[i+1];
		}
		else
		{
			if(i == numTokens-1)
				firstOp = 0;
			if(argFlag == 1)
				return i+1;
			argFlag = 1;
			wasOp = 0;
		}
	}
	return 0;
}
// redirects stdout to file
void redirectOut( char* filename )
{
	int out = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	if( out < 0 )
		error("File error.");
	dup2(out, 1);
	close(out);
}
// redirects stdin to file
void redirectIn( char* filename )
{
	int in = open(filename, O_RDONLY);
	if( in < 0 )
		error("File error.");
	dup2(in, 0);
	close(in);
}
// redirects stderr to file
void redirectErr( char* filename )
{
	int out = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	if( out < 0 )
		error("File error.");
	dup2(out, 2);
	close(out);
}
// redirects stdout to file to append
void redirectAppend( char* filename )
{
	int out = open(filename, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	if( out < 0 )
		error("File error.");
	dup2(out, 1);
	close(out);
}
// redirects stdout and stderr to file
void redirectOutErr( char* filename )
{
	int out = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	if( out < 0)
		error("File error.");
	dup2(out, 1);
	dup2(out, 2);
	close(out);
}
// creates pipe
void createPipe( char* arg1[], char* arg2[] )
{
	int fd[2];
	pid_t pid;
	if( pipe(fd) != 0 )
		error("Pipe error.");

	if((pid = Fork()) == -1)
		error("Fork error.");
	
	if(pid == 0)
	{
		dup2(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
		execvp(arg1[0], arg1);
		error("Execute error while piping.");
	}
	else
	{
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execvp(arg2[0], arg2);
		error("Execute error while piping.");
	}
}

int main(int argc, char *argv[])
{
	int i; //loop counter
	char *pos, *dir, *last; //used for removing newline character
	char *args[MAX_TOKENS + 1], *temp[MAX_TOKENS + 1], *pipe[MAX_TOKENS + 1];
	int exec_result, exit_status;
	sigset_t mask, prev_mask;
	pid_t pid, wait_result;
	// create linked list
	list = malloc(sizeof(List_t));
	list->head = NULL;
	// block SIGTSTP
	sigemptyset(&mask);
	sigaddset(&mask, SIGTSTP);
	sigprocmask(SIG_BLOCK, &mask, &prev_mask);

	for(;;)
	{
		reset_vars(); //reset variables
		//get current time
		time_t curr_time;
		struct tm* t;
		curr_time = time(NULL);
		t = localtime(&curr_time);
		//get current directory
		dir = getcwd(buffer, 1024);
		last = strrchr(dir, '/');
		if(last != NULL)
			++last;
		//handle SIGUSR2 signal
		if( signal(SIGUSR2, sigusr2_handler) == SIG_ERR )
			error("Signal Error.");
		//prompt
		printf("\033[0;36m<tsuiat [%s] %02d/%02d/%04d %02d:%02d:%02d>$\033[0m ", last, t->tm_mon+1, t->tm_mday, 1900+t->tm_year, t->tm_hour, t->tm_min, t->tm_sec);

		fgets(buffer, BUFFER_SIZE, stdin);
		if( (pos = strchr(buffer, '\n')) != NULL)
		{
			*pos = '\0'; //Removing the newline character and replacing it with NULL
		}
		// Handling empty strings.
		if(strcmp(buffer, "")==0)
			continue;
		// Parsing input string into a sequence of tokens
		size_t numTokens;
		*args = NULL;
		numTokens = tokenizer(buffer, args, MAX_TOKENS);
		if(numTokens == 0)
			continue;
		args[numTokens] = NULL;
		if(strcmp(args[0],"exit") == 0) // Terminating the shell
			return 0;
		else if(strcmp(args[0], "cd") == 0) // Check if user wants to change directories
		{
			int ret;
			if((ret = chdir(args[1])) == -1)
				printf("Changing directories error.\n");
			continue;
		}
		else if(strcmp(args[0], "list") == 0) // Check if user wants to list out bg processes
		{
			printList();
			continue;
		}

		if(*args[numTokens-1] == '&') // Mark background process
			bg = 1;
		else
			bg = 0;

		checkPipe(args);

		int index = checkRedirection(args, numTokens);
		// throw an error if there is more than one out,in,or err redirect, multiple operators in a row
		// operators as last argument, or arguments after the first redirection operator
		if(outflag > 1 || inflag > 1 || errflag > 1 || rFlag == 1 || successiveOp || firstOp)
		{
			printf("Invalid combination of redirection operators.\n");
			continue;
		}

		args[index+1] = NULL;


		pid = Fork();
		if (pid == 0) //If zero, then it's the child process
		{
			if(outfile)
				redirectOut(outfile);
			if(errfile)
				redirectErr(errfile);
			if(outerrfile)
				redirectOutErr(outerrfile);
			if(appendfile)
				redirectAppend(appendfile);
			if(infile)
				redirectIn(infile);
			if(pFlag == 1)
			{
				int k = 0, l = 0, pipeFlag = 0;
				//create pipe argument
				while(args[k])
				{
					if(strcmp(args[k], "|") == 0)
					{
						++k;
						temp[k] = NULL;
						pipeFlag = 1;
						continue;
					}
					if(pipeFlag)
					{
						pipe[l] = args[k];
						++l;
					}
					else
						temp[k] = args[k];
					++k;
				}
				pipe[l] = NULL;
				createPipe(temp, pipe);
			}
			execvp(args[0], args);
			error("Execute error.");
		}
		else
		{
			addNode(pid, args[0]); // add node to linked list
			if(bg == 0) // foregrounnd
			{
				wait_result = waitpid(pid, &exit_status, 0);
				if(wait_result == -1)
					error("An error occurred while waiting for the process.\n");
				deleteNode(wait_result);
			}
			else // background
			{
				if(signal(SIGCHLD, sigchld_handler) == SIG_ERR)
					error("Signal error.");
			}
		}
		
	}
	sigprocmask(SIG_SETMASK, &prev_mask, NULL);
	return 0;
}


size_t tokenizer(char *buffer, char *argv[], size_t argv_size)
{
	char *p, *wordStart;
	int c;
	enum mode { DULL, IN_WORD, IN_STRING } currentMode = DULL;
	size_t argc = 0;
	for (p = buffer; argc < argv_size && *p != '\0'; p++) 
	{
		c = (unsigned char) *p;
		switch (currentMode) 
		{
			case DULL:
				if (isspace(c))
					continue;
			    	// Catching "
				if (c == '"') 
			    	{
					currentMode = IN_STRING;
					wordStart = p + 1;
					continue;
				}
				currentMode = IN_WORD;
				wordStart = p;
				continue;
				// Catching "
			case IN_STRING:
			    	if (c == '"') 
			    	{
					*p = 0;
					argv[argc++] = wordStart;
					currentMode = DULL;
			    	}
			    	continue;
			case IN_WORD:
			    	if (isspace(c)) 
			    	{
					*p = 0;
					argv[argc++] = wordStart;
					currentMode = DULL;
			    	}
			    	continue;
		}
	}
	if (currentMode != DULL && argc < argv_size)
		argv[argc++] = wordStart;
	return argc;
}
