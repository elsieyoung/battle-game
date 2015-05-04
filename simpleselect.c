/*
 * socket demonstrations:
 * This is the server side of an "internet domain" socket connection, for
 * communicating over the network.
 *
 * In this case we are willing to wait either for chatter from the client
 * _or_ for a new connection.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PORT
#define PORT 11029
#endif

typedef enum { false, true } bool;

struct client {
    int fd;
    struct in_addr ipaddr;
    struct client *next;
    struct client *opponent;
    struct client *last_opponent;
    char *name;
    char *buf;
    int inbuf;
    bool if_name;    // true if name is completely entered false otherwise
    bool in_match;   // true if the player is in match false otherwise
    bool if_active;  // true if the player is an active player false otherwise
    int hitpoints;
    int powermoves;
    char command;
};

int end_match(struct client **head, struct client *p);
int speak(struct client *p, int nbytes);
char get_command(struct client **head, struct client *p, int nbytes);
int find_valid_command(struct client *p);
int handle_command(struct client **head, struct client *p);
int print_inactive_player(struct client *p);
int print_active_player(struct client *p);
int print_status(struct client *p);
int start_match(struct client *head, struct client *player, struct client *opponent);
int find_opponent(struct client *head, struct client *p);
int handle_player(struct client **head, struct client *p, int nbytes);
int add_name(struct client *head, struct client *p, int nbytes);
int find_network_newline(char *buf, int inbuf);
static struct client *addclient(struct client *top, int fd, struct in_addr addr);
static struct client *removeclient(struct client **top, struct client *p);
static void broadcast(struct client *top, char *s, int size, struct client *source);
struct client *move_to_end(struct client **head, struct client *p);
int handleclient(struct client *p, struct client *top);
int bindandlisten(void);


struct client *head = NULL;

int main(void) {
    int clientfd, maxfd, nready;
    struct client *p;
    head = NULL;
    socklen_t len;
    struct sockaddr_in q;
    fd_set allset;
    fd_set rset;
    
    int i;
    
    
    int listenfd = bindandlisten();
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;
    
    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;

        
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        
        if (nready == -1) {
            perror("select");
            continue;
        }
        
        if (FD_ISSET(listenfd, &rset)){
            printf("a new client is connecting\n");
            len = sizeof(q);
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }
            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            head = addclient(head, clientfd, q.sin_addr); // name not added yet
        }
        
        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                for (p = head; p != NULL; p = p->next) {
                    if (p->fd == i) {
                        int nbytes;
                        int room = 200 - p->inbuf;
                        char *after = &p->buf[p->inbuf]; // pointer to current position in p.buf
                        if ((nbytes = read(p->fd, after, room)) <= 0) {
                            int tmp_fd = p->fd;
                            head = removeclient(&head, p);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                            break;
                        }
                        
                        int result = handle_player(&head, p, nbytes);
                        //int result = handleclient(p, head);
                        if (result == -1) { // player drops
                            int tmp_fd = p->fd;
                            head = removeclient(&head, p);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                            break;
                        }
                        else if (result == -2) { // opponent drops
                            int tmp_fd = p->opponent->fd;
                            head = removeclient(&head, p->opponent);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                            break;
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

/* Call an appropriate fucnction depending on the player's input */
int handle_player(struct client **head, struct client *p, int nbytes) {
    //fprintf(stderr, "name: %s, buf: %s, command: %c, inbuf: %d\n", p->name, p->buf, p->command, p->inbuf);
    
    // add a name of the player
    if (p->if_name == false) {
        return add_name(*head, p, nbytes);
    }
    
    // clears buffer when inactive player inputs something
    else if (p->if_active == false) {
        p->inbuf = 0;
    }
    
    // get a command from an active player and process it
    else if (p->if_active == true && p->command == '\0') {
        return get_command(head, p, nbytes);
    }
    
    // handle speak
    else if (p->if_active == true && p->command == 's') {
        return speak(p, nbytes);
    }
    
    return 0;
}

/* respond to active player's action */
int handle_command(struct client **head, struct client *p) {
    char buf[200];
    
    srand(time(NULL)); // initialize rand
    int rand_attack = (rand()%(6+1-2))+2; // randomly pick attack
    
    // handle (a)
    if (p->command == 'a') {
        p->opponent->hitpoints = p->opponent->hitpoints - rand_attack;
        
        // print to the player
        sprintf(buf, "\nYou hit %s for %d damage!\n", p->opponent->name, rand_attack);
        if (write(p->fd, buf, strlen(buf)) == -1) {
            return -1;
        }
        // print to the opponent
        sprintf(buf, "%s hits you for %d damage!\n", p->name, rand_attack);
        if (write(p->opponent->fd, buf, strlen(buf)) == -1) {
            return -2;
        }
    }
    
    // handle (p)
    else if (p->command == 'p') {
        int rand_powermove = rand()%2; // randomly decide if the powermove hits
        p->powermoves--;
        
        if (rand_powermove == 1) {
            rand_attack = rand_attack * 3;
            p->opponent->hitpoints = p->opponent->hitpoints - rand_attack;
            
            // print to the player
            sprintf(buf, "\nYou hit %s for %d damage!\n", p->opponent->name, rand_attack);
            if (write(p->fd, buf, strlen(buf)) == -1) {
                return -1;
            }
            // print to the opponent
            sprintf(buf, "%s powermoves you for %d damage!\n", p->name, rand_attack);
            if (write(p->opponent->fd, buf, strlen(buf)) == -1) {
                return -2;
            }
        } else {
            // print to the player
            sprintf(buf, "\nYou missed!\n");
            if (write(p->fd, buf, strlen(buf)) == -1) {
                return -1;
            }
            // print to the opponent
            sprintf(buf, "%s missed you!\n", p->name);
            if (write(p->opponent->fd, buf, strlen(buf)) == -1) {
                return -2;
            }
        }
    }
    
    // when p beats the opponent
    if (p->opponent->hitpoints <= 0) {
        return end_match(head, p);
    }
    
    // print status on player's side
    if (print_status(p) == -1 || print_inactive_player(p) == -1) {
        return -1;
    }
    // print status on opponent's side
    if (print_status(p->opponent) == -1 || print_active_player(p->opponent) == -1) {
        return -2;
    }
    
    // flip the side
    p->command = '\0'; // initialize command for the next action
    p->if_active = false;
    p->opponent->if_active = true;
    
    return 0;
}

/* read the player's input till find a valid command and return it
 * return -1 if fails
 */
char get_command(struct client **head, struct client *p, int nbytes) {
    int where; // location of a valid command
    p->inbuf = p->inbuf + nbytes; // update inbuf

    where = find_valid_command(p);
    
    if (where >= 0) { // have complete name
        p->command = p->buf[where];
        p->inbuf = 0;
        if (p->command != 's') {
            return handle_command(head, p);
        }
        else if (p->command == 's') {
            char outbuf[200];
            // print to p
            sprintf(outbuf, "\nSpeak: \n");
            if (write(p->fd, outbuf, strlen(outbuf)) == -1) {
                return -1;
            }
            return 0;
        }
    }
    return 0;
}

/* find the position of 'a', 's' or 'p' in the player's input*/
int find_valid_command(struct client *p) {
    int i = 0;
    while (i < p->inbuf) {
        if (p->buf[i] == 'a' || p->buf[i] == 's' || (p->buf[i] == 'p' && p->powermoves != 0)) {
            return i;
        }
        i++;
    }
    return -1;
}

/* notifies the players of the end of this match and rearrange the list */
int end_match(struct client **head, struct client *p) {
    char buf[200];
    // notifies to p
    sprintf(buf, "%s gives up. You win!\n\n", p->opponent->name);
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    // notifies to opponent
    sprintf(buf, "You are no match for %s. You scurry away...\n\n", p->name);
    if (write(p->opponent->fd, buf, strlen(buf)) == -1) {
        return -2;
    }
    
    // update their status
    p->in_match = false;
    p->opponent->in_match = false;
    
    // put them at the end of the list
    *head = move_to_end(head, p);
    *head = move_to_end(head, p->opponent);
    
    // find new opponents
    sprintf(buf, "Awaiting next opponent...\n");
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    if (write(p->opponent->fd, buf, strlen(buf)) == -1) {
        return -2;
    }
	find_opponent(*head, p->opponent);
    find_opponent(*head, p);
    
    return 0;
}

/* handle when the player selects (s)peak */
int speak(struct client *p, int nbytes) {
    char outbuf[200];
    int where; // location of network newline
    
    p->inbuf = p->inbuf + nbytes; // update inbuf

    where = find_network_newline(p->buf, p->inbuf);
    if (where >= 0) { // have complete message
        p->buf[where] = '\0';
        
        // print to p
        sprintf(outbuf, "You speak: %s\n", p->buf);
        if (write(p->fd, outbuf, strlen(outbuf)) == -1) {
            return -1;
        }
        
        // print to p's opponent
        sprintf(outbuf, "%s takes a break to tell you:\n%s\n\n", p->name, p->buf);
        if (write(p->opponent->fd, outbuf, strlen(outbuf)) == -1) {
            return -2;
        }
        
        // print status on player's side
        if (print_status(p) == -1 || print_active_player(p) == -1) {
            return -1;
        }
        // print status on opponent's side
        if (print_status(p->opponent) == -1 || print_inactive_player(p->opponent) == -1) {
            return -2;
        }
        
        p->inbuf = 0;
        p->command = '\0'; // initialize command for the next action
    }

    return 0;
}

/* Find an opponent for p */
int find_opponent(struct client *head, struct client *p) {
    struct client *current = head;
    
    
    while (current!= NULL) {
        fprintf(stderr, "%s is seaching %s\n", p->name, current->name);
        if (current->if_name == true && current != p && current->in_match == false
            && current->last_opponent != p) { // restriction for a new oppoent
            fprintf(stderr, "found %s\n", current->name);

            char outbuf[200];
            
            // update status of p
            p->opponent = current;
            p->last_opponent = current;
            p->if_active = true;
            p->in_match = true;
            sprintf(outbuf, "You engage %s!\n", current->name);
            if (write(p->fd, outbuf, strlen(outbuf)) == -1) {
                return -1;
            }
            
            // update status of opponent
            current->opponent = p;
            current->last_opponent = p;
            current->if_active = false;
            current->in_match = true;
            sprintf(outbuf, "You engage %s!\n", p->name);
            if (write(current->fd, outbuf, strlen(outbuf)) == -1) {
                return -2;
            }
            
            return start_match(head, p, current);
        }
        current = current->next;
    }
    return 0;
}

/* set up a new match */
int start_match(struct client *head, struct client *player, struct client *opponent) {
    srand(time(NULL)); // initialize rand
    
    // assign hitpoints/powermoves/command
    player->hitpoints = 20 + rand()%11;
    player->powermoves = 1 + rand()%3;
    player->command = '\0';
    opponent->hitpoints = 20 + rand()%11;
    opponent->powermoves = 1 + rand()%3;
    opponent->command = '\0';
    
    // print on player's side
    if (print_status(player) == -1 || print_active_player(player) == -1) {
        return -1;
    }
    
    // print on opponent's side
    if (print_status(opponent) == -1 || print_inactive_player(opponent) == -1) {
        return -2;
    }
    
    return 0;
}

/* print status on active player's side */
int print_active_player(struct client *p) {
    char buf[200];
    sprintf(buf, "(a)ttack\n");
    if (write(p->fd, buf, strlen(buf)) == -1) { // (a)
        return -1;
    }
    if (p->powermoves != 0) { // option if there is powermove left
        sprintf(buf, "(p)owermoves\n");
        if (write(p->fd, buf, strlen(buf)) == -1) { // (p)
            return -1;
        }
    }
    sprintf(buf, "(s)peak something\n");
    if (write(p->fd, buf, strlen(buf)) == -1) { // (s)
        return -1;
    }
    return 0;
}

/* print status on inactive player's side */
int print_inactive_player(struct client *p) {
    char buf[200];
    sprintf(buf, "Waiting for %s to strike...\n\n", p->opponent->name);
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    return 0;
}

/* print the status of the match*/
int print_status(struct client *p) {
    char buf[200];
    sprintf(buf, "Your hitpoints: %d\n", p->hitpoints);
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    sprintf(buf, "Your powermoves: %d\n\n", p->powermoves);
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    sprintf(buf, "%s's hitpoints: %d\n\n", p->opponent->name, p->opponent->hitpoints);
    if (write(p->fd, buf, strlen(buf)) == -1) {
        return -1;
    }
    return 0;
}


//int handleclient(struct client *p, struct client *top) {
//    char buf[256];
//    char outbuf[512];
//    int len = read(p->fd, buf, sizeof(buf) - 1);
//    if (len > 0) {
//        buf[len] = '\0';
//        printf("Received %d bytes: %s", len, buf);
//        sprintf(outbuf, "%s says: %s", inet_ntoa(p->ipaddr), buf);
//        broadcast(top, outbuf, strlen(outbuf));
//        return 0;
//    } else if (len == 0) {
//        // socket is closed
//        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
//        sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
//        broadcast(top, outbuf, strlen(outbuf));
//        return -1;
//    } else { // shouldn't happen
//        perror("read");
//        return -1;
//    }
//}

/* bind and listen, abort on error
 * returns FD of listening socket
 */
int bindandlisten(void) {
    struct sockaddr_in r;
    int listenfd;
    
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(PORT);
    
    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }
    
    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}


/* Add a new player to the end of the list. Name not added */
static struct client *addclient(struct client *top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }
    
    printf("Adding client %s\n", inet_ntoa(addr));
    
    // create a new client
    p->fd = fd;
    p->ipaddr = addr;
    p->next = NULL;
    p->name = malloc(sizeof(char)*200);
    p->buf = malloc(sizeof(char)*200); // FREEEEEEEEE
    p->if_name = false;
    p->if_active = false;
    p->in_match = false;
    p->opponent = NULL;
    p->last_opponent = NULL;
    p->inbuf = 0;
    
    // ask new player's name
    write(p->fd, "What is your name? ", sizeof(char)*20);

    // add the new client to the end of the list
    struct client *current = top;
    if (current == NULL) {
        top = p;
    }
    else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = p;
    }
    return top;
}

/* Sets the player's name. If the player hasn't finished inputting
 * his name, just updates the name buffer
 */
int add_name(struct client *head, struct client *p, int nbytes) {
    int where; // location of network newline
    
    p->inbuf = p->inbuf + nbytes; // update inbuf
    
    where = find_network_newline(p->buf, p->inbuf);
    if (where >= 0) { // have complete name
        p->buf[where] = '\0';
        strncpy(p->name, p->buf, strlen(p->buf)); // copy the complete name in buf to p.name
        p->if_name = true;
        p->inbuf = 0;
        
        char outbuf[200];
        sprintf(outbuf, "**%s enters the arena**\n", p->name);
        broadcast(head, outbuf, strlen(outbuf), p);
        sprintf(outbuf, "Welcome, %s! Awaiting opponent...\n", p->name);
        write(p->fd, outbuf, strlen(outbuf));
        if (write(p->fd, outbuf, strlen(outbuf) == -1)) {
            return -1;
        }
        return find_opponent(head, p);
    }
    return 0;
}

/* helper function for add_name. Finds a network newline character*/
int find_network_newline(char *buf, int inbuf) {
    int i = 0;
    while (i <= inbuf) {
        if (buf[i] == '\n') {
            return i;
        }
        i++;
    }
    return -1;
}

static struct client *removeclient(struct client **top, struct client *p) {
    char outbuf[200];
    
    if(p->opponent != NULL){
        // handle p's opponent
        if (p->in_match == true) {
            sprintf(outbuf, "--%s dropped. You win!\n\n", p->name);
            write(p->opponent->fd, outbuf, strlen(outbuf));
            // update the opponent's status
            p->opponent->in_match = false;
            *top = move_to_end(top, p->opponent);
        }
        // broadcaset to remaining players that p leaves
        if (p->if_name == true) {
            sprintf(outbuf, "**%s leaves**\n", p->name);
            broadcast(*top, outbuf, strlen(outbuf), p);
        }
        
        sprintf(outbuf, "Awaiting next opponent...\n");
        if (write(p->opponent->fd, outbuf, strlen(outbuf)) == -1) {
            //return -1;
            return *top;
        }
        
        struct client *temp = p->opponent;
        temp->in_match = false;
        
        // move the opponent to the end of the list.
        move_to_end(top, temp);
        
        
        
        find_opponent(*top, temp);
        // remove p from the list
        struct client *current = *top;
        if (*top == p) {
            *top = p->next;
            return *top;
        }
        while (current->next != p) {
            current = current->next;

        }
        current->next = p->next;
        //fprintf(stderr, "REMOVE FTN\n");
        current = *top;
        while (current->next != NULL) {
            current = current->next;
            fprintf(stderr, "linkedlist: %s\n", current->name);
        }
        

        return *top;
    }
else{
    // broadcaset to remaining players that p leaves
    if (p->if_name == true) {
        sprintf(outbuf, "**%s leaves**\n", p->name);
        broadcast(*top, outbuf, strlen(outbuf), p);
    }
    
    // remove p from the list
    struct client *current = *top;
    if (*top == p) {
        *top = p->next;
        return *top;
    }
    while (current->next != p) {
        current = current->next;
    }
    current->next = p->next;
    
        return *top;
}
}

/* move the client to the end of the list */
struct client *move_to_end(struct client **head, struct client *p) {
    struct client *current = *head;
    
    // when p is already at the end of the list
    if (p->next == NULL) {
        return *head;
    }
    
    // remove p from the list
    if (*head == p) {
        *head = (*head)->next;
    } else {
        while (current->next != p) {
            current = current->next;
        }
        current->next = p->next;
    }
    
    // place p to the end of the list
    current = *head;
    while (current->next != NULL) {
        current = current->next;
    }
    p->next = NULL;
    current->next = p;
    
    return *head;
}

/* broadcast to every player except for source */
static void broadcast(struct client *top, char *s, int size, struct client *source) {
    struct client *p;
    for (p = top; p; p = p->next) {
        if (p != source) {
            write(p->fd, s, size);
        }
    }
    /* should probably check write() return value and perhaps remove client */
}
