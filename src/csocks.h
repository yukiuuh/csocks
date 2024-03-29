#define CSOCKS_PORT_DEFAULT 1080
#define USER_ID_BUFFER_SIZE 1024
#define DOMAINNAME_SIZE 256
#define DOMAINNAME_BUFFER_SIZE DOMAINNAME_SIZE + 1
#define MAX_CLIENT_NUM 100
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef enum
{
    VERSION4 = 4,
    VERSION5 = 5
} SOCKS_VERSIONS;

typedef enum
{
    ATYPE_IPV4 = 0x01,
    ATYPE_DOMAINNAME = 0x03,
    ATYPE_IPV6 = 0x04
} SOCKS_ADDRESS_TYPES;

typedef enum
{
    C_CONNECT = 1,
    C_BIND = 2,
    C_REQUEST_GRANTED = 90,
    C_REQUEST_REJECTED_OR_FAILED = 91,
    C_REQUEST_REJECTED_SERVER_CONNECT = 92,
    C_REQUEST_REJECTED_DIFFERENT_USER_IDS = 93
} SOCKS_CMDS;

typedef enum
{
    R_SUCCESS = 0x0,
    R_GENERAL_FAIL = 0x1,
    R_NON_PERMITTED = 0x2,
    R_NETWORK_UNREACHABLE = 0x3,
    R_HOST_UNREACHABLE = 0x4,
    R_CONNECTION_REFUSED = 0x5,
    R_TTL_EXPIRED = 0x6,
    R_UNSUPPORTED_COMMAND = 0x7,
    R_UNSUPPORTED_ATYPE = 0x8
} SOCKS5_REPLYS;

