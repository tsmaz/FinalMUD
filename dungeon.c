#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <mosquitto.h>
#include <ctype.h>


static struct mosquitto *mosquitto_client = NULL;

#define GRID_SIZE 4
#define NUM_ROOMS 10
#define NUM_DUNGEONS 4 // Number of dungeons
#define TCP_PORT 4000
#define BACKLOG 1 // Number of connections allowed (1 currently)
#define BUFFER_SIZE 256
#define COMMAND_BUFFER_SIZE 128
#define SENDMSG(x) mosquitto_publish(mosquitto_client, NULL, "game/message/serverToClient",(int)strlen(x), x, 0, false)

static void cleanup_input(char *input) {
    char *stripped = input + strlen(input);

    while (stripped > input && (*(stripped - 1) =='\n' || *(stripped - 1)=='\r')) {
        *--stripped = '\0';
    }

    for (char *upper = input; *upper; ++upper) {
        *upper = toupper((unsigned char)*upper);
    }
}

int setup_listener(int port) {
    int listen_descriptor = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_descriptor < 0) {
        perror("socket");
        exit(1);
    }

    int reuse_address = 1;

    if (setsockopt(listen_descriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_address, sizeof(reuse_address)) < 0) {
        perror("setsocketopt");
        close(listen_descriptor);
        exit(1);
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = INADDR_ANY; // Local IP
    server_address.sin_port = htons(port); 

    // Bind the socket to the specified port
    if (bind(listen_descriptor,
             (struct sockaddr*)&server_address,
             sizeof(server_address)) < 0)
    {
        perror("bind");
        close(listen_descriptor);
        exit(1);
    }

        // Start listening, with a backlog for pending connections
    if (listen(listen_descriptor, BACKLOG) < 0) {
        perror("listen");
        close(listen_descriptor);
        exit(1);
    }

    printf("Server listening on TCP port %d\n", port);
    return listen_descriptor;
}

// Room structure definition
typedef struct
{
    int id;
    int x;
    int y;
    char description[256];
    bool isItemRoom;
    bool isConnectorRoom;
    bool isStartRoom;
    int connectedDungeon; // Which dungeon this connector leads to (-1 if none)
} Room;

// First dungeon
Room dungeon1[10] = {
    {0, 0, 3, "You are at coordinates (0,3)", false, false, true, -1}, // Start Room (top left)
    {1, 1, 3, "You are at coordinates (1,3)", false, false, false, -1},
    {2, 2, 3, "You are at coordinates (2,3)", false, false, false, -1},
    {3, 0, 2, "You are at coordinates (0,2)", false, false, false, -1},
    {4, 3, 0, "You are at coordinates (3,0)", false, true, false, 1}, // Connector Room (bottom right)
    {5, 2, 2, "You are at coordinates (2,2)", false, false, false, -1},
    {6, 0, 1, "You are at coordinates (0,1)", false, false, false, -1},
    {7, 1, 1, "You are at coordinates (1,1)", false, false, false, -1},
    {8, 2, 1, "You are at coordinates (2,1)", true, false, false, -1}, // Item Room
    {9, 3, 1, "You are at coordinates (3,1)", false, false, false, -1}};

// Second dungeon
Room dungeon2[10] = {
    {0, 0, 3, "You are at coordinates (0,3)", false, true, true, 0}, // Start Room (top left) - also connects to main dungeon
    {1, 1, 3, "You are at coordinates (1,3)", false, false, false, -1},
    {2, 2, 3, "You are at coordinates (2,3)", false, false, false, -1},
    {3, 0, 2, "You are at coordinates (0,2)", false, false, false, -1},
    {4, 3, 0, "You are at coordinates (3,0)", false, true, false, 2}, // Connector Room (bottom right)
    {5, 2, 2, "You are at coordinates (2,2)", false, false, false, -1},
    {6, 0, 1, "You are at coordinates (0,1)", false, false, false, -1},
    {7, 1, 1, "You are at coordinates (1,1)", false, false, false, -1},
    {8, 2, 1, "You are at coordinates (2,1)", true, false, false, -1}, // Item Room
    {9, 3, 1, "You are at coordinates (3,1)", false, false, false, -1}};

// Third dungeon
Room dungeon3[10] = {
    {0, 1, 0, "You find yourself navigating an armory of sorts. Around you there are numerous weapons surrounding an archway to the East.", false, true, true, 1}, // Start room, connects to dungeon2
    {1, 1, 1, "There are more weapons around you. A hallway continues to the East, and there is an open cage to the south.", false, false, false, -1},
    {2, 0, 1, "You continue into the cage. It is simply a cage. The door is still open behind you, you know. You should, like, get out.", false, false, false, -1},
    {3, 1, 2, "The barren hallway leads you towards the central room towards the North.", false, false, false, -1},
    {4, 2, 2, "The central room has multiple hallways, one to the North and one to the East.", false, false, false, -1},
    {5, 3, 2, "The northern hall is barren and gray, with curtains on all of the walls, making it difficult to see a path.", false, false, false, -1},
    {6, 2, 3, "You continue out of the armory and into a new area.", false, true, false, 3}, // Connector room to dungeon 4
    {7, 3, 1, "You push through a curtain and find yourself into a warm, furnished room. It's cozy here. You almost don't want to continue through the hallway to the North.", false, false, false, -1},
    {8, 4, 1, "You continue through the scary hallway. Nothing to do except move forward. Or go back to the nice room behind you. You choose.", false, false, false, -1},
    {9, 5, 1, "Finally, you find yourself in a lavish throne room with many riches. Congratulations, you made it!", true, false, false, -1}}; // Item Room

// Fourth dungeon
Room dungeon4[10] = {
    {0, 0, 1, "You are in a room that is filled on almost all sides with chairs. The only way out is a room to the East.", false, true, true, 0}, // Start Room (near bottom left)
    {1, 1, 1, "You are now at a split choice in a corridor made of metal. You can either move West to the room of chairs, move North through a door, or move South.", false, false, false, -1},
    {2, 1, 0, "You are now in the south corner of the metal corridor. You can either move up North or move East to investigate the nearby room.", false, false, false, -1},
    {3, 2, 0, "You are in a dark room. It's impossible to make anything out, so there's nowhere to go, except the door to the West.", false, false, false, -1},
    {4, 1, 2, "You are in a room made entirely made of candy. Despite the temptations, you must choose to either move South to the metal corridor or move East through a door.", false, false, false, 2},
    {5, 2, 2, "You are in a room that's made of creaky wood. The way East seems a bit unstable, but you could probably jump through the doorway. You could also choose to move to the West, which you can tell is safe.", false, false, false, -1},
    {6, 3, 2, "You are in a room made of soft bedding. You can move through the gold door North of you, or investigate the heat in the room to the South.", false, false, false, -1},
    {7, 3, 3, "You are in a room that appears to be made of gold... but it turns out to be a trick. The yellow-painted floor gives way, and you fall for a long time. Everything goes dark. When you regain consciousness...", false, true, false, -1}, // Connector room (top right)
    {8, 3, 1, "You are in a room that's on fire. You can either brave the flames and move to the east, or flee to the chill of the North.", false, false, false, -1},
    {9, 4, 1, "You are in a room made entirely of gold. There's a treasure chest at the end of the room. You look inside... Inside is a small golden chair. Maybe it's valuable...? THE END.", true, false, false, -1}}; //Item room (near bottom right)

// Array of dungeon pointers for easy access
Room *dungeons[NUM_DUNGEONS] = {dungeon1, dungeon2, dungeon3, dungeon4};


int currentRoom = 0;
int currentDungeon = 0;

// Find room by coordinates
int findRoomByCoords(int x, int y)
{
    for (int i = 0; i < NUM_ROOMS; i++)
    {
        if (dungeons[currentDungeon][i].x == x && dungeons[currentDungeon][i].y == y)
        {
            return i;
        }
    }
    return -1;
}

// Sends the description of the current room to the connected client over MQTT, plus some server-side printing to console for vertification
// MQTT payload should be handled and printed on the clientside after receiving.
void printRoomDescription(int roomId)
{
    char *description = dungeons[currentDungeon][roomId].description;
    printf("\n%s\n", description);
    printf("Current Dungeon: %d\n", currentDungeon);

    if (dungeons[currentDungeon][roomId].isConnectorRoom &&
        dungeons[currentDungeon][roomId].connectedDungeon != -1)
    {
        printf("This connector leads to Dungeon %d\n",
               dungeons[currentDungeon][roomId].connectedDungeon);
    }

    int ret = mosquitto_publish(mosquitto_client, NULL, "game/room/description", strlen(description), description, 0, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to publish message: %s\n", mosquitto_strerror(ret));
    }
}

int movePlayer(char direction)
{
    int newX = dungeons[currentDungeon][currentRoom].x;
    int newY = dungeons[currentDungeon][currentRoom].y;

    switch (direction)
    {
    case 'N':
    case 'n':
        newY++;
        break;
    case 'S':
    case 's':
        newY--;
        break;
    case 'E':
    case 'e':
        newX++;
        break;
    case 'W':
    case 'w':
        newX--;
        break;
    default:
        printf("Player tried to move in invalid direction.\n");
        SENDMSG("Invalid direction! Use N, S, E, or W.");
        return 0;
    }

    // Check if we're trying to move through a connector
    if ((dungeons[currentDungeon][currentRoom].isConnectorRoom ||
         dungeons[currentDungeon][currentRoom].isStartRoom) &&
        dungeons[currentDungeon][currentRoom].connectedDungeon != -1)
    {
        // If moving in the direction that would take us off the grid
        if ((direction == 'N' && newY >= GRID_SIZE) ||
            (direction == 'S' && newY < 0) ||
            (direction == 'E' && newX >= GRID_SIZE) ||
            (direction == 'W' && newX < 0))
        {

            int nextDungeon = dungeons[currentDungeon][currentRoom].connectedDungeon;
            printf("Moving to Dungeon %d...\n", nextDungeon);
            currentDungeon = nextDungeon;

            // If it's a start room, go to the connector room instead of room 0
            if (dungeons[currentDungeon][currentRoom].isStartRoom)
            {
                int connectorRoom = -1;
                for (int i = 0; i < NUM_ROOMS; i++)
                {
                    if (dungeons[currentDungeon][i].isConnectorRoom)
                    {
                        connectorRoom = i;
                        break;
                    }
                }
                currentRoom = connectorRoom;
            }
            // Go to starting room
            else
            {
                currentRoom = 0; // Start at the start room of the new dungeon
            }

            printf("Entered Dungeon %d\n", currentDungeon);
            printRoomDescription(currentRoom);
            return 1;
        }
    }

    int newRoom = findRoomByCoords(newX, newY);
    if (newRoom != -1)
    {
        currentRoom = newRoom;
        printRoomDescription(currentRoom);
        return 1;
    }

    printf("Cannot move in that direction - no room there!\n");
    SENDMSG("Cannot move in that direction - no room there!");
    return 0;
}

void printDungeon()
{
    printf("\nDungeon Layout (Dungeon %d):\n", currentDungeon);
    printf("   0   1   2   3\n");
    printf("  +---+---+---+---+\n");

    for (int y = 3; y >= 0; y--)
    {
        printf("%d |", y);
        for (int x = 0; x < 4; x++)
        {
            int room = findRoomByCoords(x, y);
            if (room != -1)
            {
                if (room == currentRoom)
                {
                    printf(" * |"); // Current room
                }
                else if (dungeons[currentDungeon][room].isStartRoom)
                {
                    printf(" S |"); // Start room
                }
                else if (dungeons[currentDungeon][room].isConnectorRoom)
                {
                    printf(" C |"); // Connector room
                }
                else if (dungeons[currentDungeon][room].isItemRoom)
                {
                    printf(" I |"); // Item room
                }
                else
                {
                    printf(" R |"); // Regular room
                }
            }
            else
            {
                printf("   |"); // Empty space
            }
        }
        printf("\n  +---+---+---+---+\n");
    }
}

// Handles / interprets commands coming from the ESP32. 
void handle_incoming_command(int client_descriptor) {
SENDMSG("Welcome to the MUD! Enter N, S, W, or E to move North, South, West, or East respectively..\n> ");
printRoomDescription(currentRoom);

    char buffer[COMMAND_BUFFER_SIZE];
    ssize_t n;

    while ((n = read(client_descriptor, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        cleanup_input(buffer);

        if (strcmp(buffer, "N") == 0 || strcmp(buffer, "S") == 0 || strcmp(buffer, "E") == 0 || strcmp(buffer, "W") == 0) {
            char direction;

            if (strcmp(buffer, "S")== 0) {
                direction = 'n';
            }
            else if (strcmp(buffer, "E")== 0) {
                direction = 's';
            } 
            else if (strcmp(buffer, "W" )== 0) {
                direction = 'e';
            }
            else {
                direction = 'w';
            }

            movePlayer(direction);
        }
    }
}

int main()
{
mosquitto_lib_init();
mosquitto_client = mosquitto_new("dungeon-server", true, NULL);

if (!mosquitto_client) {
    fprintf(stderr, "Error: No valid pointer\n");
    exit(1);
}

if (mosquitto_connect(mosquitto_client, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
    fprintf(stderr, "Error: Couldn't connect to broker\n");
    exit(1);
}


    char input[10];

    // Initialize random seed
    srand(time(NULL));

    // Start in a random room
    currentRoom = rand() % 10;


    int listen_descriptor = setup_listener(TCP_PORT);

    while (1) {
        struct sockaddr_in client_address;
        socklen_t addresslength = sizeof(client_address);
        int client_descriptor = accept(listen_descriptor, (struct sockaddr*)&client_address, &addresslength);

        if (client_descriptor < 0) {
            perror("accept");
            continue;
        }

        handle_incoming_command(client_descriptor);
    }

    mosquitto_loop_stop(mosquitto_client, true);
    mosquitto_disconnect(mosquitto_client);
    mosquitto_destroy(mosquitto_client);
    mosquitto_lib_cleanup();

    return 0;

}
