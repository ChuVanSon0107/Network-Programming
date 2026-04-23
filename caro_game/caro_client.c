# include <stdio.h>
# include <arpa/inet.h>
# include <stdlib.h>
# include <unistd.h>
# include <string.h>

/* Message type */
# define MOVE 0x02
# define STATE_UPDATE 0x03
# define RESULT 0x04
# define TURN_NOTIFICATION 0x05

/* Constant */
# define PORT 8080
# define IP "127.0.0.1"
# define BUFFER_SIZE 10
# define BOARD_SIZE 3

/* Global variables */
struct sockaddr_in server_addr;
char buffer[BUFFER_SIZE];
int tcp_sock;
int game_board[BOARD_SIZE][BOARD_SIZE];
int player = -1;

/* Check whether it is a valid move */
int is_valid_move(int row, int col) {
    if (1 <= row && row <= BOARD_SIZE && 1 <= col && col <= BOARD_SIZE && game_board[row - 1][col - 1] == 0) {
        return 1;
    }

    return 0;
}

/* Update game state */
void update_state(int row, int col, int current_player) {
    game_board[row - 1][col - 1] = current_player;
}

/* Print game state */
void print_state() {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (game_board[i][j] == 1) {
                printf("X");
            } else if (game_board[i][j] == 2) {
                printf("O");
            } else {
                printf(" ");
            }
            
            if (j < BOARD_SIZE - 1) {
                printf(" | ");
            }
        }
        printf("\n");
    }
    printf("\n");
}

/* Print result */
void print_result(int winner) {
    if (winner == 0x03) {
        printf("Draw!\n");
    } else if (winner == player) {
        printf("You win!\n");
    } else {
        printf("You lose!\n");
    }
}

int main() {
    memset(&server_addr, 0, sizeof(server_addr));
    memset(buffer, 0, sizeof(buffer));
    memset(game_board, 0, sizeof(game_board));

    // Create socket
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, IP, &server_addr.sin_addr) == -1) {
        perror("inet_pton failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    // connect to server
    if (connect(tcp_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("Start playing game!\n");
    print_state();

    while(1) {
        // Receive message from server
        int n = recv(tcp_sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            continue;
        }

        if (buffer[0] == TURN_NOTIFICATION) {
            player = buffer[1];

            // input from user
            int row, col;
            printf("It's your turn.\nEnter your move: \n");
            while(is_valid_move(row, col) == 0) {
                scanf("%d %d", &row, &col);
            }

            // Send move to server
            buffer[0] = MOVE;
            buffer[1] = row;
            buffer[2] = col;

            send(tcp_sock, buffer, sizeof(buffer), 0);
        } else if (buffer[0] == STATE_UPDATE) {
            int row = buffer[1];
            int col = buffer[2];
            int current_player = buffer[3];

            // update state
            update_state(row, col, current_player); 

            // print game state
            print_state();
        } else if (buffer[0] == RESULT) {
            int winner = buffer[1];
            
            // print result
            print_result(winner);
            break;
        }
    }

    // close socket
    close(tcp_sock);

    return 0;
}
