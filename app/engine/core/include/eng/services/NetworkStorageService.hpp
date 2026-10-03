#ifndef ENG_NETWORK_STORAGE_SERVICE_HPP
#define ENG_NETWORK_STORAGE_SERVICE_HPP

#include "eng/interfaces/IFileSystemBrowser.hpp"
#include "eng/services/FtpClient.hpp"
#include <string>
#include <vector>
#include <memory>

namespace engine {

enum class NetworkProtocol {
    FTP = 0,
    SMB = 1
};

struct NetworkShare {
    std::string id;
    std::string name;
    NetworkProtocol protocol = NetworkProtocol::FTP;
    std::string host = "127.0.0.1";
    int port = 2121;
    std::string user = "anonymous";
    std::string password = "anonymous";
    std::string startPath = "/";
};

class NetworkStorageService {
public:
    static NetworkStorageService& getInstance();

    NetworkStorageService();
    ~NetworkStorageService();

    void loadConfig();
    void saveConfig();

    const std::vector<NetworkShare>& getShares() const { return m_shares; }
    const NetworkShare* getShare(const std::string& id) const;
    void addShare(const NetworkShare& share);
    void removeShare(const std::string& id);

    bool listEntries(const std::string& shareId, const std::string& remotePath,
                     std::vector<BrowserEntry>& outEntries, std::string& outError);

    std::string buildMediaUrl(const std::string& shareId, const std::string& remotePath) const;
    std::string buildMediaUrl(const NetworkShare& share, const std::string& remotePath) const;

    FileCategory classifyFileName(const std::string& fileName) const;

private:
    std::vector<NetworkShare> m_shares;
    std::unique_ptr<FtpClient> m_ftpClient;
    std::string m_lastConnectedShareId;
};

} // namespace engine

#endif // ENG_NETWORK_STORAGE_SERVICE_HPP
