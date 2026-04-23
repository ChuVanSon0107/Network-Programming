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
# define BUFFER_SIZE 10
# define BOARD_SIZE 3

/* Global variables */
struct sockaddr_in server_addr, client_addr;
char buffer[BUFFER_SIZE];
int tcp_sock, client_socks[2];
int game_board[BOARD_SIZE][BOARD_SIZE];
int current_player = 1;
int move_count = 0;
socklen_t addr_len = sizeof(client_addr);

/* Notify turn to player */
int notify_turn(int client_sock) {
    buffer[0] = TURN_NOTIFICATION;
    buffer[1] = current_player;
    return send(client_sock, buffer, sizeof(buffer), 0);
}

/* Check whether move is valid */
int is_valid_move(int row, int col) {
    if (1 <= row && row <= BOARD_SIZE && 1 <= col && col <= BOARD_SIZE && game_board[row - 1][col - 1] == 0) {
        return 1;
    }

    return 0;
}

/* Update state */
void update_state(int row, int col) {
    game_board[row - 1][col - 1] = current_player;
    move_count ++;
}

/* Check winner */
int check_winner() {
    for (int i = 0; i < BOARD_SIZE; i++) {
        if ((game_board[i][0] == current_player && game_board[i][1] == current_player && game_board[i][2] == current_player) 
            || (game_board[0][i] == current_player && game_board[1][i] == current_player && game_board[2][i] == current_player)){
            return current_player;
        }
    }

    if ((game_board[0][0] == current_player && game_board[1][1] == current_player && game_board[2][2] == current_player)
        || (game_board[0][2] == current_player && game_board[1][1] == current_player && game_board[2][0] == current_player)) {
            return current_player;
    }

    return 0;
}

/* Print result */
void print_result(int winner) {
    if (winner == 0) {
        printf("Draw!\n");
    } else {
        printf("Player %d win!\n", winner);
    }
}

int main() {
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));
    memset(buffer, 0, sizeof(buffer));
    memset(game_board, 0, sizeof(game_board));
    current_player = 1;

    // Create TCP socket
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up socket address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to a specific port
    if (bind(tcp_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    // listen to incoming connections
    if (listen(tcp_sock, 2) == -1) {
        perror("Listen failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on PORT %d...\n", PORT);

    // player 1 connect to server
    client_socks[0] = accept(tcp_sock, (struct sockaddr *) &client_addr, &addr_len);
    if (client_socks[0] == -1) {
        perror("Accept failed");
        close(tcp_sock);
        exit(EXIT_FAILURE);
    }

    printf("Player 1 is connected to server...\n");

    // player 2 connect to server
    client_socks[1] = accept(tcp_sock, (struct sockaddr *) &client_addr, &addr_len);
    if (client_socks[0] == -1) {
        perror("Accept failed");
        close(tcp_sock);
        close(client_socks[0]);
        exit(EXIT_FAILURE);
    }

    printf("Player 2 is connected to server...\n");

    // Loop for playing game
    while(1) {
        // notify to current player that it's your turn
        notify_turn(client_socks[current_player - 1]);

        // Receive move from current player
        int n = recv(client_socks[current_player - 1], buffer, sizeof(buffer), 0);
        if (n <= 0) {
            continue;
        }

        // check whether it's a valid move
        int row = buffer[1];
        int col = buffer[2];
        
        printf("Player %d: %d %d\n", current_player, row, col);
        
        if (is_valid_move(row, col) == 0) {
            continue;
        }
        
        // update state
        update_state(row, col);

        // send to player to update state
        buffer[0] = STATE_UPDATE;
        buffer[1] = row;
        buffer[2] = col;
        buffer[3] = current_player; // player who makes this move

        send(client_socks[current_player - 1], buffer, sizeof(buffer), 0);
        send(client_socks[2 - current_player], buffer, sizeof(buffer), 0);

        // check winner and send to player
        int winner = check_winner();
        if  (winner > 0 || move_count == 9) {
            buffer[0] = RESULT;
            buffer[1] = (winner > 0) ? winner : 0; // 0 is draw
            send(client_socks[0], buffer, sizeof(buffer), 0);
            send(client_socks[1], buffer, sizeof(buffer), 0);

            // print result
            print_result(winner);
            break;
        }

        // switch player
        current_player = 3 - current_player;
    }

    // close socket
    close(tcp_sock);
    close(client_socks[0]);
    close(client_socks[1]);

    return 0;
}