#include <WiFi.h>
#include "AsyncFTP.h"

// === Global login credentials ===
String ASYNCFTP_Username = "admin";
String ASYNCFTP_Password = "admin";

//---------------------------------------------------------------------
// Helper functions (internal to the library)
//---------------------------------------------------------------------

// Return a pointer to the selected filesystem.
static FS* getFilesystem(FTP_FS ftpfs) {
    switch (ftpfs) {
        case FTP_FS::LITTLEFS: return &LittleFS;
        case FTP_FS::SD_CARD:  return &SD;
        default:               return nullptr;
    }
}

// Join two path components ensuring exactly one '/' separator.
static String joinPath(const String &base, const String &sub) {
    if (base.endsWith("/")) return base + sub;
    else                    return base + "/" + sub;
}

//---------------------------------------------------------------------
// FTP File/Directory functions
//---------------------------------------------------------------------

String _FTPDirectoryList(String path, FTP_FS ftpfs) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) { return "550 Failed to open directory. No Filesystem Found\r\n"; }

    File dir = filesystem->open(path);
    if (!dir)        { return "550 Failed to open directory\r\n"; }

    String listing = "";
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) listing += "drwxr-xr-x 1 user group ";
        else                    listing += "-rw-r--r-- 1 owner group ";
        listing += String(file.size()) + " Jan 1 00:00 " + String(file.name()) + "\r\n";
        file = dir.openNextFile();
    }
    dir.close();
    return listing;
}

bool _FTPCreateDirectory(FTP_FS ftpfs, String path) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return false;
    return filesystem->mkdir(path);
}

bool _FTPDeleteDirectory(FTP_FS ftpfs, String path) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return false;
    return filesystem->rmdir(path);
}

bool _FTPDeleteFile(FTP_FS ftpfs, String path) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return false;
    return filesystem->remove(path);
}

fs::File _FTPCreateFile(FTP_FS ftpfs, String path) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return fs::File();
    return filesystem->open(path, "w");
}

fs::File _FTPOpenFile(FTP_FS ftpfs, String path) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return fs::File();
    return filesystem->open(path, "r");
}

bool _FTPMoveFile(FTP_FS ftpfs, String from, String to) {
    FS *filesystem = getFilesystem(ftpfs);
    if (!filesystem) return false;
    return filesystem->rename(from, to);
}

// A helper to resolve a path that might be absolute or relative:
static String resolvePath(const String &cwd, const String &param) {
  if (param.startsWith("/")) { return param; } 
  else {
    if (cwd == "/") { return "/" + param; } 
    else            { return cwd + "/" + param; }
  }
}


//---------------------------------------------------------------------
// AsyncFTP methods
//---------------------------------------------------------------------

AsyncFTP::AsyncFTP(uint16_t port, FTP_FS ftpfs): _port(port), _FTPFS(ftpfs) {}

// Updated begin method with optional username and password parameters.
void AsyncFTP::begin(const String &username, const String &password) {
    if (username.length() > 0) { setUsername(username); }
    if (password.length() > 0) { setPassword(password); }
    _asyncServer = new AsyncServer(_port);
    _asyncServer->onClient([this](void *arg, AsyncClient *client) { _onClient(arg, client); }, this);
    _asyncServer->begin();
}

void AsyncFTP::setUsername(String username) {
    ASYNCFTP_Username = username;
}

void AsyncFTP::setPassword(String password) {
    ASYNCFTP_Password = password;
}

void AsyncFTP::_onClient(void *arg, AsyncClient *client) {
    // Allow a maximum of 2 simultaneous control connections.
    if (_controlClients[0] == nullptr) {
        _controlClients[0] = new AsyncFTPClient(client, _FTPFS);
        client->onDisconnect([this](void *arg, AsyncClient *client) {
            delete _controlClients[0];
            _controlClients[0] = nullptr;
        }, this);
    }
    else if (_controlClients[1] == nullptr) {
        _controlClients[1] = new AsyncFTPClient(client, _FTPFS);
        client->onDisconnect([this](void *arg, AsyncClient *client)
        {
            delete _controlClients[1];
            _controlClients[1] = nullptr;
        }, this);
    }
    else {
        client->write("421 Too many connections\r\n");
        client->close();
    }
}

//---------------------------------------------------------------------
// AsyncFTPClient methods
//---------------------------------------------------------------------

AsyncFTPClient::AsyncFTPClient(AsyncClient *client, FTP_FS ftpfs) : _controlClient(client), _FTPFS(ftpfs) {
    _controlClient->write("220 Welcome to ESP32 FTP Server\r\n");
    _controlClient->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
        _onData(arg, client, data, len);
    }, this);
}

AsyncFTPClient::~AsyncFTPClient() {
    if (_controlClient) {
        _controlClient->close();
        delete _controlClient;
    }
}

void AsyncFTPClient::_onData(void *arg, AsyncClient *client, void *data, size_t len) {
    // Append incoming data to the command buffer.
    _line += String((char*)data).substring(0, len);
    
    // Enforce a maximum command length.
    if (_line.length() > MAX_COMMAND_LENGTH) {
        _controlClient->write("500 Command too long\r\n");
        _line = "";
        return;
    }
    
    int index;
    // Process complete command lines terminated with "\r\n".
    while ((index = _line.indexOf("\r\n")) >= 0) {
        String commandLine = _line.substring(0, index);
        _line = _line.substring(index + 2);
        commandLine.trim();
        if (commandLine.length() == 0)
            continue;
        
        int spaceIndex = commandLine.indexOf(' ');
        if (spaceIndex > 0) {
            _command = commandLine.substring(0, spaceIndex);
            _parameter = commandLine.substring(spaceIndex + 1);
        } else {
            _command = commandLine;
            _parameter = "";
        }
        
        Serial.println("Received Command: " + commandLine);
        _process();
    }
}

void AsyncFTPClient::_process() {
    _command.toUpperCase();
    
#ifdef DEBUG
    Serial.println("========PROCESSING COMMAND=========");
    Serial.printf("Command: %s\n", _command.c_str());
    Serial.printf("Parameter: %s\n", _parameter.c_str());
    Serial.println("===================================");
#endif

    if (_command == "USER") {
        if (_parameter == ASYNCFTP_Username)
            _controlClient->write("331 OK. Password required\r\n");
        else {
            _controlClient->write("530 Invalid username\r\n");
            _controlClient->close();
        }
    }
    else if (_command == "PASS") {
        if (_parameter == ASYNCFTP_Password)
            _controlClient->write("230 OK. User logged in\r\n");
        else {
            _controlClient->write("530 Invalid password\r\n");
            _controlClient->close();
        }
    }
    else if (_command == "SYST") {
        _controlClient->write("215 UNIX Type: L8\r\n");
    }
    else if (_command == "CDUP") {
          // If we're at root ("/"), can't go higher:
          if (_CWD == "/") {
            _controlClient->write("550 Can't go above root directory\r\n");
          } 
          else {
            // Trim back _CWD by removing its trailing segment
            int index = _CWD.lastIndexOf('/');
            if (index <= 0) {
              _CWD = "/";
            } else {
              _CWD = _CWD.substring(0, index);
            }
            _controlClient->write("250 Directory successfully changed\r\n");
          }
    }
    else if (_command == "CWD") {
          String newPath = resolvePath(_CWD, _parameter);
        
          // 1) Get the filesystem pointer
          FS* filesystem = getFilesystem(_FTPFS);
          if (!filesystem) {
            _controlClient->write("550 No valid filesystem\r\n");
            return;
          }
        
          // 2) Try opening 'newPath' as a directory
          File dir = filesystem->open(newPath);
          if (!dir || !dir.isDirectory()) {
            // Not a valid directory
            _controlClient->write("550 Not a valid directory\r\n");
            return;
          }
          dir.close();
        
          // 3) If valid, update current working directory
          _CWD = newPath;
          _controlClient->write("250 OK\r\n");
    }
    else if (_command == "PWD") {
        String response = "257 \"" + _CWD + "\" is the current directory\r\n";
        _controlClient->write(response.c_str());
    }
    else if (_command == "TYPE") {
        String response = "200 Type set to " + _parameter + "\r\n";
        _controlClient->write(response.c_str());
    }
    else if (_command == "PASV") {
        // 1) Get the ESP32's local IP address
        IPAddress ip = WiFi.localIP();
      
        // Choose an ephemeral data port
        uint16_t port = random(1024, 65535);
        byte portHigh = port >> 8;   // High-order byte
        byte portLow  = port & 0xFF; // Low-order byte
      
        // 4) Build the '227 Entering Passive Mode' response with the **real** IP
        //    e.g. "227 Entering Passive Mode (192,168,68,116,abcd,efgh)\r\n"
        String response = "227 Entering Passive Mode (" +
            String(ip[0]) + "," + 
            String(ip[1]) + "," + 
            String(ip[2]) + "," + 
            String(ip[3]) + "," +
            String(portHigh) + "," + 
            String(portLow) + ")\r\n";
      
        _controlClient->write(response.c_str());
      
        // 5) Create the listening data server on the chosen random port
        _createPassiveServer(port);
    }
    else if (_command == "PORT") {
        // Active mode: parse the PORT command (format: h1,h2,h3,h4,p1,p2).
        int parts[6];
        int startIndex = 0;
        int partIndex = 0;
        while (partIndex < 6) {
            int commaIndex = _parameter.indexOf(',', startIndex);
            if (commaIndex < 0 && partIndex < 5)
                break;
            String part = (commaIndex < 0) ? _parameter.substring(startIndex)
                                           : _parameter.substring(startIndex, commaIndex);
            parts[partIndex] = part.toInt();
            startIndex = commaIndex + 1;
            partIndex++;
        }
        if (partIndex < 6) {
            _controlClient->write("501 Syntax error in parameters or arguments\r\n");
        } else {
            _activeDataIP = IPAddress(parts[0], parts[1], parts[2], parts[3]);
            _activeDataPort = (uint16_t)(parts[4] * 256 + parts[5]);
            _activeMode = true;
            _controlClient->write("200 PORT command successful\r\n");
        }
    }
    else if (_command == "LIST") {
        _dataCommand = _command;
        if (_activeMode)
            _createActiveDataConnection();
        // Otherwise, in passive mode the client connects to our passive server.
    }
    else if (_command == "MKD") {
        String MKDName = (_CWD == "/" ? _CWD : joinPath(_CWD, _parameter));
        if (_FTPCreateDirectory(_FTPFS, MKDName))
            _controlClient->write("257 Directory created\r\n");
        else
            _controlClient->write("550 Failed to create directory\r\n");
            
    }
    else if (_command == "RMD") {
          String fullPath = resolvePath(_CWD, _parameter);
          if (_FTPDeleteDirectory(_FTPFS, fullPath))
            _controlClient->write("250 Directory deleted\r\n");
          else
            _controlClient->write("550 Failed to delete directory\r\n");
    }
    else if (_command == "RETR") {
        _dataCommand = _command;
        _dataParameter = _parameter;
        if (_activeMode)
            _createActiveDataConnection();
    }
    else if (_command == "STOR") {
        _dataCommand = _command;
        _dataParameter = _parameter;
        if (_activeMode)
            _createActiveDataConnection();
    }
    else if (_command == "DELE") {
          String fullPath = resolvePath(_CWD, _parameter);
          if (_FTPDeleteFile(_FTPFS, fullPath))
            _controlClient->write("250 File deleted\r\n");
          else
            _controlClient->write("550 Failed to delete file\r\n");
    }
    else if (_command == "RNFR") {
        _RNFRParameter = _parameter; // store it for RNTO
        _controlClient->write("350 Ready for RNTO\r\n");
    }
    else if (_command == "RNTO") {
          // Convert both RNFR and RNTO parameters to absolute paths if needed
          String fromPath = resolvePath(_CWD, _RNFRParameter);
          String toPath   = resolvePath(_CWD, _parameter);
        
          if (_FTPMoveFile(_FTPFS, fromPath, toPath)) {
            _controlClient->write("250 File renamed\r\n");
          } else {
            _controlClient->write("550 Failed to rename file\r\n");
          }
    }
    else if (_command == "QUIT") {
        _controlClient->write("221 Goodbye\r\n");
        _controlClient->close();
    }
    else {
        _controlClient->write("502 Command not implemented\r\n");
    }
    
    // Clear the command buffers for the next command.
    _command = "";
    _parameter = "";
}

void AsyncFTPClient::_createPassiveServer(uint16_t port) {
    _passiveServer = new AsyncServer(port);
    _passiveServer->onClient([this](void *arg, AsyncClient *client)
    {
        _onPassiveClient(arg, client);
    }, this);
    _passiveServer->begin();
}

void AsyncFTPClient::_onPassiveClient(void *arg, AsyncClient *client) {
    Serial.println("Passive client connected");
    client->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
        _onPassiveData(arg, client, data, len);
    }, this);
    client->onDisconnect([this](void *arg, AsyncClient *client) {
        _onPassiveDisconnect(arg, client);
    }, this);
    
    if      (_dataCommand == "LIST")  _processListCommand(client);
    else if (_dataCommand == "RETR")  _processRetrCommand(client);
    else if (_dataCommand == "STOR")  _processStorCommand(client);
    _dataCommand = "";
}

void AsyncFTPClient::_onPassiveData(void *arg, AsyncClient *client, void *data, size_t len) {
    if (_STORFile)
        _STORFile.write((uint8_t*)data, len);
}

void AsyncFTPClient::_processListCommand(AsyncClient *client) {
    String listing = _FTPDirectoryList(_CWD, _FTPFS);
    client->write(listing.c_str());
    client->close();
}

void AsyncFTPClient::_processStorCommand(AsyncClient *client) {
    String path = (_CWD == "/" ? "" : _CWD);
    path = joinPath(path, _dataParameter);
    _STORFile = _FTPCreateFile(_FTPFS, path);
}

void AsyncFTPClient::_processRetrCommand(AsyncClient *client) {
    String path = (_CWD == "/" ? "" : _CWD);
    path = joinPath(path, _dataParameter);
    _RETRFile = _FTPOpenFile(_FTPFS, path);
    if (_RETRFile) {
        _controlClient->write("150 Sending file\r\n");
        _sendFileChunk(client);
        client->onPoll([this](void *arg, AsyncClient *client)
        {
            _sendFileChunk(client);
        }, this);
    }
    else
        _controlClient->write("550 Failed to open file\r\n");
}

void AsyncFTPClient::_sendFileChunk(AsyncClient *client) {
    if (_RETRFile) {
        uint8_t fileBuffer[FILEBUFFERSIZE];
        size_t bytesRead = _RETRFile.read(fileBuffer, FILEBUFFERSIZE);
        if (bytesRead > 0) {
            client->add((const char*)fileBuffer, bytesRead);
            client->send();
        }
        else {
            _RETRFile.close();
            client->close();
            _controlClient->write("226 Transfer complete\r\n");
        }
    }
}

void AsyncFTPClient::_onPassiveDisconnect(void *arg, AsyncClient *client) {
    _controlClient->write("226 Closing data connection\r\n");
    Serial.println("Passive client disconnected");
    if (_STORFile)
        _STORFile.close();
    delete client;
    delete _passiveServer;
}

// --------------------------------------------------------------------
// *** Active Mode Functions ***
// In active mode the client sends a PORT command.
// When a data command (LIST, RETR, STOR) is received and _activeMode is true,
// the server immediately creates an AsyncClient and connects to the client's IP/port.
// --------------------------------------------------------------------

void AsyncFTPClient::_createActiveDataConnection() {
    _activeDataClient = new AsyncClient();
    _activeDataClient->onConnect([this](void *arg, AsyncClient *client) {
        _onActiveConnect(arg, client);
    }, this);
    _activeDataClient->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
        _onPassiveData(arg, client, data, len); // reuse the same callback for data.
    }, this);
    _activeDataClient->onDisconnect([this](void *arg, AsyncClient *client) {
          // Send a “closing data connection” message.
          _controlClient->write("226 Closing data connection\r\n");
          Serial.println("Active data connection disconnected");
      
          // If a file was open for STOR, close it:
          if (_STORFile) {
              _STORFile.close();
          }
      
          delete _activeDataClient;
          _activeDataClient = nullptr;
          _activeMode = false;
    }, this);
    // Initiate connection to the client's provided IP and port.
    if (!_activeDataClient->connect(_activeDataIP, _activeDataPort)) {
        _controlClient->write("425 Can't open data connection\r\n");
        delete _activeDataClient;
        _activeDataClient = nullptr;
        _activeMode = false;
    }
}

void AsyncFTPClient::_onActiveConnect(void *arg, AsyncClient *client) {
    Serial.println("Active data connection established");
    if      (_dataCommand == "LIST") _processListCommand(client);
    else if (_dataCommand == "RETR") _processRetrCommand(client);
    else if (_dataCommand == "STOR") _processStorCommand(client);
    _dataCommand = "";
}
