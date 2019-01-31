#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

using namespace std;

void parse_and_run_command(const std::string &command) {

    int pipecount = 0;                                          //Create a variable to track the number of pipes while iterating;
    istringstream s(command);                                   //Cpp object that is a buffer of strings
    string token;                                               //The individual strings within the stream that are command tokens
    vector<std::string> tokenlist;                              //A variable length vector for storing all command tokens
    vector<vector<string>> cmdlist;                             //A 2D vector for separating commands and their args into rows
    vector<pid_t> pidvec;                                       //A vector for tracking Process IDs of children
    cmdlist.push_back(std::vector<std::string>());              //Preparing the vector to accept pushes
    
    while (s >> token){
        tokenlist.push_back(token);                             //Pushing strings onto the tokenlist vector
    }

    for(uint i = 0; i < tokenlist.size(); i++){                 //Iterate over the tokenlist and check for pipe operators
        if(tokenlist[i] == "|"){                                //If its operators
            pipecount++;                                        //increment the pipe count
            int prev = i-1;                                     //previous token indexma
            if(prev > 0 && tokenlist[prev] == "|"){             //If the previous token was a pipe operator
                cerr << "Invalid command: Piping to pipe\n";    //its invalid because its piping a pipe
                return;                                         //exit parent process
            }
            cmdlist.push_back(std::vector<std::string>());      //if its not broken, add a row for the next command 
        }
        else{
            cmdlist[pipecount].push_back(tokenlist[i]);         //pushes all other tokens to the current row in cmdlist
        }
    }

    if(tokenlist.size() > 0){                                   //If the command list isn't empty
        if (tokenlist[0] == "exit") {                           //and the user typed exit
            exit(0);                                            //exit shell
        }
    }

    int pipearray[pipecount*2];                                 //declared a pipe array whose size is 2 X the number of pipe ops
    for(int i = 0; i < pipecount; i++){                         //iterate through this pipe array
        if (pipe(pipearray + 2*i) < 0){                         //call pipe() on index 0 and every even index afterward in order to
            cerr<< "Out of File Descriptors";                   //set up all pipes needed for the commands. If pipe() returns -1 then
            return;                                             //exit process becuase the pipes can't be made
        }
    }

    int cmdnum = 0;                                             //tracks how many commands have been processed

    for(uint j = 0; j < cmdlist.size();j++){                    //for every command in the command list

        vector<string> preargs;                                 //a vector for storing args of a single command
        int mode = S_IRUSR | S_IWUSR;                           //sets what mode to open a file in. Read and write permissions

        pid_t child_pid = fork();                               //forks to create child process and saves PID
        if(child_pid < 0){                                      //if fork() returns -1
            cerr << "Fork Failed\n";                            //the fork failed and errno has been set
            return;                                             //as such exit the process
        }

        if(child_pid == 0){                                     //if this is the child proc
            if(cmdnum != 0){                                    //if this is not the first command, we expect input from the previous command
                if(dup2(pipearray[(cmdnum-1)*2],0) < 0){        //we dup2 the cmd's pipe's read index to stdin to form the pipe
                    perror("dup2 error");                       //but if dup2 returns -1, it failed and errno is set
                    exit(7);                                    //exit the process
                }
            }
            if(cmdnum != pipecount){                            //if this not the last command, we need to send output to the next command
                if(dup2(pipearray[cmdnum*2+1],1) < 0){          //dup2 the write index of the pipe to stdout in order to form the next pipe
                    perror("dup2 error");                       //if it fails use perror() to show why
                    exit(7);                                    //exit 
                }
            }

            for(int z = 0; z < pipecount*2;z++){                //for all file descriptors in the pipearray of child
                close(pipearray[z]);                            //close the fds so they are not left hanging
            }

            for(uint i = 0; i < cmdlist[j].size();i++){                 //for every token in the command
                if(cmdlist[j][i] == ">"){                               //if its an output redirect
                    i++;                                                //increment because we don't want to add > to our list of args
                    if(i >= cmdlist[j].size()){                         //if i is now >= to the number of tokens in the command, its going to an empty direction 
                        cerr << "Invalid command: redirecting to nowhere\n";
                        exit(7);                                        //exit
                    }
                                                                        //if the next token is another redirector
                    if(cmdlist[j][i] == ">" || cmdlist[j][i] == "<"){
                        cerr << "Invalid command: redirecting to redirector\n";
                        exit(7);                                        //exit
                    }
                    int flags = O_WRONLY | O_TRUNC | O_CREAT;           //flags that set open()to write only, truncate the file, or create it if its not there
                    int fd = open(cmdlist[j][i].c_str(), flags, mode);  //saving fd of the open file and opening destination after >
                    if(fd < 0){                                         //if open fails and returns -1
                        perror("Invalid command");
                        exit(7);                                        //exit
                    }
                    if(dup2(fd,STDOUT_FILENO) < 0){                     //dup2 the newly opened file to stdout, but if it returns -1
                        perror("Invalid command");                      //print out why dup2 failed
                        exit(7);                                        //exit
                    }
                    close(fd);                                          //close opened file
                    continue;                                           //continue to next loop so the file dest isnt added to the args
                }
                else if(cmdlist[j][i] == "<"){                          //if the token is an input redirect
                    i++;                                                //increment to get to file name
                    if(i >= cmdlist[j].size()){                         //if i is now >= to the number of tokens in the command, its going to an empty direction
                        cerr << "Invalid command: redirecting to pipe\n";
                        exit(7);                                        //exit
                    }
                    if(cmdlist[j][i] == "<" || cmdlist[j][i] == ">"){   //if the next token is another redirector it is an invalid command
                        cerr << "Invalid command: redirecting to redirector\n";
                        exit(7);                                        //exit
                    }
                    int flags = O_RDONLY;                               //set open to read only
                    int fd = open(cmdlist[j][i].c_str(), flags, mode);  //save fd and open file to be read
                    if(fd < 0){                                         //if open fails
                        perror("Invalid command");
                        exit(7);                                        //exit
                    }
                    if(dup2(fd,STDIN_FILENO) < 0){                      //dup2 the fd to stdin to perform redirect
                        perror("Invalid command");                      //if it fails tell the user why and
                        exit(7);                                        //exit
                    }
                    close(fd);                                          //close opened file
                    continue;                                           //continue to next loop so the file name isnt added to the args
                }
                preargs.push_back(cmdlist[j][i]);                       //BUT if the token is not a rediector, add it to the preargs vector
            }

            const char** args = new const char*[preargs.size() + 1];    //create a char** the size of preargs + 1 to store the cmd and its args for execv
            for(uint k = 0; k < preargs.size();k++){                    //iterate through preargs
                args[k] = preargs[k].c_str();                           //create C style string to add to char** args
            }

            if(preargs.size() == 0){                                 //unless the cmd is empty
                cerr << "Invalid command: no args\n";
                exit(7);                                                //exit
            }

            args[preargs.size()] = NULL;                                //terminate the command with a NULL

            execv(args[0],(char **) args);                              //call execv with command at args[0] and give it the rest of the args
            std:: cerr << "Executable failed\n";                        //if this is printed then execv didn't finish as expected
            exit(1);                                                    //exit status 1 becuase the exe failed
        }
        else if(child_pid > 0){                                         //BUT if this process hasnt failed in a fork
            pidvec.push_back(child_pid);                                //push the child's PID to the vector in order to store it for later checking
        }
        cmdnum++;                                                       //increment the command number before restarting the loop
    }

    for(int z = 0; z < pipecount*2;z++){                                //close all the pipe fds in the parent. This should only be done once so it
        close(pipearray[z]);                                            //is not done within the larger for loop
    }

    for(uint j = 0; j < cmdlist.size();j++){                            //the parent now needs to go through PID list of child processes
        int status;                                                     //and figure out their statuses
        waitpid(pidvec[j],&status,0);                                   //and wait for them to finish

        if(WEXITSTATUS(status) == 1){                                   //waitpid will set this status to 1 if something has gone wrong
            cout << "Exit status: 1\n";
        }
        else if(WEXITSTATUS(status) == 0){                              //waitpid will set this status to 0 if the process exits normally
            cout << "Exit status: 0\n";
        }
    }
}

int main(void) {
    while (true) {
        std::string command;
        std::cout << "> ";
        std::getline(std::cin, command);
        parse_and_run_command(command);
    }
    return 0;
}
