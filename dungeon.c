#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <mosquitto.h>

static struct mosquitto *mosquitto_client = NULL;

#define GRID_SIZE 4
#define NUM_ROOMS 10
#define NUM_DUNGEONS 4 // Number of dungeons
#define TCP_PORT 4000
#define BACKLOG 1 // Number of connections allowed (1 currently)
#define BUFFER_SIZE 256
#define COMMAND_BUFFER_SIZE 128
#define SENDMSG(x) mqttClient.publish("game/message/serverToClient", x)

static void cleanup_input(char *input) {
    char *stripped = input + strlen(s);

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

    if (setsockopt(listen_descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0) {
        perror("setsocketopt");
        close(listen_descriptor);
        exit(1);
    }

    struct socketaddress_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = INADDR_ANY; // Local IP
    server_address.sin_port = htons(port); 

    // Bind the socket to the specified port
    if (bind(listen_descriptor,
             (struct socketaddress*)&server_address,
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
    return 
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
    {4, 3, 0, "You are at coordinates (3,0)", false, true, false, 0}, // Connector Room (bottom right)
    {5, 2, 2, "You are at coordinates (2,2)", false, false, false, -1},
    {6, 0, 1, "You are at coordinates (0,1)", false, false, false, -1},
    {7, 1, 1, "You are at coordinates (1,1)", false, false, false, -1},
    {8, 2, 1, "You are at coordinates (2,1)", true, false, false, -1}, // Item Room
    {9, 3, 1, "You are at coordinates (3,1)", false, false, false, -1}};

// Array of dungeon pointers for easy access
Room *dungeons[NUM_DUNGEONS] = {dungeon1, dungeon2, NULL, NULL};


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
    char *description = dungeons[currentDungeon][roomID].description;
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
        SENDMSG("Invalid direction! Use N, S, E, or W.")
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
    char buffer[COMMAND_BUFFER_SIZE];
    ssize_t n;

    while ((n = read(client_descriptor, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        cleanup_input(buffer);

        if (strcmp(buffer, "N") == 0 || strcmp(buffer, "S") == 0 || strcmp(buffer, "E") == 0 || strcmp(buffer, "W") == 0) {
            char direction;

            if (strcmp(buffer, "NORTH")==0) {
                direction = 'n';
            }
            else if (strcmp(buffer, "SOUTH")==0) {
                direction = 's';
            } 
            else if (strcmp(buffer, "EAST" )==0) {
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
    fprintf(stderr, "Error: No valid pointer\n")
}

if (mosquitto_connect(mosquitto_client, "localhost, 1883, 60") != MOSQ_ERR_SUCCESS) {
    fprintf(stdrerr, "Error: Couldn't connect to broker\n");
    exit(1);
}


    char input[10];

    // Initialize random seed
    srand(time(NULL));

    // Start in a random room
    currentRoom = rand() % 10;

    printf("Welcome to the Dungeon!\n");
    printf("Type 'H' for help, 'Q' to quit.\n");
    printRoomDescription(currentRoom);

    while (1)
    {
        printf("\nEnter command: ");
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        // Remove newline
        input[strcspn(input, "\n")] = 0;

        if (input[0] == 'D' || input[0] == 'd')
        {
            printDungeon();
        }
        else if (strlen(input) == 1)
        {
            movePlayer(input[0]);
        }
        else
        {
            printf("Invalid command!");
        }
    }

    mosquitto_loop_stop(mosquitto_client, true);
    mosquitto_disconnect(mosquitto_client);
    mosquitto_destroy(mosquitto_client);
    mosquitto_lib_cleanup();

    return 0;

}
