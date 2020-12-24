/* 
 * File:   commands.h
 * Author: aomx
 *
 * Created on 9. Mai 2015, 22:37
 */

#ifndef COMMANDS_H
#define	COMMANDS_H

    struct irc_command_t {
        char *name;
        irc_event_callback_t execute;
    };
    
    typedef struct irc_command_t irc_command_t;
    
    const irc_command_t* get_command(const char *commandString, size_t n);

#endif	/* COMMANDS_H */

