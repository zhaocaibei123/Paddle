#pragma once

#include <string>
#include <zlib.h>
#include <memory>
#include "glog/logging.h"
#include <vector>
#include "baidu/inf/afs-api/client/afs_filesystem.h"
#include "paddle/fluid/string/string_helper.h"

namespace paddle {
namespace distributed {

const int MAX_BUF_SIZE = 1024 * 1024 * 4;
const int CHUNK = MAX_BUF_SIZE;

class AfsWriter {
public:
    AfsWriter(afs::AfsFileSystem* afshandler, std::string filename);
    virtual ~AfsWriter();
    AfsWriter(AfsWriter&&) = delete;
    AfsWriter(const AfsWriter&) = delete;
    void flush_writer();
    int compress(int mode);
    int write(const char* data, size_t len, bool direct = false);
    int write(const uint64_t k, const std::vector<float>& value);

private:
    std::string _filename;
    int _use_gz = 0;
    afs::WriterOptions _option;
    char* _line_buf = NULL;
    char* _zlib_buf = NULL;
    afs::Writer* _writer = NULL;
    afs::AfsFileSystem *_afshandler = NULL;
    z_stream _strm;
    int _read_count = 0;
};

class AfsReader {
public:
    AfsReader(afs::AfsFileSystem* afshandler, std::string filename);
    virtual ~AfsReader();
    int read(char* buf, int len);
    AfsReader(AfsReader&&) = delete;
    AfsReader(const AfsReader&) = delete;

private:
    std::string _filename;
    int _use_gz = 0;
    afs::WriterOptions _option;
    char* _line_buf = NULL;
    char* _zlib_buf = NULL;
    afs::Reader* _reader = nullptr;
    afs::AfsFileSystem *_afshandler = NULL;
    z_stream _strm;
    int _read_count = 0;
};

class AfsApiWrapper {
public:
    AfsApiWrapper() {};
    virtual ~AfsApiWrapper();
    int init(const char* fs_name, const char* fs_user, const char* pass, const char* conf);
    AfsApiWrapper(AfsApiWrapper&&) = delete;
    AfsApiWrapper(const AfsApiWrapper&) = delete;

    std::shared_ptr<AfsWriter> open_writer(const std::string& filename);
    std::shared_ptr<AfsReader> open_reader(const std::string& filename);
    int touchz(const std::string& path);
    int mv(const std::string& old_path, const std::string& dest_path);
    int remove(const std::string& path);
    int mkdir(const std::string& path);
    int download_file(const std::string& local_file, const std::string& afs_file);
    int upload_file(const std::string& local_file, const std::string& afs_file);
    std::vector<std::string> list(const std::string& path);
    std::string cat(const std::string& path);

    int exist(const std::string& dir);

private:
    std::string _fs_name;
    std::string _fs_ugi;
    afs::AfsFileSystem *_afshandler = NULL;
};

}
}
