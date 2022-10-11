// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/distributed/common/afs_api.h"
//#include "common/timer.h"
//#include <comlog/comlog.h>
namespace paddle {
namespace distributed {

void Zerr(int ret) {
  switch (ret) {
    case Z_ERRNO:
      if (ferror(stdin)) LOG(ERROR) << "zpipe: error reading stdin";
      if (ferror(stdout)) LOG(ERROR) << "zpipe: error writing stdout";
      break;
    case Z_STREAM_ERROR:
      LOG(ERROR) << "zpipe: invalid compression level";
      break;
    case Z_DATA_ERROR:
      LOG(ERROR) << "zpipe: invalid or incomplete deflate data";
      break;
    case Z_MEM_ERROR:
      LOG(ERROR) << "zpipe: out of memory";
      break;
    case Z_VERSION_ERROR:
      LOG(ERROR) << "zpipe: zlib version mismatch!";
  }
}

AfsWriter::AfsWriter(afs::AfsFileSystem* afshandler, std::string filename) {
  _afshandler = afshandler;
  if (paddle::string::start_with(filename, "afs:")) {
    filename = filename.substr(4);
  }
  if (paddle::string::end_with(filename, ".gz")) {
    _use_gz = 1;
  }

  size_t buf_len = MAX_BUF_SIZE;
  _line_buf = (char*)calloc(buf_len + 1, sizeof(char));
  _zlib_buf = (char*)calloc(CHUNK, sizeof(char));

  struct afs::CreateOptions create_opts;
  create_opts.num_replica = 3;
  int ret = _afshandler->Create(filename.c_str(), create_opts);
  if (ret < 0) {
    LOG(ERROR) << "fs create file failed: " << filename;
    exit(-1);
  }

  afs::WriterOptions w_options;
  w_options.write_mode = afs::kPipeLine;
  w_options.num_safe_replica = 2;
  w_options.buffer_size = 1 * 1024 * 1024;
  _writer = _afshandler->OpenWriter(filename.c_str(), w_options);
  if (_writer == NULL) {
    LOG(ERROR) << "fail to open writer for file " << filename
               << " errno: " << afs::GetRc()
               << " errmsg: " << afs::Rc2Str(afs::GetRc());
    exit(-1);
  }

  _strm.zalloc = Z_NULL;
  _strm.zfree = Z_NULL;
  _strm.opaque = Z_NULL;

  if (deflateInit2(&_strm,
                   Z_DEFAULT_COMPRESSION,
                   Z_DEFLATED,
                   MAX_WBITS + 16,
                   8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    LOG(ERROR) << "zlib init error, exit";
    exit(-1);
  }
}

AfsWriter::~AfsWriter() {
  flush_writer();
  if (_line_buf != NULL) {
    free(_line_buf);
    _line_buf = NULL;
  }
  if (_zlib_buf != NULL) {
    free(_zlib_buf);
    _zlib_buf = NULL;
  }
  if (_writer->Sync() != 0) {
    LOG(WARNING) << "fail to sync afs writer";
    exit(-1);
  }
  if (_afshandler != NULL) {
    if (_afshandler->CloseWriter(_writer) != 0) {
      LOG(WARNING) << "fail to close afs writer";
      exit(-1);
    }
  }
  deflateEnd(&_strm);
}

void AfsWriter::flush_writer() {
  if (_read_count != 0) {
    _strm.next_in = (Bytef*)_line_buf;
    _strm.avail_in = _read_count;
    compress(Z_NO_FLUSH);
    _read_count = 0;
  }
  if (_use_gz) {
    _strm.next_in = (Bytef*)_line_buf;
    _strm.avail_in = _read_count;
    compress(Z_FINISH);
  }
}

int AfsWriter::write(const char* data, size_t len, bool direct) {
  if (len > CHUNK || direct) {
    _strm.next_in = (Bytef*)data;
    _strm.avail_in = len;
    compress(Z_NO_FLUSH);
    return 0;
  }
  if (len + _read_count >= CHUNK) {
    _strm.next_in = (Bytef*)_line_buf;
    _strm.avail_in = _read_count;
    compress(Z_NO_FLUSH);
    _read_count = 0;
  }
  memcpy(_line_buf + _read_count, data, len);
  _read_count += len;
  return 0;
}

int AfsWriter::write(const uint64_t k, const std::vector<float>& value) {
  uint32_t len =
      sizeof(uint64_t) + value.size() * sizeof(float) + sizeof(uint32_t);
  if (len + _read_count >= CHUNK) {
    _strm.next_in = (Bytef*)_line_buf;
    _strm.avail_in = _read_count;
    compress(Z_NO_FLUSH);
    _read_count = 0;
  }
  *(uint32_t*)(_line_buf + _read_count) = len;
  _read_count += sizeof(uint32_t);

  *(uint64_t*)(_line_buf + _read_count) = k;
  _read_count += sizeof(uint64_t);

  memcpy(_line_buf + _read_count, value.data(), sizeof(float) * value.size());
  _read_count += sizeof(float) * value.size();
  return 0;
}

int AfsWriter::compress(int mode) {
  int flush = mode;
  int zerr = Z_OK;

  if (!_use_gz) {
    int res_len = _writer->Append(_strm.next_in, _strm.avail_in);
    if (res_len < 0) {
      LOG(WARNING) << "fail to append, expect_len: " << _strm.avail_in
                   << ", errno: " << res_len
                   << ", errmsg:" << afs::Rc2Str(res_len);
      exit(-1);
    } else if (static_cast<uint32_t>(res_len) != _strm.avail_in) {
      LOG(WARNING) << "fail to append, expect_len: " << _strm.avail_in
                   << ", res_len: " << res_len;
      exit(-1);
    }
    return 0;
  }

  do {
    _strm.next_out = (Bytef*)_zlib_buf;
    _strm.avail_out = CHUNK;
    zerr = deflate(&_strm, flush);
    if (flush == Z_NO_FLUSH && zerr != Z_OK) {
      Zerr(zerr);
      exit(-1);
    }
    if (flush == Z_FINISH && zerr != Z_STREAM_END) {
      Zerr(zerr);
      exit(-1);
    }
    int have = CHUNK - _strm.avail_out;
    {
      int res_len = _writer->Append(_zlib_buf, have);
      if (res_len < 0) {
        LOG(WARNING) << "fail to append, expect_len: " << have
                     << ", errno: " << res_len
                     << ", errmsg:" << afs::Rc2Str(res_len);
        exit(-1);
      } else if (res_len != have) {
        LOG(WARNING) << "fail to append, expect_len: " << have
                     << ", res_len: " << res_len;
        exit(-1);
      }
    }
  } while (_strm.avail_out == 0);
  // if (flush == Z_FINISH) {
  //     deflateReset(_strm);
  // }
  return 0;
}

AfsReader::AfsReader(afs::AfsFileSystem* afshandler, std::string filename) {
  _afshandler = afshandler;
  if (paddle::string::start_with(filename, "afs:")) {
    filename = filename.substr(4);
  }
  if (paddle::string::end_with(filename, ".gz")) {
    _use_gz = 1;
  }

  size_t buf_len = MAX_BUF_SIZE;
  _line_buf = (char*)calloc(buf_len + 1, sizeof(char));

  _reader = _afshandler->OpenReader(filename.c_str());
  if (_reader == NULL) {
    LOG(FATAL) << "Create Reader Fail  " << filename;
    _exit(-1);
  }

  _strm.zalloc = Z_NULL;
  _strm.zfree = Z_NULL;
  _strm.opaque = Z_NULL;
  _strm.next_in = NULL;
  _strm.avail_in = 0;
  _strm.data_type = Z_UNKNOWN;

  int zerr = inflateInit2(&_strm, 16 + MAX_WBITS);
  if (zerr != Z_OK) {
    LOG(ERROR) << "zlib init error, exit";
  }
}

AfsReader::~AfsReader() {
  if (_line_buf != NULL) {
    free(_line_buf);
    _line_buf = NULL;
  }
  if (_afshandler != NULL) {
    _afshandler->CloseReader(_reader);
  }
  inflateEnd(&_strm);
}

int AfsReader::read(char* buf, int len) {
  if (!_use_gz) {
    return _reader->Read(buf, len);
  } else {
    int finished = 0;
    do {
      if (_strm.avail_in != 0) {
        _strm.next_out = (Bytef*)(buf + finished);
        _strm.avail_out = len;
        int zerr = inflate(&_strm, Z_NO_FLUSH);
        if (zerr != Z_OK && zerr != Z_STREAM_END) {
          Zerr(zerr);
          _exit(-1);
        }
        int have = len - _strm.avail_out;
        finished += have;
        len -= have;
      } else {
        _strm.avail_in = _reader->Read(_line_buf, MAX_BUF_SIZE);
        _strm.next_in = (Bytef*)_line_buf;
        if (_strm.avail_in <= 0) {
          return finished;
        }
      }
    } while (len);
    return finished;
  }
}

int AfsApiWrapper::exist(const std::string& path) {
  std::string path_name = paddle::string::trim_afs(path);
  int ret = _afshandler->Exist(path_name.c_str());
  return ret == 0;
}

int AfsApiWrapper::init(const char* fs_name,
                        const char* fs_user,
                        const char* pass,
                        const char* conf) {
  _afshandler =
      new afs::AfsFileSystem(fs_name, fs_user, pass, "./conf/client.conf");
  int ret = _afshandler->Init(true, true);
  if (ret != 0) {
    return 1;
  }
  ret = _afshandler->Connect();
  if (ret != 0) {
    return 1;
  }
  return 0;
}

AfsApiWrapper::~AfsApiWrapper() {
  if (_afshandler != NULL) {
    _afshandler->DisConnect();
    _afshandler->Destroy();
    delete _afshandler;
    _afshandler = nullptr;
  }
}

std::vector<std::string> AfsApiWrapper::list(const std::string& path) {
  std::string path_name = paddle::string::trim_afs(path);
  std::vector<afs::DirEntry> entrys;
  std::vector<std::string> filenames;
  _afshandler->Readdir(path_name.c_str(), &entrys);
  for (auto& entry : entrys) {
    std::string filename = path_name + '/' + entry.name;
    filenames.push_back(filename);
  }
  return filenames;
}

int AfsApiWrapper::touchz(const std::string& path) {
  std::string path_name = paddle::string::trim_afs(path);
  int ret = 0;
  // int read_len = 0;
  open_writer(path_name);
  return ret;
}

int AfsApiWrapper::upload_file(const std::string& local_file,
                               const std::string& afs_file) {
  FILE* fd = fopen(local_file.c_str(), "r");
  if (fd == NULL) {
    VLOG(0) << "ERROR: file of " << local_file << " not exist";
    return -1;
  }
  std::string afs_file_name = paddle::string::trim_afs(afs_file);
  int ret = 0;
  int read_len = 0;
  char* buf = (char*)malloc(MAX_BUF_SIZE + 10);
  auto writer = open_writer(afs_file_name);
  while ((read_len = fread(buf, sizeof(char), MAX_BUF_SIZE, fd)) > 0) {
    int res = writer->write(buf, read_len);
    if (res != 0) {
      VLOG(0) << "ERROR: upload file failed";
      ret = -1;
      break;
    }
  }
  fclose(fd);
  free(buf);
  return ret;
}

int AfsApiWrapper::download_file(const std::string& local_file,
                                 const std::string& afs_file) {
  FILE* fd = fopen(local_file.c_str(), "wb");
  if (fd == NULL) {
    VLOG(0) << "ERROR: file of " << local_file << " not exist";
    return -1;
  }
  std::string afs_file_name = paddle::string::trim_afs(afs_file);
  int ret = 0;
  int read_len = 0;
  char* buf = (char*)malloc(MAX_BUF_SIZE + 10);
  auto reader = open_reader(afs_file_name);
  while ((read_len = reader->read(buf, MAX_BUF_SIZE)) > 0) {
    int num = fwrite(buf, 1, read_len, fd);
    if (num != read_len) {
      VLOG(0) << "ERROR: download file [" << afs_file << "]failed";
      ret = -1;
      break;
    }
  }
  fclose(fd);
  free(buf);
  return ret;
}

std::string AfsApiWrapper::cat(const std::string& path) {
  std::string afs_file_name = paddle::string::trim_afs(path);
  // int ret = 0;
  std::string output = "";
  int read_len = 0;
  char* buf = (char*)malloc(MAX_BUF_SIZE + 10);
  auto reader = open_reader(afs_file_name);
  while ((read_len = reader->read(buf, MAX_BUF_SIZE)) > 0) {
    output += buf;
  }
  free(buf);
  return output;
}

int AfsApiWrapper::remove(const std::string& path) {
  std::string path_name = paddle::string::trim_afs(path);
  int ret = _afshandler->Delete(path_name.c_str(), true);
  return ret != 0;
}

int AfsApiWrapper::mv(const std::string& old_path,
                      const std::string& dest_path) {
  std::string old_path_name = paddle::string::trim_afs(old_path);
  std::string dest_path_name = paddle::string::trim_afs(dest_path);
  int ret = _afshandler->Move(old_path_name.c_str(), dest_path_name.c_str());
  return ret != 0;
}

int AfsApiWrapper::mkdir(const std::string& path) {
  std::string path_name = paddle::string::trim_afs(path);
  int ret = _afshandler->Mkdir(path_name.c_str());
  if (ret == 0 || afs::GetErrno() != EEXIST) {
    return 0;
  }
  return 1;
}

std::shared_ptr<AfsWriter> AfsApiWrapper::open_writer(
    const std::string& filename) {
  return std::make_shared<AfsWriter>(_afshandler, filename);
}

std::shared_ptr<AfsReader> AfsApiWrapper::open_reader(
    const std::string& filename) {
  return std::make_shared<AfsReader>(_afshandler, filename);
}

}  // namespace distributed
}  // namespace paddle
