#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <winsock2.h>
#include <mysql.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <fstream>
#include <sys/stat.h>
#include <direct.h>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"libmysql.lib")

using namespace std;

MYSQL* conn;
const string UPLOAD_DIR = "uploads/";

struct AttachmentData {
    string fileName;
    string filePath;
    long long fileSize;
};

bool safe_stoll(const string& str, long long& result) {
    if (str.empty()) return false;
    try {
        result = stoll(str);
        return true;
    }
    catch (const invalid_argument&) {
        return false;
    }
    catch (const out_of_range&) {
        return false;
    }
}

void ensureUploadDir() {
    _mkdir(UPLOAD_DIR.c_str());
}

string saveFile(const string& fileName, const vector<char>& data) {
    ensureUploadDir();
    string filePath = UPLOAD_DIR + fileName;
    ofstream file(filePath, ios::binary);
    if (file.is_open()) {
        file.write(data.data(), data.size());
        file.close();
        return filePath;
    }
    return "";
}

string escapeJson(const string& str) {
    string result;
    for (char c : str) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c;
        }
    }
    return result;
}

// URL解码函数
string urlDecode(const string& str) {
    string result;
    for (size_t i = 0; i < str.length(); ) {
        if (str[i] == '%' && i + 2 < str.length()) {
            char hex[3] = { str[i + 1], str[i + 2], 0 };
            result += (char)strtol(hex, NULL, 16);
            i += 3;
        }
        else if (str[i] == '+') {
            result += ' ';
            i++;
        }
        else {
            result += str[i];
            i++;
        }
    }
    return result;
}

// 解析GET参数
string parseGetParam(const string& req, const string& key) {
    // 先尝试查找 ?key=（第一个参数）
    size_t start = req.find("?" + key + "=");
    if (start != string::npos) {
        start += key.length() + 2;
    }
    else {
        // 如果没找到，尝试查找 &key=（后续参数）
        start = req.find("&" + key + "=");
        if (start == string::npos) return "";
        start += key.length() + 2;
    }

    size_t end = req.find("&", start);
    if (end == string::npos) end = req.find(" ", start);
    return urlDecode(req.substr(start, end - start));
}

string parsePostParam(const string& req, const string& key) {
    size_t bodyStart = req.find("\r\n\r\n");
    if (bodyStart == string::npos) return "";
    string body = req.substr(bodyStart + 4);

    size_t keyStart = body.find(key + "=");
    if (keyStart == string::npos) return "";
    keyStart += key.length() + 1;
    size_t keyEnd = body.find("&", keyStart);
    if (keyEnd == string::npos) keyEnd = body.length();

    return urlDecode(body.substr(keyStart, keyEnd - keyStart));
}

string parseMultipartBoundary(const string& req) {
    const string prefix = "Content-Type: multipart/form-data; boundary=";
    size_t contentTypePos = req.find(prefix);
    if (contentTypePos == string::npos) return "";
    size_t start = contentTypePos + prefix.length();
    size_t end = req.find("\r\n", start);
    return req.substr(start, end - start);
}

struct MultipartField {
    string name;
    string fileName;
    string contentType;
    vector<char> data;
};

vector<MultipartField> parseMultipart(const string& req, const string& boundary) {
    vector<MultipartField> fields;
    size_t bodyStart = req.find("\r\n\r\n");
    if (bodyStart == string::npos) return fields;

    string body = req.substr(bodyStart + 4);
    string delimiter = "--" + boundary;
    string endDelimiter = "--" + boundary + "--";

    size_t pos = 0;
    while (pos < body.length()) {
        size_t partStart = body.find(delimiter, pos);
        if (partStart == string::npos) break;

        size_t partEnd = body.find(delimiter, partStart + delimiter.length());
        if (partEnd == string::npos) break;

        string part = body.substr(partStart + delimiter.length(), partEnd - partStart - delimiter.length());

        size_t headerEnd = part.find("\r\n\r\n");
        if (headerEnd != string::npos) {
            string headers = part.substr(0, headerEnd);
            string content = part.substr(headerEnd + 4);

            MultipartField field;

            size_t namePos = headers.find("name=\"");
            if (namePos != string::npos) {
                namePos += 6;
                size_t nameEnd = headers.find("\"", namePos);
                field.name = headers.substr(namePos, nameEnd - namePos);
            }

            size_t filenamePos = headers.find("filename=\"");
            if (filenamePos != string::npos) {
                filenamePos += 10;
                size_t filenameEnd = headers.find("\"", filenamePos);
                field.fileName = headers.substr(filenamePos, filenameEnd - filenamePos);
            }

            size_t ctPos = headers.find("Content-Type: ");
            if (ctPos != string::npos) {
                ctPos += 14;
                size_t ctEnd = headers.find("\r\n", ctPos);
                field.contentType = headers.substr(ctPos, ctEnd - ctPos);
            }

            size_t dataStart = 0;
            while (dataStart < content.length() && (content[dataStart] == '\r' || content[dataStart] == '\n')) {
                dataStart++;
            }
            size_t dataEnd = content.length();
            while (dataEnd > dataStart && (content[dataEnd - 1] == '\r' || content[dataEnd - 1] == '\n')) {
                dataEnd--;
            }

            field.data.assign(content.begin() + dataStart, content.begin() + dataEnd);
            fields.push_back(field);
        }

        pos = partEnd;

        if (body.substr(partEnd, endDelimiter.length()) == endDelimiter) break;
    }

    return fields;
}

// 数据库连接
bool connectDB() {
    conn = mysql_init(NULL);
    return mysql_real_connect(conn, "127.0.0.1", "root", "123456", "email_db", 3306, NULL, 0);
}

// 安全的注册函数（使用参数化查询）
string registerUser(const string& username, const string& password, const string& email) {
    if (username.empty() || password.empty() || email.empty()) {
        return "fail: empty parameters";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "INSERT INTO user(username, password, email) VALUES(?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "fail: " + string(mysql_error(conn));
    }

    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (char*)username.c_str();
    params[0].buffer_length = username.length();

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (char*)password.c_str();
    params[1].buffer_length = password.length();

    params[2].buffer_type = MYSQL_TYPE_STRING;
    params[2].buffer = (char*)email.c_str();
    params[2].buffer_length = email.length();

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "fail: " + string(mysql_stmt_error(stmt));
    }

    mysql_stmt_close(stmt);
    return "ok";
}

// 安全的登录函数（使用参数化查询）
string login(const string& username, const string& password) {
    if (username.empty() || password.empty()) {
        return "0";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "SELECT id FROM user WHERE username=? AND password=?";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "0";
    }

    MYSQL_BIND params[2];
    memset(params, 0, sizeof(params));

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (char*)username.c_str();
    params[0].buffer_length = username.length();

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (char*)password.c_str();
    params[1].buffer_length = password.length();

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "0";
    }

    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    long long userId = 0;
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &userId;

    mysql_stmt_bind_result(stmt, &result);
    mysql_stmt_fetch(stmt);

    mysql_stmt_close(stmt);
    return userId > 0 ? to_string(userId) : "0";
}

// 获取邮件的附件列表
string getAttachments(long long mailId) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "SELECT id, file_name, file_path, file_size FROM attachment WHERE mail_id=?";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "[]";
    }

    MYSQL_BIND params[1];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &mailId;

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "[]";
    }

    mysql_stmt_store_result(stmt);

    string json = "[";
    bool first = true;

    MYSQL_BIND results[4];
    memset(results, 0, sizeof(results));

    long long attId, attSize;
    char fileName[256], filePath[512];

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &attId;

    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = fileName;
    results[1].buffer_length = 256;

    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = filePath;
    results[2].buffer_length = 512;

    results[3].buffer_type = MYSQL_TYPE_LONGLONG;
    results[3].buffer = &attSize;

    mysql_stmt_bind_result(stmt, results);

    while (mysql_stmt_fetch(stmt) == 0) {
        if (!first) json += ",";
        first = false;
        json += "{\"id\":" + to_string(attId) + ",";
        json += "\"fileName\":\"" + escapeJson(string(fileName)) + "\",";
        json += "\"filePath\":\"" + escapeJson(string(filePath)) + "\",";
        json += "\"fileSize\":" + to_string(attSize) + "}";
    }

    json += "]";
    mysql_stmt_close(stmt);
    return json;
}

// 获取邮件列表（根据用户名查询）
string getMails(const string& username) {
    if (username.empty()) {
        return "[]";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "SELECT id, sender, subject, content, is_read, create_time FROM mail WHERE receiver=? AND is_deleted=0 ORDER BY id DESC";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "[]";
    }

    MYSQL_BIND params[1];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (char*)username.c_str();
    params[0].buffer_length = username.length();

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "[]";
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    MYSQL_ROW row;
    mysql_stmt_store_result(stmt);

    string json = "[";
    bool first = true;

    MYSQL_BIND results[6];
    memset(results, 0, sizeof(results));

    long long id;
    char sender[256], subject[512], content[4096], createTime[64];
    char isRead;  // 修改为 char，因为 MySQL TINYINT(1) 是 1 字节

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;

    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = sender;
    results[1].buffer_length = 256;

    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = subject;
    results[2].buffer_length = 512;

    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = content;
    results[3].buffer_length = 4096;

    results[4].buffer_type = MYSQL_TYPE_TINY;
    results[4].buffer = &isRead;

    results[5].buffer_type = MYSQL_TYPE_STRING;
    results[5].buffer = createTime;
    results[5].buffer_length = 64;

    mysql_stmt_bind_result(stmt, results);

    while (mysql_stmt_fetch(stmt) == 0) {
        int isReadInt = static_cast<int>(isRead);
        cout << "getMails: receiver=" << username << ", id=" << id << ", is_read=" << isReadInt << endl;
        if (!first) json += ",";
        first = false;
        json += "{\"id\":" + to_string(id) + ",";
        json += "\"sender\":\"" + escapeJson(string(sender)) + "\",";
        json += "\"subject\":\"" + escapeJson(string(subject)) + "\",";
        json += "\"content\":\"" + escapeJson(string(content)) + "\",";
        json += "\"isRead\":" + to_string(isReadInt) + ",";
        json += "\"createTime\":\"" + string(createTime) + "\",";
        json += "\"attachments\":" + getAttachments(id) + "}";
    }

    json += "]";
    mysql_stmt_close(stmt);
    return json;
}

string sendMail(const string& sender, const string& receiver, const string& subject, const string& content, const vector<AttachmentData>& attachments = {}) {
    if (sender.empty() || receiver.empty()) {
        return "fail: empty sender or receiver";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "INSERT INTO mail(sender, receiver, subject, content, is_read) VALUES(?, ?, ?, ?, 0)";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "fail: " + string(mysql_error(conn));
    }

    MYSQL_BIND params[4];
    memset(params, 0, sizeof(params));

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (char*)sender.c_str();
    params[0].buffer_length = sender.length();

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (char*)receiver.c_str();
    params[1].buffer_length = receiver.length();

    params[2].buffer_type = MYSQL_TYPE_STRING;
    params[2].buffer = (char*)subject.c_str();
    params[2].buffer_length = subject.length();

    params[3].buffer_type = MYSQL_TYPE_STRING;
    params[3].buffer = (char*)content.c_str();
    params[3].buffer_length = content.length();

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "fail: " + string(mysql_stmt_error(stmt));
    }

    long long mailId = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);

    for (const auto& att : attachments) {
        MYSQL_STMT* attStmt = mysql_stmt_init(conn);
        const char* attSql = "INSERT INTO attachment(mail_id, file_name, file_path, file_size) VALUES(?, ?, ?, ?)";

        if (mysql_stmt_prepare(attStmt, attSql, strlen(attSql)) != 0) {
            mysql_stmt_close(attStmt);
            continue;
        }

        MYSQL_BIND attParams[4];
        memset(attParams, 0, sizeof(attParams));

        attParams[0].buffer_type = MYSQL_TYPE_LONGLONG;
        attParams[0].buffer = &mailId;

        attParams[1].buffer_type = MYSQL_TYPE_STRING;
        attParams[1].buffer = (char*)att.fileName.c_str();
        attParams[1].buffer_length = att.fileName.length();

        attParams[2].buffer_type = MYSQL_TYPE_STRING;
        attParams[2].buffer = (char*)att.filePath.c_str();
        attParams[2].buffer_length = att.filePath.length();

        attParams[3].buffer_type = MYSQL_TYPE_LONGLONG;
        attParams[3].buffer = (long long*)&att.fileSize;

        if (mysql_stmt_bind_param(attStmt, attParams) != 0 || mysql_stmt_execute(attStmt) != 0) {
            cout << "Failed to save attachment: " << mysql_stmt_error(attStmt) << endl;
        }
        mysql_stmt_close(attStmt);
    }

    return "ok";
}

// 标记已读
string setRead(const string& mailId) {
    cout << "setRead called with mailId: " << mailId << endl;

    long long id;
    if (!safe_stoll(mailId, id)) {
        cout << "Invalid mailId: " << mailId << endl;
        return "fail: invalid mailId";
    }

    cout << "Parsed id: " << id << endl;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "UPDATE mail SET is_read=1 WHERE id=?";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        cout << "Prepare failed: " << mysql_error(conn) << endl;
        mysql_stmt_close(stmt);
        return "fail";
    }

    MYSQL_BIND params[1];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &id;

    if (mysql_stmt_bind_param(stmt, params) != 0) {
        cout << "Bind failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return "fail";
    }

    if (mysql_stmt_execute(stmt) != 0) {
        cout << "Execute failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return "fail";
    }

    int affected = mysql_stmt_affected_rows(stmt);
    cout << "Affected rows: " << affected << endl;
    mysql_stmt_close(stmt);
    return affected > 0 ? "ok" : "fail";
}

// 删除邮件（软删除）
string deleteMail(const string& mailId) {
    long long id;
    if (!safe_stoll(mailId, id)) {
        return "fail: invalid mailId";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "UPDATE mail SET is_deleted=1 WHERE id=?";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return "fail";
    }

    MYSQL_BIND params[1];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &id;

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "fail";
    }

    int affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    return affected > 0 ? "ok" : "fail";
}

// 搜索邮件（根据用户名查询）
string searchMail(const string& username, const string& keyword) {
    cout << "searchMail called with username: '" << username << "', keyword: '" << keyword << "'" << endl;

    if (username.empty()) {
        return "[]";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    const char* sql = "SELECT id, sender, subject, content, is_read, create_time FROM mail WHERE receiver=? AND (subject LIKE CONCAT('%', ?, '%') OR content LIKE CONCAT('%', ?, '%')) AND is_deleted=0 ORDER BY id DESC";

    cout << "SQL query: " << sql << endl;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        cout << "Failed to prepare statement: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return "[]";
    }

    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (char*)username.c_str();
    params[0].buffer_length = username.length();

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (char*)keyword.c_str();
    params[1].buffer_length = keyword.length();

    params[2].buffer_type = MYSQL_TYPE_STRING;
    params[2].buffer = (char*)keyword.c_str();
    params[2].buffer_length = keyword.length();

    if (mysql_stmt_bind_param(stmt, params) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return "[]";
    }

    MYSQL_BIND results[6];
    memset(results, 0, sizeof(results));

    long long id;
    char sender[256], subject[512], content[4096], createTime[64];
    char isRead;  // 修改为 char，因为 MySQL TINYINT(1) 是 1 字节

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;

    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = sender;
    results[1].buffer_length = 256;

    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = subject;
    results[2].buffer_length = 512;

    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = content;
    results[3].buffer_length = 4096;

    results[4].buffer_type = MYSQL_TYPE_TINY;
    results[4].buffer = &isRead;

    results[5].buffer_type = MYSQL_TYPE_STRING;
    results[5].buffer = createTime;
    results[5].buffer_length = 64;

    mysql_stmt_bind_result(stmt, results);

    string json = "[";
    bool first = true;
    int count = 0;

    while (mysql_stmt_fetch(stmt) == 0) {
        count++;
        int isReadInt = static_cast<int>(isRead);
        if (!first) json += ",";
        first = false;
        json += "{\"id\":" + to_string(id) + ",";
        json += "\"sender\":\"" + string(sender) + "\",";
        json += "\"subject\":\"" + string(subject) + "\",";
        json += "\"content\":\"" + string(content) + "\",";
        json += "\"isRead\":" + to_string(isReadInt) + ",";
        json += "\"createTime\":\"" + string(createTime) + "\"}";
    }

    json += "]";
    mysql_stmt_close(stmt);
    cout << "searchMail returned " << count << " results" << endl;
    return json;
}

// 启动服务器
void startServer() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << "WSA初始化失败" << endl;
        return;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        cout << "创建socket失败" << endl;
        WSACleanup();
        return;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cout << "绑定端口失败" << endl;
        closesocket(s);
        WSACleanup();
        return;
    }

    if (listen(s, 5) == SOCKET_ERROR) {
        cout << "监听失败" << endl;
        closesocket(s);
        WSACleanup();
        return;
    }

    cout << ">>> 邮件服务端已启动 8080 <<<" << endl;

    while (true) {
        SOCKET c = accept(s, 0, 0);
        if (c == INVALID_SOCKET) continue;

        char buf[4096];
        string req;
        int totalBytes = 0;
        const int MAX_REQUEST_SIZE = 10 * 1024 * 1024;

        fd_set readFds;
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        while (true) {
            FD_ZERO(&readFds);
            FD_SET(c, &readFds);

            int ret = select(0, &readFds, NULL, NULL, &timeout);
            if (ret <= 0) break;

            int bytesRead = recv(c, buf, sizeof(buf), 0);
            if (bytesRead <= 0) break;

            req.append(buf, bytesRead);
            totalBytes += bytesRead;
            if (totalBytes >= MAX_REQUEST_SIZE) break;
        }
        string body = "{\"error\":\"invalid request\"}";

        // 调试：打印完整请求
        cout << "=== REQUEST START ===" << endl;
        cout << "Request size: " << req.size() << " bytes" << endl;
        cout << req.substr(0, min((size_t)500, req.size())) << endl;  // 只打印前500字符
        cout << "=== REQUEST END ===" << endl;

        // 解析请求参数
        string username = parsePostParam(req, "username");
        string password = parsePostParam(req, "password");
        string email = parsePostParam(req, "email");
        string userId = parsePostParam(req, "userId");
        string receiver = parsePostParam(req, "receiver");
        string subject = parsePostParam(req, "subject");
        string content = parsePostParam(req, "content");
        string mailId = parsePostParam(req, "mailId");
        string keyword = parseGetParam(req, "keyword");

        // GET请求参数回退
        if (userId.empty()) userId = parseGetParam(req, "userId");
        if (keyword.empty()) keyword = parseGetParam(req, "keyword");

        // 路由处理
        if (req.find("POST /api/register") != string::npos) {
            cout << "Processing register request" << endl;
            body = "{\"status\":\"" + registerUser(username, password, "") + "\"}";
        }
        else if (req.find("POST /api/login") != string::npos) {
            cout << "Processing login request for: " << username << endl;
            string uid = login(username, password);
            body = "{\"userId\":" + uid + ",\"status\":\"" + (uid != "0" ? "success" : "fail") + "\"}";
        }
        else if (req.find("GET /api/mails") != string::npos) {
            cout << "Processing getMails request for: " << userId << endl;
            body = "{\"list\":" + getMails(userId) + "}";
        }
        else if (req.find("POST /api/send") != string::npos) {
            cout << "Processing sendMail from: " << userId << " to: " << receiver << endl;

            string boundary = parseMultipartBoundary(req);
            vector<AttachmentData> attachments;

            if (!boundary.empty()) {
                cout << "Multipart request detected, boundary: " << boundary << endl;
                vector<MultipartField> fields = parseMultipart(req, boundary);

                for (const auto& field : fields) {
                    cout << "Field: " << field.name << ", filename: " << field.fileName << ", size: " << field.data.size() << endl;

                    if (field.name == "userId") {
                        userId = string(field.data.begin(), field.data.end());
                    }
                    else if (field.name == "receiver") {
                        receiver = string(field.data.begin(), field.data.end());
                    }
                    else if (field.name == "subject") {
                        subject = string(field.data.begin(), field.data.end());
                    }
                    else if (field.name == "content") {
                        content = string(field.data.begin(), field.data.end());
                    }
                    else if (field.name == "attachments" && !field.fileName.empty()) {
                        string filePath = saveFile(field.fileName, field.data);
                        if (!filePath.empty()) {
                            attachments.push_back({ field.fileName, filePath, (long long)field.data.size() });
                            cout << "Saved attachment: " << field.fileName << " to " << filePath << endl;
                        }
                    }
                }
            }

            body = "{\"status\":\"" + sendMail(userId, receiver, subject, content, attachments) + "\"}";
        }
        else if (req.find("POST /api/read") != string::npos) {
            cout << "Processing mark as read request, mailId: " << mailId << endl;
            body = "{\"status\":\"" + setRead(mailId) + "\"}";
        }
        else if (req.find("POST /api/delete") != string::npos) {
            cout << "Processing delete request, mailId: " << mailId << endl;
            body = "{\"status\":\"" + deleteMail(mailId) + "\"}";
        }
        else if (req.find("GET /api/search") != string::npos) {
            cout << "Processing search request for: " << userId << ", keyword: '" << keyword << "'" << endl;
            body = "{\"list\":" + searchMail(userId, keyword) + "}";
        }
        else if (req.find("GET /api/download") != string::npos) {
            string filePath = parseGetParam(req, "path");
            cout << "Processing download request for: " << filePath << endl;

            size_t pos = filePath.find("..");
            if (pos != string::npos) {
                body = "{\"error\":\"invalid path\"}";
            }
            else {
                ifstream file(filePath, ios::binary);
                if (file.is_open()) {
                    file.seekg(0, ios::end);
                    size_t fileSize = file.tellg();
                    file.seekg(0, ios::beg);

                    vector<char> fileData(fileSize);
                    file.read(fileData.data(), fileSize);
                    file.close();

                    string resp = "HTTP/1.1 200 OK\r\n";
                    resp += "Content-Type: application/octet-stream\r\n";
                    resp += "Access-Control-Allow-Origin: *\r\n";
                    resp += "Content-Disposition: attachment; filename=\"" + filePath.substr(filePath.find_last_of("/") + 1) + "\"\r\n";
                    resp += "Content-Length: " + to_string(fileSize) + "\r\n";
                    resp += "Connection: close\r\n\r\n";

                    send(c, resp.c_str(), resp.size(), 0);
                    send(c, fileData.data(), fileData.size(), 0);
                    closesocket(c);
                    continue;
                }
                else {
                    body = "{\"error\":\"file not found\"}";
                }
            }
        }

        // 发送响应
        string resp = "HTTP/1.1 200 OK\r\n";
        resp += "Content-Type: application/json; charset=utf-8\r\n";
        resp += "Access-Control-Allow-Origin: *\r\n";
        resp += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        resp += "Access-Control-Allow-Headers: Content-Type\r\n";
        resp += "Connection: close\r\n\r\n";
        resp += body;

        send(c, resp.c_str(), resp.size(), 0);
        closesocket(c);
    }

    closesocket(s);
    WSACleanup();
}

int main() {
    if (!connectDB()) {
        cout << "数据库连接失败: " << mysql_error(conn) << endl;
        return 0;
    }
    cout << "数据库连接成功" << endl;

    startServer();
    return 0;
}