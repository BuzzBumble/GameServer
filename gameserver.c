#include "gameserver.h"

#define ALLOWED_GAME_COUNT 2
const int AllowedGameCount = ALLOWED_GAME_COUNT;
int AllowedGames[ALLOWED_GAME_COUNT] = { GAME_TYPE_TTT, GAME_TYPE_RPS };

int GameServerInit(GameServer *gs, int ver, int port) {
    puts("Initializing Game Server...");
    gs->num_games = GAMES_COUNT_INC;
    gs->games = calloc(sizeof(Game *), GAMES_COUNT_INC);
    gs->port = port;
    int x;
    gs->ptcl_version = ver;
    if ((x = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("initGameServer -> socket() error");
        return -1;
    }
    gs->tcpfd = x;
    int reuse_addr = 1;
    setsockopt(gs->tcpfd, SOL_SOCKET, SO_REUSEADDR,
        &reuse_addr, sizeof(reuse_addr));

	bzero(&(gs->servaddr), sizeof(gs->servaddr));
    gs->servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	gs->servaddr.sin_family = AF_INET;
	gs->servaddr.sin_port = htons(gs->port);
    
    if ((x = bind(gs->tcpfd, (struct sockaddr *)&(gs->servaddr), sizeof(gs->servaddr))) == -1) {
        perror("initGameServer -> bind() error");
        return -1;
    }

    listen(gs->tcpfd, 10);

    if ((x = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        perror("initGameServer -> socket() error");
        return -1;
    }
    gs->udpfd = x;

    bind(gs->udpfd, (struct sockaddr *)&(gs->servaddr), sizeof(gs->servaddr));

    FD_ZERO(&(gs->m_rset));

    if (gs->udpfd > gs->tcpfd) {
        gs->fdmaxp1 = gs->udpfd + 1;
    } else {
        gs->fdmaxp1 = gs->tcpfd + 1;
    }

    FD_SET(gs->tcpfd, &(gs->m_rset));
    FD_SET(gs->udpfd, &(gs->m_rset));

    puts("Game Server Initialized");

    return 0;
}

int GameServerLoop(GameServer *gs) {
    int i;
    fd_set rset;

    puts("Game Server Listening for connections");

    for (;;) {
        rset = gs->m_rset;
        if ((gs->nready = select(gs->fdmaxp1, &rset, NULL, NULL, NULL)) == -1) {
            perror("GameServerLoop -> select() error");
            return -1;
        }

        for (i = 0; i < gs->fdmaxp1; i++) {
            if (FD_ISSET(i, &rset)) {
                if (i == gs->tcpfd) {
                    GameServerAccept(gs);
                } else if (i == gs->udpfd) {
                    GameServerHandleUDP(gs);
                } else {
                    GameServerHandleTCP(gs, i);
                }
            }
        }
    }
}

int GameServerAccept(GameServer *gs) {
    struct sockaddr cliaddr;
    socklen_t len = sizeof(cliaddr);
    int connfd;

    if ((connfd = accept(gs->tcpfd, (struct sockaddr *)&cliaddr, &len)) == -1) {
        perror("GameServerAccept -> accept() error");
        return -1;
    }

    printf("New TCP Client at fd: %d\n", connfd);

    FD_SET(connfd, &gs->m_rset);
    if (connfd >= gs->fdmaxp1) {
        gs->fdmaxp1 = connfd + 1;
    }

    return connfd;
}

int GameServerHandleUDP(GameServer *gs) {
    struct sockaddr cliaddr;
    socklen_t len = sizeof(cliaddr);
    const char *hello_msg = "Hello UDP Client";
    char buffer[1024];
    bzero(buffer, sizeof(buffer));

    printf("\nUDP Message from Client:\n");
    int n = recvfrom(gs->udpfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, &len);
    puts(buffer);
    sendto(gs->udpfd, hello_msg, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));

    return n;
}

int GameServerHandleTCP(GameServer *gs, int cfd) {
    puts("\nHandling TCP Request");
    Request req = {};
    bzero(&req, sizeof(Request));
    int n;

    n = read(cfd, &req.cfd, REQ_UID_SIZE);
    req.cfd = htonl(req.cfd);
    if (n <= 0) {
        RemoveClient(gs, cfd);
        return 0;
    }
    n = read(cfd, &req.type, REQ_TYPE_SIZE);
    n = read(cfd, &req.context, REQ_CONTEXT_SIZE);
    n = read(cfd, &req.plen, REQ_LEN_SIZE);
    if (req.plen > 0) {
        req.payload = malloc(sizeof(uint8_t) * req.plen);
        n = read(cfd, req.payload, req.plen);
    }

    if (req.cfd == 0) {
        req.cfd = cfd;
    }
    GameServerHandleRequest(gs, &req);
    free(req.payload);

    return 0;
}

int GameServerHandleRequest(GameServer *gs, Request *req) {
    puts("\nRequest Received: ");
    printf("Client fd: %d\n", req->cfd);
    printf("Type: %d\n", req->type);
    printf("Context: %d\n", req->context);
    printf("Payload Length: %d\n", req->plen);
    printf("Payload: ");
    for (size_t i = 0; i < req->plen; i++) {
        printf("%u, ", req->payload[i]);
    }
    puts("\n");

    switch (req->type) {
        case REQ_TYPE_CONFIRM:
            HandleConfirm(gs, req);
            break;
        case REQ_TYPE_GAME:
            HandleGameAction(gs, req);
            break;
        default:
            break;
    }

    return 0;
}

int HandleConfirm(GameServer *gs, Request *req) {
    switch (req->context) {
        case REQ_CONFIRM_RULESET: ;
        HandleConfirmRuleset(gs, req);
            break;
        default:
            puts("Unrecognized Confirmation Context");
            break;
    }
    return 0;
}

int HandleGameAction(GameServer *gs, Request *req) {
    switch (req->context) {
        case REQ_GAME_MOVE: ;
            HandleMove(gs, req);
            break;
        default:
            puts("Unrecognized Confirmation Context");
            break;
    }
    return 0;
}

int HandleMove(GameServer *gs, Request *req) {
    uint32_t cfd = req->cfd;
    Game *game = FindGameWithClient(gs, cfd);
    if (game == 0) {
        puts("Could not find game");
        return -1;
    }

    switch (game->type) {
        case GAME_TYPE_TTT:
            if (TTT_HandleMove(game, req) == 1) {
                DestroyGame(gs, game);
            }
            break;
        case GAME_TYPE_RPS:
            if (RPS_HandleMove(game, req) == 1) {
                DestroyGame(gs, game);
            }
            break;
        default:
            break;
    }
}

int HandleConfirmRuleset(GameServer *gs, Request *req) {
    int version;
    int game_type;
    version = req->payload[0];
    game_type = req->payload[1];
    Client *client = malloc(sizeof(Client));

    printf("Protocol Version: %d\n", version);
    if (version != gs->ptcl_version) {
        puts("Protocol version does not match");
        return -1;
    }
    int supported = 0;
    for (size_t i = 0; i < AllowedGameCount; i++) {
        if (AllowedGames[i] == game_type)
            supported = 1;
    }
    if (!supported) {
        puts("Unsupported Game");
        return -1;
    } else {
        puts("Creating client");
        client->fd = req->cfd;
    }
    puts("");

    Response res = {};
    res.type = RES_TYPE_SUCCESS;
    res.context = RES_SUCCESS_RULESET;
    res.plen = RES_PAYLOAD_PID_SIZE;
    res.payload = malloc(sizeof(uint8_t) * res.plen);

    parseIntoPayload(&res, req->cfd);
    sendResponse(&res, req->cfd);

    puts("Ruleset confirmation sent");

    // Then put the client in a game
    FindAvailableGame(gs, game_type, client);

    // ALL JUST FOR PRINTING THINGS
    printf("New Client's game: %d\n", client->game_id);
    int numClients = gs->games[client->game_id]->num_clients;
    printf("Clients in this game: ");
    Game *game = gs->games[client->game_id];
    for (size_t i = 0; i < game->max_clients; i++) {
        if (game->clients[i] != 0)
            printf("%u, ", game->clients[i]->fd);
    }
    puts("");
    // END PRINTING THINGS
    return 0;
}

int MakeNewGame(GameServer *gs, int index, int game_type, Client* client) {
    // Make a new game for this client
    if (gs->games[index] == 0) {
        printf("Creating Game at index: %d\n", index);
        Game *game = malloc(sizeof(Game));
        if (game_type == GAME_TYPE_TTT) {
            GameInitTTT(game, index);
            puts("New TTT game created");
        } else if (game_type == GAME_TYPE_RPS) {
            GameInitRPS(game, index);
            puts("New RPS game created");
        }
        if (GameAddClient(game, client) == -1) {
            puts("Could not add the client to the game");
            free(game);
            return -1;
        } else {
            gs->games[index] = game;
            return index;
        }
    }
    return -1;
}

int FindAvailableGame(GameServer *gs, int game_type, Client *client) {
    int found = 0;
    int emptyGameIndex = -1;
    size_t i = 0;
    Game *game;

    puts("Finding an available game");
    while (!found && i < gs->num_games) {
        game = gs->games[i];
        if (emptyGameIndex == -1 && game == 0)
            emptyGameIndex = i;
        if (game != 0 && game->type == game_type && game->num_clients < game->max_clients) {
            puts("Game found. Adding client");
            found = 1;
            if (GameAddClient(game, client) == -1) {
                found = 0;
            } else {
                if (game->state == Starting) {
                    if (game->type == GAME_TYPE_RPS) {
                        int x = RPS_GameStart(game);
                    }
                }
                return i;
            }
        }
        i++;
    }

    puts("No game found");
    // If no empty game was found
    if (emptyGameIndex == -1) {
        puts("Could not locate empty game. Expanding Games array");
        emptyGameIndex = gs->num_games;
        int newSize = gs->num_games + GAMES_COUNT_INC;
        gs->games = realloc(gs->games, newSize * sizeof(Game *));
    }

    return MakeNewGame(gs, emptyGameIndex, game_type, client);
}

int RemoveClient(GameServer *gs, int cfd) {
    puts("Removing Client...");
    printf("Closing socket %d\n", cfd);
    close(cfd);
    FD_CLR(cfd, &gs->m_rset);
    int index;

    Game *game = FindGameWithClient(gs, cfd);
    if (game == 0) {
        puts("Could not find a game with this client");
        return -1;
    }

    if ((index = GameClientIndex(game, cfd)) != -1) {
        int rem = GameRemoveClientAtIndex(game, index);
        printf("Clients remaining in Game %d: ", game->num_clients);
        for (size_t j = 0; j < game->max_clients; j++) {
            if (game->clients[j] != 0)
                printf("%u, ", game->clients[j]->fd);
        }
        puts("");
        if (rem == 0) {
            puts("Destroying Game");
            gs->games[game->id] = 0;
            free(game);
        }
        return 0;
    }
    return -1;
}

int DestroyGame(GameServer *gs, Game* game) {
    printf("Destroying Game %d\n", game->id);
    for (size_t i = 0; i < game->max_clients; i++) {
        if (game->clients[i] != 0) {
            close(game->clients[i]->fd);
            FD_CLR(game->clients[i]->fd, &gs->m_rset);
            free(game->clients[i]);
        }
    }
    free(game->clients);
    gs->games[game->id] = 0;
    free(game);
    return 0;
}

Game *FindGameWithClient(GameServer *gs, uint32_t cfd) {
    int index;
    for (size_t i = 0; i < gs->num_games; i++) {
        if (gs->games[i] != 0) {
            if ((index = GameClientIndex(gs->games[i], cfd)) != -1) {
                return gs->games[i];
            }
        }
    }
    return 0;
}