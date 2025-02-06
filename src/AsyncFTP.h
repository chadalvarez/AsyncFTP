#ifndef ASYNCFTP_H
#define ASYNCFTP_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <AsyncTCP.h>

#define FILEBUFFERSIZE 512    // Buffer size for file transfers
#define DEFAULT_FTP_PORT 21   // Default FTP control port

// Filesystem selection for FTP file operations.
enum class FTP_FS {
    NONE,
    LITTLEFS,
    SD_CARD
};

// Global credentials (these can be overridden via begin())
extern String ASYNCFTP_Username;
extern String ASYNCFTP_Password;

class AsyncFTPClient; // Forward declaration

class AsyncFTP {
public:
    AsyncFTP(uint16_t port = DEFAULT_FTP_PORT, FTP_FS ftpfs = FTP_FS::NONE);

    // Updated begin method with optional username and password parameters.
    // If nonempty values are provided, they will be used to set the FTP credentials.
    void begin(const String &username = "", const String &password = "");

    void setUsername(String username);
    void setPassword(String password);
    
private:
    void _onClient(void* arg, AsyncClient* client);
    uint16_t _port;
    FTP_FS _FTPFS;
    AsyncServer* _asyncServer = nullptr;
    // Allow up to 2 simultaneous control connections.
    AsyncFTPClient* _controlClients[2] = { nullptr, nullptr };
    
    friend class AsyncFTPClient;
};

class AsyncFTPClient {
public:
    AsyncFTPClient(AsyncClient* client, FTP_FS ftpfs);
    ~AsyncFTPClient();
    
private:
    // Called when data arrives on the control connection.
    void _onData(void* arg, AsyncClient* client, void* data, size_t len);
    // Process a complete FTP command.
    void _process();

    // Passive mode functions.
    void _createPassiveServer(uint16_t port);
    void _onPassiveClient(void* arg, AsyncClient* client);
    void _onPassiveData(void* arg, AsyncClient* client, void* data, size_t len);
    void _processListCommand(AsyncClient* client);
    void _processStorCommand(AsyncClient* client);
    void _processRetrCommand(AsyncClient* client);
    void _sendFileChunk(AsyncClient* client);
    void _onPassiveDisconnect(void* arg, AsyncClient* client);
    
    // *** Active mode support functions ***
    // Initiate an active data connection (the server connects to the client).
    void _createActiveDataConnection();
    // Called when the active connection is established.
    void _onActiveConnect(void* arg, AsyncClient* client);
    
    // Private members:
    AsyncClient* _controlClient;
    FTP_FS _FTPFS;
    String _CWD = "/";
    String _line = "";
    String _command = "";
    String _parameter = "";
    String _dataCommand = "";
    String _dataParameter = "";
    String _RNFRParameter = "";
    
    // For passive mode:
    AsyncServer* _passiveServer = nullptr;
    fs::File _STORFile;
    fs::File _RETRFile;
    
    // *** Active mode member variables ***
    // When a PORT command is received these are set.
    bool         _activeMode       = false;
    IPAddress    _activeDataIP;
    uint16_t     _activeDataPort   = 0;
    AsyncClient* _activeDataClient = nullptr;
    
    // Maximum allowed command length to prevent runaway buffering.
    static const size_t MAX_COMMAND_LENGTH = 256;
    
    friend class AsyncFTP;
};

// --- FTP File/Directory helper function declarations ---
// These functions implement simple file/directory operations on the underlying filesystem.
String _FTPDirectoryList(String path, FTP_FS ftpfs = FTP_FS::NONE);
bool _FTPCreateDirectory(FTP_FS ftpfs, String path);
bool _FTPDeleteDirectory(FTP_FS ftpfs, String path);
bool _FTPDeleteFile(FTP_FS ftpfs, String path);
fs::File _FTPCreateFile(FTP_FS ftpfs, String path);
fs::File _FTPOpenFile(FTP_FS ftpfs, String path);
bool _FTPMoveFile(FTP_FS ftpfs, String from, String to);

#endif // ASYNCFTP_H
