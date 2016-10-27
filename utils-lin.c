/**
 * Operating Sytems 2013 - Assignment 2
 * by
 * Chivu Constantin Razvan , 332 CA
 */

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

#include "utils.h"

#define READ		0
#define WRITE		1

#define CHILD	0
#define IN 0
#define APPEND	1
#define OVRWR	69


static char *get_word( word_t *s );
void redirect( int filedes, char* fileno, int type );
int parse_command( command_t *c, int level, command_t *father );


/**
* Internal change-directory command.
*/
static bool shell_cd( word_t *dir )
{

    int ret = chdir( get_word( dir ) );

    if (ret < 0){
        perror( "shell cd failed\n" );
    }

    return ret;

}


/**
 * Internal exit/quit command.
 */
static int shell_exit()
{

    return SHELL_EXIT;

}


/**
 * Redirect filedes to fileno
 */
void redirect( int filedes, char* fileno, int type ){

    int fd, flags;

    if (filedes == STDIN_FILENO){
        fd = open( fileno, O_RDONLY );
    }

    else{
        flags = (type == APPEND) ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY | O_CREAT | O_TRUNC;
        fd = open( fileno, flags, 0644 );
    }


    if (fd < 0) perror( "open failed\n" );

    if (dup2( fd, filedes ) < 0) perror( "dup2 failed\n" );

    close( fd );

}


/**
 * Concatenate parts of the word to obtain the command
 */
static char *get_word( word_t *s )
{
    int string_length = 0;
    int substring_length = 0;

    char *string = NULL;
    char *substring = NULL;

    while (s != NULL) {
        substring = strdup( s->string );

        if (substring == NULL) {
            return NULL;
        }

        if (s->expand == true) {
            char *aux = substring;
            substring = getenv( substring );

            /* prevents strlen from failing */
            if (substring == NULL) {
                substring = calloc( 1, sizeof( char ) );
                if (substring == NULL) {
                    free( aux );
                    return NULL;
                }
            }

            free( aux );
        }

        substring_length = strlen( substring );

        string = realloc( string, string_length + substring_length + 1 );
        if (string == NULL) {
            if (substring != NULL)
                free( substring );
            return NULL;
        }

        memset( string + string_length, 0, substring_length + 1 );

        strcat( string, substring );
        string_length += substring_length;

        if (s->expand == false) {
            free( substring );
        }

        s = s->next_part;
    }

    return string;
}


/**
 * Concatenate command arguments in a NULL terminated list in order to pass
 * them directly to execv.
 */
static char **get_argv( simple_command_t *command, int *size )
{
    char **argv;
    word_t *param;

    int argc = 0;
    argv = calloc( argc + 1, sizeof( char * ) );
    assert( argv != NULL );

    argv[argc] = get_word( command->verb );
    assert( argv[argc] != NULL );

    argc++;

    param = command->params;
    while (param != NULL) {
        argv = realloc( argv, (argc + 1) * sizeof( char * ) );
        assert( argv != NULL );

        argv[argc] = get_word( param );
        assert( argv[argc] != NULL );

        param = param->next_word;
        argc++;
    }

    argv = realloc( argv, (argc + 1) * sizeof( char * ) );
    assert( argv != NULL );

    argv[argc] = NULL;
    *size = argc;

    return argv;
}


/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple( simple_command_t *s, int level, command_t *father )
{

    pid_t child_pid, wait_;
    int status, ret;

    char* cmd = get_word( s->verb );


    /* internal cmds */
    if (!strncmp( cmd, "quit", 4 ) || !strncmp( cmd, "exit", 4 )){
        ret = shell_exit();
        return ret;
    }

    if (!strncmp( cmd, "cd", 2 )){
        ret = shell_cd( s->params );
    }


    /* set variables */
    /* @format : var_name = $var_value */
    if (s->verb->next_part != NULL){

        char * var_name = s->verb->string;
        char * var_value = (s->verb->next_part->next_part != NULL) ? s->verb->next_part->next_part->string : NULL;

        if (var_value != NULL){

            if (!strcmp( s->verb->next_part->string, "=" )){
                return setenv( var_name, var_value, 1 );
            }

        }

        free( var_name );
        free( var_value );

    }

    /* external commands */

    child_pid = fork();

    switch (child_pid) {

    case CHILD:{

        /* child */

        int argv_size;
        char** argv = get_argv( s, &argv_size );


        if (s->in != NULL){
            /* redirect stdin to getWord(s->in) */
            redirect( STDIN_FILENO, get_word( s->in ), IN );
        }


        /* redirect stdout or stderr */
        if (s->out != NULL && s->err != NULL){

            int out_type, err_type;

            if (s->io_flags & IO_ERR_APPEND){
                err_type = APPEND;
            }
            else{
                err_type = OVRWR;
            }

            /* both redirects */
            redirect( STDERR_FILENO, get_word( s->err ), err_type );
            redirect( STDOUT_FILENO, get_word( s->out ), APPEND );

        }

        else{

            /* redirect stdout */
            if (s->out != NULL){

                if (s->io_flags & IO_OUT_APPEND){
                    redirect( STDOUT_FILENO, get_word( s->out ), APPEND );
                }
                else{
                    redirect( STDOUT_FILENO, get_word( s->out ), OVRWR );
                }

            }

            /* redirect stderr */
            if (s->err != NULL){

                if (s->io_flags & IO_ERR_APPEND){
                    redirect( STDERR_FILENO, get_word( s->err ), APPEND );
                }
                else{
                    redirect( STDERR_FILENO, get_word( s->err ), OVRWR );
                }

            }
        }


        int exret;

        if (exret = execvp( cmd, argv ) < 0){

            fprintf( stderr, "Execution failed for '%s' \n", cmd );

        }

        exit( exret );

        break;
    }//end case CHILD

    default:{

        /* parrent */
        wait_ = waitpid( child_pid, &status, 0 ); /* don't let zombies invade us */

        if (wait_ < 0){
            perror( "" );
        }

        if (WIFEXITED( status )){
            return WEXITSTATUS( status );
        }


        break;
    }/* end parrent */

    }/* end switch */

    return 0;
}


/**
 * Process two commands in parallel, by creating two children.
 */
static bool do_in_parallel( command_t *cmd1, command_t *cmd2, int level, command_t *father )
{

    pid_t child1_pid, child2_pid, wait_;

    int status;

    child1_pid = fork();

    switch (child1_pid) {


    case CHILD:{
        /* execute cmd1 -> child1 */
        exit( parse_command( cmd1, level + 1, father ) );
        break;
    }

    default:{

        child2_pid = fork();

        switch (child2_pid) {

        case CHILD: {
            /* execute cmd2 -> child2 */
            exit( parse_command( cmd2, level + 1, father ) );
            break;
        }

        default:{
            wait_ = waitpid( child2_pid, &status, 0 );
            if (WIFEXITED( status )){
                return WEXITSTATUS( status );
            }
            break;
        }

        }/* end switch (child2) */

        wait_ = waitpid( child1_pid, &status, 0 ); /* zombies don't stand a chance */

        if (WIFEXITED( status )){
            return WEXITSTATUS( status );
        }

        break;
    }

    }/* end switch(child2) */

    return status;

}


/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2)
 */
static bool do_on_pipe( command_t *cmd1, command_t *cmd2, int level, command_t *father )
{
    /* redirect the stdout of cmd1 to the stdout of cmd2 */
    int pipefd[2], status;

    pid_t child_pid, wait_;

    if (pipe( pipefd ) == -1) {
        perror( "pipe" );
        exit( EXIT_FAILURE );
    }

    child_pid = fork();

    switch (child_pid) {

    case CHILD:{
        close( pipefd[0] );
        dup2( pipefd[1], STDOUT_FILENO ); /* write end of the pipe = stdout_cmd1 */
        close( pipefd[1] );
        exit( parse_command( cmd1, level + 1, father ) );

        break;
    }

    default:{

        close( pipefd[1] );
        dup2( pipefd[0], STDIN_FILENO ); /* read end of the pipe = stdin_cmd2 */
        close( pipefd[0] );
        parse_command( cmd2, level + 1, father );

        wait_ = waitpid( child_pid, &status, 0 );

        if (WIFEXITED( status )){
            return WEXITSTATUS( status );
        }

    }

    }

    return true;
}


/**
 * Parse and execute a command.
 */
int parse_command( command_t *c, int level, command_t *father )
{

    if (c->op == OP_NONE) {
        return parse_simple( c->scmd, level, c );
    }

    switch (c->op) {
    case OP_SEQUENTIAL:

        parse_command( c->cmd1, level + 1, c );
        return parse_command( c->cmd2, level + 1, c );
        break;

    case OP_PARALLEL:

        return do_in_parallel( c->cmd1, c->cmd2, level + 1, c );
        break;

    case OP_CONDITIONAL_NZERO:

        if (parse_command( c->cmd1, level + 1, c ) != 0){
            return parse_command( c->cmd2, level + 1, c );
        }
        break;

    case OP_CONDITIONAL_ZERO:

        if (parse_command( c->cmd1, level + 1, c ) == 0){
            return parse_command( c->cmd2, level + 1, c );
        }
        break;

    case OP_PIPE:

        return do_on_pipe( c->cmd1, c->cmd2, level + 1, c );
        break;


    default:
        assert( false );
    }


    return 0;
}


/**
 * Readline from mini-shell.
 */
char *read_line()
{
    char *instr;
    char *chunk;
    char *ret;

    int instr_length;
    int chunk_length;

    int endline = 0;

    instr = NULL;
    instr_length = 0;

    chunk = calloc( CHUNK_SIZE, sizeof( char ) );
    if (chunk == NULL) {
        fprintf( stderr, ERR_ALLOCATION );
        return instr;
    }

    while (!endline) {
        ret = fgets( chunk, CHUNK_SIZE, stdin );
        if (ret == NULL) {
            break;
        }

        chunk_length = strlen( chunk );
        if (chunk[chunk_length - 1] == '\n') {
            chunk[chunk_length - 1] = 0;
            endline = 1;
        }

        ret = instr;
        instr = realloc( instr, instr_length + CHUNK_SIZE + 1 );
        if (instr == NULL) {
            free( ret );
            return instr;
        }
        memset( instr + instr_length, 0, CHUNK_SIZE );
        strcat( instr, chunk );
        instr_length += chunk_length;
    }

    free( chunk );

    return instr;
}